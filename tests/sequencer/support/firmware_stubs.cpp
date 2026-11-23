// Stand-ins for the parts of the firmware the sequencer calls but this suite doesn't build:
// the audio engine, MIDI out, the file manager and the UI. Each one either records what it
// was asked to do (so a test can check it) or answers with a fixed, documented value.
//
// The list of functions here is exactly the list of symbols the two sequencer sources leave
// undefined. If the firmware grows a new dependency, the link fails and names it; add it here.
#include "harness.h"

#include "Synth.h"
#include "configEditor/configEditor.h"
#include "core/interface.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtMidi.h"
#include "patternEditor/patternEditor.h"
#include "performanceMode/performanceMode.h"
#include "sampleRecorder/sampleRecorder.h"
#include "util_math.h"

#include <cstring>
#include <new>

namespace hostShim
{
int interruptMaskDepth = 0;
int interruptMaskUnderflows = 0;
}

// ---------------------------------------------------------------------------------------------
// Firmware globals.
//
// The plain data structs are real objects. The big UI and engine objects are not constructed:
// their constructors pull in half the firmware. They get zero-filled storage under the
// firmware's own symbol name instead, which is also how they start life on the device (.bss).
// The sequencer only calls the member functions defined below on them, and those don't read
// the object's own state, except to work out which voice `this` is.
// ---------------------------------------------------------------------------------------------
strMtProject mtProject;
strMtConfig mtConfig;
InterfaceGlobals interfaceGlobals;
Sequencer::strPattern fileManagerPerformancePatternBuffer;

#define ZEROED_GLOBAL(type, name, count)                                                           \
    alignas(type) unsigned char name##_storage[sizeof(type) * (count)] __asm__(#name)

ZEROED_GLOBAL(audioEngine, engine, 1);
ZEROED_GLOBAL(playerEngine, instrumentPlayer, audioEngine::kVoices);
ZEROED_GLOBAL(cFileManager, newFileManager, 1);
ZEROED_GLOBAL(cPatternEditor, patternEditor, 1);
ZEROED_GLOBAL(InterfacePopups, mtPopups, 1);
ZEROED_GLOBAL(Interface, mtInterface, 1);
ZEROED_GLOBAL(cSampleRecorder, sampleRecorder, 1);
ZEROED_GLOBAL(cPerformanceMode, performanceMode, 1);
ZEROED_GLOBAL(cSongTimer, songTimer, 1);

namespace seqtest
{

namespace
{
std::vector<Event> g_events;
uint32_t g_blocks = 0;

// Simple song model for the file-manager stand-in: tests can fill the project playlist and
// the stand-in reports the pattern "loaded" straight away.
uint8_t g_loadedPattern = 0;

void record(Event::Kind kind, int track, int a = 0, int b = 0, int c = 0, int d = 0)
{
    const auto* player = sequencer.ptrPlayer;
    g_events.push_back(Event{ kind,
                              static_cast<int8_t>(track),
                              a,
                              b,
                              c,
                              d,
                              g_blocks,
                              player->globalPos,
                              player->uStep });
}

int voiceIndex(const playerEngine* voice)
{
    const auto* first = reinterpret_cast<const playerEngine*>(instrumentPlayer_storage);
    return static_cast<int>(voice - first);
}

const char* kindName(Event::Kind kind)
{
    switch (kind)
    {
        case Event::kSampleNoteOn: return "sampleNoteOn";
        case Event::kSampleNoteOff: return "sampleNoteOff";
        case Event::kSampleFx: return "sampleFx";
        case Event::kSampleVelocity: return "sampleVelocity";
        case Event::kMidiNoteOn: return "midiNoteOn";
        case Event::kMidiNoteOff: return "midiNoteOff";
        case Event::kMidiCC: return "midiCC";
        case Event::kMidiPitchbend: return "midiPitchbend";
        case Event::kMidiProgram: return "midiProgram";
        case Event::kMidiAfterTouch: return "midiAfterTouch";
        case Event::kMidiPolyPressure: return "midiPolyPressure";
        case Event::kMidiClock: return "midiClock";
        case Event::kMidiStart: return "midiStart";
        case Event::kMidiStop: return "midiStop";
        case Event::kSynthFx: return "synthFx";
        case Event::kMetronome: return "metronome";
        case Event::kEndAllFx: return "endAllFx";
        case Event::kPopup: return "popup";
    }
    return "?";
}
} // namespace

std::string Event::describe() const
{
    char text[160];
    std::snprintf(text,
                  sizeof text,
                  "block %u step %u.%02u %-16s track %d  a=%d b=%d c=%d d=%d",
                  block,
                  step,
                  uStep,
                  kindName(kind),
                  track,
                  a,
                  b,
                  c,
                  d);
    return text;
}

const std::vector<Event>& events()
{
    return g_events;
}

void clearEvents()
{
    g_events.clear();
}

std::vector<Event> eventsOfKind(Event::Kind kind)
{
    std::vector<Event> out;
    for (const Event& event : g_events)
    {
        if (event.kind == kind)
            out.push_back(event);
    }
    return out;
}

uint64_t hashEvents()
{
    uint64_t hash = 1469598103934665603ULL;
    auto mix = [&hash](uint64_t value)
    {
        for (int byte = 0; byte < 8; byte++)
        {
            hash ^= (value >> (byte * 8)) & 0xFF;
            hash *= 1099511628211ULL;
        }
    };
    for (const Event& event : g_events)
    {
        mix(event.kind);
        mix(static_cast<uint64_t>(static_cast<int64_t>(event.track)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(event.a)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(event.b)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(event.c)));
        mix(static_cast<uint64_t>(static_cast<int64_t>(event.d)));
        mix(event.block);
        mix(event.step);
        mix(event.uStep);
    }
    return hash;
}

void reset(uint32_t seed)
{
    std::memset(engine_storage, 0, sizeof engine_storage);
    std::memset(instrumentPlayer_storage, 0, sizeof instrumentPlayer_storage);
    std::memset(newFileManager_storage, 0, sizeof newFileManager_storage);
    std::memset(patternEditor_storage, 0, sizeof patternEditor_storage);
    std::memset(mtPopups_storage, 0, sizeof mtPopups_storage);
    std::memset(mtInterface_storage, 0, sizeof mtInterface_storage);
    std::memset(sampleRecorder_storage, 0, sizeof sampleRecorder_storage);
    std::memset(performanceMode_storage, 0, sizeof performanceMode_storage);
    std::memset(songTimer_storage, 0, sizeof songTimer_storage);

    // The data structs carry default member initializers, so rebuild them in place.
    mtProject.~strMtProject();
    new (&mtProject) strMtProject();
    mtConfig.~strMtConfig();
    new (&mtConfig) strMtConfig();
    interfaceGlobals.~InterfaceGlobals();
    new (&interfaceGlobals) InterfaceGlobals();
    std::memset(&fileManagerPerformancePatternBuffer, 0, sizeof fileManagerPerformancePatternBuffer);

    sequencer.~Sequencer();
    new (&sequencer) Sequencer();

    hostShim::interruptMaskDepth = 0;
    hostShim::interruptMaskUnderflows = 0;
    g_events.clear();
    g_blocks = 0;
    g_loadedPattern = 0;

    mtConfig.midi.clkIn = clockIn_Internal;
    mtProject.values.globalTempo = 120.0f;
    mtProject.values.actualPattern = 1;

    utilMathRandom::srandom(seed);

    sequencer.init();
    sequencer.initAfterReadConfig();
    setPatternLength(16);
}

Step& step(int track, int row)
{
    return sequencer.getActualPattern()->track[track].step[row];
}

void setPatternLength(int steps)
{
    for (auto& track : sequencer.getActualPattern()->track)
    {
        track.length = static_cast<uint8_t>(steps - 1); // the firmware counts length from 0
    }
    sequencer.updateTrackLengths(sequencer.getActualPattern());
}

void setNote(int track, int row, int note, int instrument)
{
    step(track, row).note = static_cast<int8_t>(note);
    step(track, row).instrument = static_cast<uint8_t>(instrument);
}

void setFx(int track, int row, int slot, int fxType, int fxValue)
{
    step(track, row).fx[slot].type = static_cast<uint8_t>(fxType);
    step(track, row).fx[slot].value = static_cast<uint8_t>(fxValue);
}

int firstMidiInstrument()
{
    return MIDI_INSTRUMENTS_START_IDX;
}

int firstSynthInstrument()
{
#ifdef FEATURE_SYNTH
    return SYNTH_INSTRUMENTS_START_IDX;
#else
    return -1;
#endif
}

void runBlocks(int blocks)
{
    // One audio interrupt on the device: 128 samples at 44.1 kHz.
    static constexpr float kBlockSeconds = 128.0f / 44100.0f;
    for (int block = 0; block < blocks; block++)
    {
        sequencer.tick(kBlockSeconds);
        g_blocks++;
    }
}

void runSteps(int steps)
{
    // At the slowest tempo (40 BPM) a step is about 129 blocks, so this limit never cuts a
    // healthy run short. It only stops a test whose sequencer never advances.
    const int safetyLimitBlocks = steps * 200 + 200;
    int advanced = 0;
    uint16_t lastPos = sequencer.ptrPlayer->globalPos;

    for (int block = 0; block < safetyLimitBlocks && advanced < steps; block++)
    {
        runBlocks(1);
        const uint16_t pos = sequencer.ptrPlayer->globalPos;
        if (pos != lastPos)
        {
            advanced++;
            lastPos = pos;
        }
    }
}

uint32_t blocksRun()
{
    return g_blocks;
}

int interruptMaskErrors()
{
    return hostShim::interruptMaskDepth + hostShim::interruptMaskUnderflows;
}

} // namespace seqtest

using seqtest::Event;
using seqtest::record;

// ---------------------------------------------------------------------------------------------
// MIDI out (core/source/mtMidi.cpp)
// ---------------------------------------------------------------------------------------------
void midiSetClockSubBlockOffsetUs(uint32_t) {}
void sendMidiStart() { record(Event::kMidiStart, -1); }
void sendMidiStop() { record(Event::kMidiStop, -1); }
void sendMidiClock() { record(Event::kMidiClock, -1); }

void sendMidiNoteOn(uint8_t note, uint8_t velo, uint8_t channel, bool toSynth)
{
    record(Event::kMidiNoteOn, -1, note, velo, channel, toSynth);
}
void sendMidiNoteOff(uint8_t note, uint8_t velo, uint8_t channel, bool toSynth)
{
    record(Event::kMidiNoteOff, -1, note, velo, channel, toSynth);
}
void sendCC(uint8_t index, uint8_t value, uint8_t channel, bool toSynth)
{
    record(Event::kMidiCC, -1, index, value, channel, toSynth);
}
void sendProgramChange(uint8_t value, uint8_t channel, bool toSynth)
{
    record(Event::kMidiProgram, -1, value, 0, channel, toSynth);
}
void sendAfterTouch(uint8_t value, uint8_t channel, bool toSynth)
{
    record(Event::kMidiAfterTouch, -1, value, 0, channel, toSynth);
}
void sendPolyPressure(uint8_t note, uint8_t value, uint8_t channel, bool toSynth)
{
    record(Event::kMidiPolyPressure, -1, note, value, channel, toSynth);
}
void sendPitchbend(int16_t value, int8_t channel, bool toSynth)
{
    record(Event::kMidiPitchbend, -1, value, 0, channel, toSynth);
}

// ---------------------------------------------------------------------------------------------
// Audio engine (core/AudioEngine)
// ---------------------------------------------------------------------------------------------
uint8_t playerEngine::noteOn(uint8_t instrument,
                             int8_t note,
                             int8_t velocity,
                             uint8_t fx1Id,
                             uint8_t fx1Val,
                             uint8_t fx2Id,
                             uint8_t fx2Val)
{
    record(Event::kSampleNoteOn, seqtest::voiceIndex(this), instrument, note, velocity);
    // A note-on carries the step's FX with it on the device; log them the same way a
    // stand-alone FX is logged so tests read one event stream.
    if (fx1Id != 0)
        record(Event::kSampleFx, seqtest::voiceIndex(this), fx1Id, fx1Val, 0);
    if (fx2Id != 0)
        record(Event::kSampleFx, seqtest::voiceIndex(this), fx2Id, fx2Val, 1);
    return 1; // "the voice started"
}
void playerEngine::noteOff(int8_t option)
{
    record(Event::kSampleNoteOff, seqtest::voiceIndex(this), option);
}
void playerEngine::seqFx(uint8_t fx_id, uint8_t fx_val, uint8_t fx_n)
{
    record(Event::kSampleFx, seqtest::voiceIndex(this), fx_id, fx_val, fx_n);
}
void playerEngine::setStepVelocity(int8_t velocity)
{
    record(Event::kSampleVelocity, seqtest::voiceIndex(this), velocity);
}
uint8_t playerEngine::getMostSignificantVolume()
{
    return 100;
}

void audioEngine::endAllFx(bool synthOnly)
{
    record(Event::kEndAllFx, -1, synthOnly);
}
#ifdef FEATURE_SYNTH
bool audioEngine::isSynthFx(uint8_t)
{
    // The real answer depends on the loaded synth patch. No patch is loaded here.
    return false;
}
void audioEngine::seqSynthFx(uint8_t fx_id, uint8_t fx_val, uint8_t fx_n, uint8_t synthIdx)
{
    record(Event::kSynthFx, -1, fx_id, fx_val, fx_n, synthIdx);
}
#endif
void audioEngine::setLastUsedVoice(uint8_t) {}
void audioEngine::makeMetronomeTick(uint8_t type)
{
    record(Event::kMetronome, -1, type);
}

// ---------------------------------------------------------------------------------------------
// File manager (core/FileManager). Nothing touches a disk: a pattern is "loaded" the moment
// it's asked for, and the song is whatever the test put in mtProject.song.
// ---------------------------------------------------------------------------------------------
void cFileManager::setActualPatternStructChanged() {}
void cFileManager::storePatternUndoRevision() {}
void cFileManager::checkVersionAndTranslate(Sequencer::strPattern*) {}
void cFileManager::forgetPreloadedPattern()
{
    seqtest::g_loadedPattern = 0;
}
uint8_t cFileManager::getLoadedPatternIndex()
{
    return seqtest::g_loadedPattern;
}
void cFileManager::loadWorkspacePattern(uint8_t index)
{
    seqtest::g_loadedPattern = index;
}
bool cFileManager::loadWorkspacePatternNow(uint8_t index)
{
    seqtest::g_loadedPattern = index;
    return true;
}
bool cFileManager::saveWorkspacePatternNow(uint8_t)
{
    return true;
}
bool cFileManager::loadPerformanceTrackToBuffer(uint8_t, uint8_t)
{
    return true;
}
bool cFileManager::isPatternLoading()
{
    return false;
}
bool cFileManager::isPatternPreloaded(uint8_t index)
{
    return seqtest::g_loadedPattern == index;
}
uint8_t cFileManager::getSongPattern(uint8_t pos)
{
    return mtProject.song.playlist[pos];
}
uint8_t cFileManager::getNextSongPattern()
{
    return mtProject.song.playlist[mtProject.song.playlistPos + 1];
}
std::optional<int> cFileManager::getPatternPosInSong(uint8_t patternToSearch)
{
    for (int pos = 0; pos < static_cast<int>(std::size(mtProject.song.playlist)); pos++)
    {
        if (mtProject.song.playlist[pos] == patternToSearch)
            return pos;
    }
    return std::nullopt;
}
void cFileManager::setSongPos(uint8_t pos)
{
    mtProject.song.playlistPos = pos;
}
bool cFileManager::switchNextPatternInSong()
{
    mtProject.song.playlistPos++;
    return true;
}

// ---------------------------------------------------------------------------------------------
// UI (core/interface). No screen is active, nothing is selected, popups are only counted.
// ---------------------------------------------------------------------------------------------
bool Interface::isPatternScreenActive()
{
    return false;
}
bool Interface::isPerformanceModeScreenActive()
{
    return false;
}
enumStepPopups InterfacePopups::getStepPopupState()
{
    return stepPopupNone;
}
void InterfacePopups::show(uint8_t config_slot, const char*, uint8_t)
{
    record(Event::kPopup, -1, config_slot);
}

void cPatternEditor::refreshEditStateFromOutsideModule() {}
void cPatternEditor::setActualPattern(int16_t) {}
void cPatternEditor::setPreviousUsedFx(uint8_t) {}
uint8_t cPatternEditor::getPreviousUsedFx()
{
    return 0;
}
void cPatternEditor::setPreviousUsedFxValue(uint8_t) {}
uint8_t cPatternEditor::getPreviousUsedFxValue()
{
    return 0;
}
uint8_t isMultiSelection()
{
    return 0;
}
int16_t getActualTrack()
{
    return 0;
}
uint8_t isEditMode()
{
    return 0;
}

void cSongTimer::start() {}
void cSongTimer::stop() {}

void cbkSetCurrentExporterOnLastStep() {}
void cbkTurnOffPerformanceMode() {}
