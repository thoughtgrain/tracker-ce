// The harness checking itself: if these fail, no other result in the suite means anything.
#include "harness.h"
#include "testing.h"

using seqtest::Event;

TEST("a note is sent on the step it was written on")
{
    seqtest::reset();
    seqtest::setNote(0, 0, 60, 0);
    seqtest::setNote(0, 4, 62, 0);
    seqtest::setNote(3, 9, 48, 5);

    sequencer.playPattern();
    seqtest::runSteps(15);

    const auto notes = seqtest::eventsOfKind(Event::kSampleNoteOn);
    CHECK_EQ(notes.size(), 3);
    if (notes.size() != 3)
        return;

    CHECK_EQ(notes[0].step, 0);
    CHECK_EQ(notes[0].track, 0);
    CHECK_EQ(notes[0].b, 60);

    CHECK_EQ(notes[1].step, 4);
    CHECK_EQ(notes[1].b, 62);

    CHECK_EQ(notes[2].step, 9);
    CHECK_EQ(notes[2].track, 3);
    CHECK_EQ(notes[2].a, 5); // instrument
    CHECK_EQ(notes[2].b, 48);
}

TEST("the pattern loops, and a looped note is sent again")
{
    seqtest::reset();
    seqtest::setPatternLength(4);
    seqtest::setNote(0, 1, 60, 0);

    sequencer.playPattern();
    seqtest::runSteps(12); // three passes of a 4-step pattern

    const auto notes = seqtest::eventsOfKind(Event::kSampleNoteOn);
    CHECK_EQ(notes.size(), 3);
    for (const Event& note : notes)
        CHECK_EQ(note.step, 1);
}

TEST("internal clock sends 6 MIDI clocks per step (24 per quarter note)")
{
    seqtest::reset();
    sequencer.playPattern();
    seqtest::runSteps(8);

    CHECK_EQ(seqtest::eventsOfKind(Event::kMidiStart).size(), 1);

    int clocksInFirstEightSteps = 0;
    for (const Event& clock : seqtest::eventsOfKind(Event::kMidiClock))
    {
        if (clock.step < 8)
            clocksInFirstEightSteps++;
    }
    CHECK_EQ(clocksInFirstEightSteps, 8 * 6);
}

TEST("a step lasts 125 ms at 120 BPM, within one audio block")
{
    seqtest::reset();
    seqtest::setNote(0, 0, 60, 0);
    seqtest::setNote(0, 8, 60, 0);

    sequencer.playPattern();
    seqtest::runSteps(9);

    const auto notes = seqtest::eventsOfKind(Event::kSampleNoteOn);
    CHECK_EQ(notes.size(), 2);
    if (notes.size() != 2)
        return;

    // 8 steps = 1 s = 44100 / 128 = 344.5 blocks.
    const int blocks = static_cast<int>(notes[1].block - notes[0].block);
    CHECK(blocks == 344 || blocks == 345);
}

TEST("MIDI instruments go to MIDI out, not to a sample voice")
{
    seqtest::reset();
    seqtest::setNote(0, 0, 60, seqtest::firstMidiInstrument());

    sequencer.playPattern();
    seqtest::runSteps(2);

    CHECK_EQ(seqtest::eventsOfKind(Event::kSampleNoteOn).size(), 0);
    const auto notes = seqtest::eventsOfKind(Event::kMidiNoteOn);
    CHECK_EQ(notes.size(), 1);
    if (notes.size() == 1)
    {
        CHECK_EQ(notes[0].a, 60);
        CHECK_EQ(notes[0].d, 0); // not a synth
    }
}

TEST("stop sends MIDI stop and leaves interrupts unmasked")
{
    seqtest::reset();
    seqtest::setNote(0, 0, 60, 0);

    sequencer.playPattern();
    seqtest::runSteps(3);
    sequencer.stop();
    seqtest::runBlocks(10);

    CHECK_EQ(seqtest::eventsOfKind(Event::kMidiStop).size(), 1);
    CHECK(sequencer.isStop());
    CHECK_EQ(seqtest::interruptMaskErrors(), 0);
}

TEST("reset() gives every test the same starting point")
{
    seqtest::reset(7);
    seqtest::setNote(0, 0, 60, 0);
    sequencer.playPattern();
    seqtest::runSteps(20);
    const uint64_t first = seqtest::hashEvents();

    seqtest::reset(7);
    seqtest::setNote(0, 0, 60, 0);
    sequencer.playPattern();
    seqtest::runSteps(20);

    CHECK(seqtest::hashEvents() == first);
}
