// What a sequencer test uses: a fresh firmware state, a way to write steps into the
// pattern, a way to run time forward, and the log of everything the sequencer sent.
#ifndef TESTS_SEQUENCER_SUPPORT_HARNESS_H_
#define TESTS_SEQUENCER_SUPPORT_HARNESS_H_

#include "mtSequencer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace seqtest
{

// Everything the sequencer hands to the rest of the firmware ends up here, in order.
struct Event
{
    enum Kind : uint8_t
    {
        kSampleNoteOn,  // track = voice; a = instrument, b = note, c = volume
        kSampleNoteOff, // track = voice; a = off option
        kSampleFx,      // track = voice; a = fx id, b = value, c = fx slot
        kSampleVelocity,// track = voice; a = velocity
        kMidiNoteOn,    // a = note, b = velocity, c = channel, d = toSynth
        kMidiNoteOff,   // a = note, b = velocity, c = channel, d = toSynth
        kMidiCC,        // a = index, b = value, c = channel, d = toSynth
        kMidiPitchbend, // a = value, c = channel, d = toSynth
        kMidiProgram,   // a = value, c = channel, d = toSynth
        kMidiAfterTouch,// a = value, c = channel, d = toSynth
        kMidiPolyPressure, // a = note, b = value, c = channel, d = toSynth
        kMidiClock,
        kMidiStart,
        kMidiStop,
        kSynthFx,       // a = fx id, b = value, c = fx slot, d = synth index
        kMetronome,     // a = click type
        kEndAllFx,      // a = synthOnly
        kPopup,         // a = config slot
    };

    Kind kind;
    int8_t track;  // -1 when the event has no voice
    int a, b, c, d;

    // Where the sequencer was when it sent the event.
    uint32_t block; // audio blocks run since reset()
    uint16_t step;  // global step position
    uint16_t uStep; // microstep inside the step (1..48)

    std::string describe() const;
};

// The log. Cleared by reset() and clearEvents().
const std::vector<Event>& events();
void clearEvents();
std::vector<Event> eventsOfKind(Event::Kind kind);

// FNV-1a over the fields of every event, for golden comparisons.
uint64_t hashEvents();

// Puts every firmware global the sequencer touches back to a known state, the way a
// power-on would (the objects live in zeroed memory on the device), then calls
// Sequencer::init(). Project defaults: 120 BPM, internal clock, 16-step pattern,
// all tracks unmuted, chromatic scale. `seed` seeds the firmware's own random generator.
void reset(uint32_t seed = 1);

// Pattern editing, straight into the pattern the sequencer plays.
using Step = Sequencer::strPattern::strTrack::strStep;
Step& step(int track, int row);
void setPatternLength(int steps);
void setNote(int track, int row, int note, int instrument);
void setFx(int track, int row, int slot, int fxType, int fxValue);

// Instrument numbering, so tests don't hard-code offsets that differ between builds.
int firstMidiInstrument();
int firstSynthInstrument(); // -1 when the build has no synth instruments

// Time. One block is one audio interrupt on the device: 128 samples at 44.1 kHz.
void runBlocks(int blocks);
// Runs until the global position has advanced by `steps` steps (or a safety limit hits).
void runSteps(int steps);
uint32_t blocksRun();

// Number of times the sequencer left interrupts masked or unmasked them twice.
int interruptMaskErrors();

} // namespace seqtest

#endif
