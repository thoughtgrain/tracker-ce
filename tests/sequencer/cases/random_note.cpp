// Random Note step FX ("n"): the note on the step is replaced by a random note within
// +/- value semitones of it, and when the project has a scale, by one that's in the scale.
//
// Backstage bug 16101: with a scale set, notes sometimes land far outside the range.
// https://backstage.polyend.com/t/16101
#include "harness.h"
#include "testing.h"

#include "scales.h"

using seqtest::Event;

namespace
{

constexpr int kRandomNote = Sequencer::strFxConsts::FX_TYPE_RANDOM_NOTE;

constexpr int kChromatic = 0;
constexpr int kMajor = 2;
constexpr int kPentatonicMinor = 17;

struct Result
{
    int count = 0;
    int lowest = 127;
    int highest = 0;
    int outOfScale = 0;
    int seen[128] = {};
};

// Plays one step holding `baseNote` + Random Note `range`, `passes` times, on a MIDI
// instrument (the thread's case), and collects the notes that were sent.
Result playRandomNote(int baseNote, int range, int scale, int root, int passes, uint32_t seed = 1)
{
    seqtest::reset(seed);
    mtProject.values.padBoardScale = static_cast<uint8_t>(scale);
    mtProject.values.padBoardRootNote = static_cast<uint8_t>(root);

    seqtest::setPatternLength(4);
    seqtest::setNote(0, 0, baseNote, seqtest::firstMidiInstrument());
    seqtest::setFx(0, 0, 0, kRandomNote, range);

    sequencer.playPattern();
    seqtest::runSteps(4 * passes);

    Result result;
    for (const Event& note : seqtest::eventsOfKind(Event::kMidiNoteOn))
    {
        result.count++;
        result.lowest = std::min(result.lowest, note.a);
        result.highest = std::max(result.highest, note.a);
        result.seen[note.a]++;
        if (!isInScale(static_cast<uint8_t>(note.a), static_cast<uint8_t>(root),
                       static_cast<uint8_t>(scale)))
            result.outOfScale++;
    }
    return result;
}

} // namespace

TEST("random note, no scale: every note is within +/- value of the written note")
{
    const Result result = playRandomNote(65, 8, kChromatic, 24, 2000);

    CHECK_EQ(result.count, 2000);
    CHECK(result.lowest >= 65 - 8);
    CHECK(result.highest <= 65 + 8);
}

TEST("random note, no scale: every note in the range turns up")
{
    const Result result = playRandomNote(65, 8, kChromatic, 24, 2000);

    for (int note = 65 - 8; note <= 65 + 8; note++)
        CHECK(result.seen[note] > 0);
}

TEST("random note with value 0 plays the written note")
{
    const Result result = playRandomNote(65, 0, kChromatic, 24, 50);

    CHECK_EQ(result.count, 50);
    CHECK_EQ(result.lowest, 65);
    CHECK_EQ(result.highest, 65);
}

TEST("random note with a scale: every note is in the scale")
{
    const Result result = playRandomNote(65, 8, kMajor, 24, 2000);

    CHECK_EQ(result.count, 2000);
    CHECK_EQ(result.outOfScale, 0);
}

// The thread's own numbers: F4 with n8 must stay between A3 (-8) and C#5 (+8).
KNOWN_BUG("16101", "random note with a scale: every note is within +/- value of the written note")
{
    const Result major = playRandomNote(65, 8, kMajor, 24, 2000);
    CHECK(major.lowest >= 65 - 8);
    CHECK(major.highest <= 65 + 8);

    // A sparser scale rejects more candidates, so the walk goes further.
    const Result pentatonic = playRandomNote(65, 8, kPentatonicMinor, 24, 2000);
    CHECK(pentatonic.lowest >= 65 - 8);
    CHECK(pentatonic.highest <= 65 + 8);
}

// What the fix has to keep: with no scale the result must not change at all, because the
// first candidate is always accepted. This pins the exact notes for a fixed seed.
TEST("random note, no scale: exact sequence for a fixed seed (golden)")
{
    playRandomNote(65, 8, kChromatic, 24, 500, 12345);
    CHECK_GOLDEN("random_note_chromatic_seed12345", seqtest::hashEvents());
}
