#ifndef SOURCE_SAMPLESTRIP_H_
#define SOURCE_SAMPLESTRIP_H_

// Sample strips: the thin waveform beside a track in the pattern editor that shows which stretch of
// sample each row plays (docs/design/tape-ux.md §5). This file is the model only: given the steps
// of one track and how each note's sample plays, it works out a level per quarter-row. It has no
// firmware globals so it can be tested on the host (tests/strips). Main loop only; nothing here may
// be called from the audio interrupt.

#include <cstdint>

#include "waveformPeak.h"

namespace sampleStrip
{

constexpr int kQuartersPerRow = 4;

// How a note's sample plays, in frames of the sample. Mirrors the play modes in
// core/libs/Audio/play_memory_*.cpp closely enough to draw a picture, not to reproduce the audio:
// envelopes, glide, swing and step FX are ignored (see docs/design/tape-ux.md §5).
enum class Mode : uint8_t
{
    kSilent,
    kOneShot,      // start -> end, once (also Slice and Beat Slice, with the slice as start/end)
    kLoopForward,  // start -> loop2, then loop1 -> loop2 repeatedly
    kLoopBackward, // start -> loop2, then loop2 -> loop1 backwards, repeatedly
    kPingPong,     // start -> loop2, then back and forth between loop2 and loop1
};

struct Playback
{
    Mode mode = Mode::kSilent;
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t loop1 = 0;
    uint32_t loop2 = 0;
    float framesPerSecond = 0.0f; // playback speed through the sample, always > 0 when audible

    // Where to read levels from: the instrument's existing peak cache (VoiceStreamManager), which
    // covers the whole sample in kWaveformPeakPoints points per channel.
    const WaveformPeakPoint* peaks = nullptr;
    uint32_t totalFrames = 0;
};

struct Position
{
    bool audible = false;
    uint32_t frame = 0;
    uint32_t lap = 0; // how many times the loop has wrapped; a change marks a wrap
};

// Where the sample is after `seconds` of playing.
Position positionAt(const Playback& playback, float seconds);

// Level 0..255 of the peak cache at `frame`.
uint8_t levelAt(const Playback& playback, uint32_t frame);

// One step of a track, as the model needs it.
struct Step
{
    enum class Kind : uint8_t
    {
        kEmpty,
        kNote, // a note starts here; `playback` says how it plays
        kOff,  // note off: whatever was playing stops
    };
    Kind kind = Kind::kEmpty;
    Playback playback;
};

struct Row
{
    uint8_t level[kQuartersPerRow] = {}; // 0 = nothing drawn
    bool noteStart = false;              // a note starts on this row (a line across the strip)
    bool loopWrap[kQuartersPerRow] = {}; // the loop wrapped during this quarter (a dashed line)
};

struct Options
{
    float stepSeconds = 0.125f; // length of one step: 60 / (tempo * 4)
    // A note that sounds for fewer steps than this draws nothing ("Long samples" in Config).
    // 0 draws every note ("All notes").
    uint8_t minSteps = 0;
};

// Fills `out[i]` for the pattern step `firstStep + i`, i = 0..rowCount-1. Steps outside
// 0..patternLength-1 wrap, the way the pattern loops, so a note near the end carries over into the
// rows shown before step 0. `steps` holds patternLength entries.
void computeRows(const Step* steps,
                 int patternLength,
                 int firstStep,
                 int rowCount,
                 const Options& options,
                 Row* out);

} // namespace sampleStrip

#endif /* SOURCE_SAMPLESTRIP_H_ */
