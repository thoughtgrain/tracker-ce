// Sample strips in the pattern editor (docs/design/tape-ux.md §5): turns the visible tracks' steps
// into sampleStrip::Step lists, runs the model, and hands the result to the tracker control.
// Main loop only. Reads samples' peak caches; never touches the SD card or the audio interrupt.

#include "patternEditor/patternEditor.h"

#include <math.h>

#include "VoiceStreamManager.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "sampleStrip.h"

namespace
{
// The peak cache for a RAM-resident sample is built here, on first use. A loaded or deleted sample
// clears it (VoiceStreamManager::clearInstrumentMetadata), but an in-place edit in the Sample
// Editor doesn't, so the cache is tagged with where the sample lives, its length and a few of its
// values, and rebuilt when any of them changes.
struct PeakSignature
{
    const int16_t* address = nullptr;
    uint32_t length = 0;
    uint32_t hash = 0;
};
PeakSignature s_peakSignature[kSampleInstrumentsCount];

uint32_t quickHash(const int16_t* data, uint32_t length)
{
    uint32_t hash = 2166136261u;
    uint32_t stride = (length > 32) ? length / 32 : 1;
    for (uint32_t i = 0; i < length; i += stride)
    {
        hash = (hash ^ (uint16_t)data[i]) * 16777619u;
    }
    return hash;
}

// Each instrument's peaks are looked up (and its signature checked) once per refresh, however many
// notes use it.
uint16_t s_refreshGeneration = 0;
uint16_t s_peakGeneration[kSampleInstrumentsCount] = {};
const WaveformPeakPoint* s_peakThisRefresh[kSampleInstrumentsCount] = {};

const WaveformPeakPoint* lookUpPeaks(uint8_t instrIdx, uint32_t length);

const WaveformPeakPoint* peaksFor(uint8_t instrIdx, uint32_t length)
{
    if (s_peakGeneration[instrIdx] != s_refreshGeneration)
    {
        s_peakGeneration[instrIdx] = s_refreshGeneration;
        s_peakThisRefresh[instrIdx] = lookUpPeaks(instrIdx, length);
    }
    return s_peakThisRefresh[instrIdx];
}

const WaveformPeakPoint* lookUpPeaks(uint8_t instrIdx, uint32_t length)
{
    strSampleMemory& mem = mtProject.sampleMemory[instrIdx];

    if (mem.isStreamed)
    {
        // Streamed samples get their peaks read from the card when the stream is set up.
        return mem.hasWaveformPeaks ? voiceStreamManager.getInstrumentWaveformPeaks(instrIdx, 0)
                                    : nullptr;
    }

    const int16_t* data = mem.address[strSampleMemory::kLeft];
    if (data == nullptr)
    {
        data = mtProject.instrument[instrIdx].sample.address;
    }
    if (data == nullptr || length == 0)
    {
        return nullptr;
    }

    PeakSignature& sig = s_peakSignature[instrIdx];
    uint32_t hash = quickHash(data, length);
    if (!mem.hasWaveformPeaks || sig.address != data || sig.length != length || sig.hash != hash)
    {
        voiceStreamManager.buildWaveformPeaksFromRam(instrIdx, 0, data, length);
        sig.address = data;
        sig.length = length;
        sig.hash = hash;
    }
    return voiceStreamManager.getInstrumentWaveformPeaks(instrIdx, 0);
}

uint32_t pointToFrame(uint16_t point, uint32_t length)
{
    return (uint32_t)(((uint64_t)point * length) / MAX_16BIT);
}

// How a note on a sample instrument plays, for the model. Returns a silent playback for play modes
// that don't read a sample in order (Wavetable, Granular) or an instrument with nothing loaded.
sampleStrip::Playback makePlayback(uint8_t instrIdx, int8_t note)
{
    using sampleStrip::Mode;
    sampleStrip::Playback pb;

    const strInstrument& instr = mtProject.instrument[instrIdx];
    uint32_t length = instr.sample.length;
    if (!instr.isActive || length == 0)
    {
        return pb;
    }

    switch (instr.playMode)
    {
        case playModeSingleShot: pb.mode = Mode::kOneShot; break;
        case playModeLoopForward: pb.mode = Mode::kLoopForward; break;
        case playModeLoopBackward: pb.mode = Mode::kLoopBackward; break;
        case playModePingpong: pb.mode = Mode::kPingPong; break;
        case playModeSlice:
        case playModeBeatSlice: pb.mode = Mode::kOneShot; break;
        default: return pb; // Wavetable, Granular
    }

    pb.start = pointToFrame(instr.startPoint, length);
    pb.end = pointToFrame(instr.endPoint, length);
    pb.loop1 = pointToFrame(instr.loopPoint1, length);
    pb.loop2 = pointToFrame(instr.loopPoint2, length);

    int pitchNote = note;
    if (instr.playMode == playModeSlice || instr.playMode == playModeBeatSlice)
    {
        // Mirrors play_memory_slice.cpp / play_memory_beat_slice.cpp: Slice plays the selected
        // slice at the note's pitch, Beat Slice picks the slice from the note and plays it
        // unpitched.
        int slice = instr.selectedSlice;
        if (instr.playMode == playModeBeatSlice)
        {
            slice = note - FIRST_BEAT_SLICE_NOTE;
            pitchNote = Sequencer::STEP_NOTE_DEFAULT;
        }
        int count = instr.sliceNumber;
        if (slice < 0)
            slice = 0;
        if (count > 0 && slice > count - 1)
            slice = count - 1;
        uint16_t startPoint = (count > 0) ? instr.slices[slice] : 0;
        uint16_t endPoint = (!instr.slicePlayThru && count > 1 && slice != count - 1)
                              ? instr.slices[slice + 1]
                              : MAX_16BIT;
        pb.start = pointToFrame(startPoint, length);
        pb.end = pointToFrame(endPoint, length);
    }

    // AudioPlayMemory::notes[]: note 60 plays at the sample's own rate; fine tune is in cents.
    float semitones = (float)(pitchNote + instr.tune - 60) + (float)instr.fineTune / 100.0f;
    pb.framesPerSecond = 44100.0f * powf(2.0f, semitones / 12.0f);

    pb.peaks = peaksFor(instrIdx, length);
    pb.totalFrames = length;
    return pb;
}

// Sequencer::kStepsPerBeat, which is private: a step is a sixteenth.
constexpr int kStepsPerBeat = 4;

sampleStrip::StepKind s_steps[Sequencer::MAXSTEP + 1];

sampleStrip::Playback playbackOfStep(void* context, int step)
{
    const Sequencer::strPattern::strTrack* track =
      static_cast<const Sequencer::strPattern::strTrack*>(context);
    return makePlayback(track->step[step].instrument, track->step[step].note);
}
sampleStrip::Row s_rows[strTrackerPattern::kStripRows];
} // namespace

void cPatternEditor::refreshSampleStrips(int16_t patternPosition)
{
    using sampleStrip::StepKind;

    uint8_t mode = mtConfig.general.sampleStrips;
    // Only the vertical 4-track view has room; the 8-track view and the horizontal arrangement
    // don't draw strips, so they cost nothing there.
    trackerPattern.showStrips =
      (mode != kSampleStripsOff) && (patternViewMode == 0) &&
      (mtConfig.general.patternArrangement != PatternArrangement::kHorizontal);
    if (!trackerPattern.showStrips)
    {
        return;
    }

    // A new generation makes peaksFor() look every instrument up again; 0 is never used so a
    // freshly zeroed table can't match.
    if (++s_refreshGeneration == 0)
    {
        s_refreshGeneration = 1;
    }

    float tempo = sequencer.getCurrentTempo();
    if (tempo < 1.0f)
    {
        tempo = 1.0f;
    }

    sampleStrip::Options options;
    options.stepSeconds = 60.0f / (tempo * (float)kStepsPerBeat);
    options.minSteps = (mode == kSampleStripsLongSamples) ? 4 : 0;

    int patternLength = trackerPattern.patternLength;
    if (patternLength < 1 || patternLength > Sequencer::MAXSTEP + 1)
    {
        return;
    }
    int firstStep = patternPosition - 7;
    bool revolver = mtConfig.general.patternArrangement == PatternArrangement::kVerticalRevolver;

    for (uint8_t column = 0; column < strTrackerPattern::kStripColumns; column++)
    {
        strTrackerPattern::strStripRow* out = trackerPattern.strip[column];
        for (uint8_t r = 0; r < strTrackerPattern::kStripRows; r++)
        {
            out[r] = strTrackerPattern::strStripRow{};
        }

        int trackIdx = trackerPattern.firstVisibleTrack + column;
        if (trackIdx >= kSampleTracks || !isSampleTrack(trackIdx))
        {
            continue; // MIDI tracks have no audio to show
        }

        const Sequencer::strPattern::strTrack& track = seq->track[trackIdx];
        for (int s = 0; s < patternLength; s++)
        {
            const Sequencer::strPattern::strTrack::strStep& step = track.step[s];
            if (step.note >= 0 && step.instrument < kSampleInstrumentsCount)
            {
                s_steps[s] = StepKind::kNote;
            }
            else if (step.note == Sequencer::STEP_NOTE_OFF_DEFAULT ||
                     step.note == Sequencer::STEP_NOTE_OFF_CUT ||
                     step.note == Sequencer::STEP_NOTE_OFF_FADE)
            {
                s_steps[s] = StepKind::kOff;
            }
            else
            {
                // Notes for MIDI or synth instruments don't use the track's sample voice, so a
                // sample already playing carries on under them.
                s_steps[s] = StepKind::kEmpty;
            }
        }

        sampleStrip::PlaybackSource source;
        source.get = playbackOfStep;
        source.context = const_cast<Sequencer::strPattern::strTrack*>(&track);

        sampleStrip::computeRows(s_steps,
                                 source,
                                 patternLength,
                                 firstStep,
                                 strTrackerPattern::kStripRows,
                                 options,
                                 s_rows);

        for (uint8_t r = 0; r < strTrackerPattern::kStripRows; r++)
        {
            int stepIdx = firstStep + r;
            if (!revolver && (stepIdx < 0 || stepIdx >= patternLength))
            {
                continue; // rows past the pattern's ends are blank in this arrangement
            }
            uint8_t flags = s_rows[r].noteStart ? strTrackerPattern::kStripFlagNoteStart : 0;
            for (uint8_t q = 0; q < sampleStrip::kQuartersPerRow; q++)
            {
                out[r].level[q] = s_rows[r].level[q];
                if (s_rows[r].loopWrap[q])
                {
                    flags |= (uint8_t)(1 << (strTrackerPattern::kStripFlagWrapShift + q));
                }
            }
            out[r].flags = flags;
        }
    }
}
