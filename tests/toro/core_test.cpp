// TORO DSP core tests (host build).
//
// 1. Parity with the OP-XY toro. The golden hashes below were produced by rendering the reference
//    (thoughtgrain/labyrinth, opxy/machines/toro/engine.cpp) and this core side by side at 64-sample blocks:
//    0 sample mismatches over 23,683 cases. Any change to ToroCore.h's math shows up here.
// 2. Behaviour at the Tracker's 128-sample blocks: output stays under the declared peak, cutoff and fold
//    audibly brighten, a held note has no DC offset, APHX is the same on the same key and gentle at a quarter.
//
// Copyright (c) 2026 briannhinton. MIT.

#include "ToroCore.h"
#include "harness.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace tc = polysynth::toro;
using toro_test::fnv1a;
using toro_test::kFnvSeed;

namespace
{

// The OP-XY delivers controls as Q31; toro reads them as q31 >> 16.
tc::i32 fromQ31(int32_t q31) { return q31 < 0 ? 0 : q31 >> 16; }

tc::u32 incFor(double hz) { return static_cast<tc::u32>(hz / 44100.0 * 4294967296.0); }

tc::Controls controlsFromQ31(const int32_t* p)
{
    return { fromQ31(p[0]), fromQ31(p[1]), fromQ31(p[2]), fromQ31(p[3]),
             fromQ31(p[4]), fromQ31(p[5]), fromQ31(p[6]), fromQ31(p[7]) };
}

// Golden hashes from the OP-XY reference (see the file comment).
constexpr uint64_t kGoldenMatrix = 0x228f0907b36f145full;
constexpr uint64_t kGoldenFuzz = 0x5111aa5680df00f1ull;

void testParityMatrix()
{
    // every control at 0 / half / full, three pitches, 16 blocks each
    const int32_t levels[3] = { 0, 1073741823, 2147483647 };
    const double notes[3] = { 41.2, 261.6, 1760.0 };
    uint64_t h = kFnvSeed;
    tc::u32 inc[64];
    tc::i32 out[64];
    for (int m = 0; m < 6561; m++)
    {
        for (double hz : notes)
        {
            int32_t p[8];
            int t = m;
            for (int i = 0; i < 8; i++)
            {
                p[i] = levels[t % 3];
                t /= 3;
            }
            for (auto& i : inc)
                i = incFor(hz);
            tc::State s;
            tc::reset(s, 1);
            const tc::Controls c = controlsFromQ31(p);
            for (int b = 0; b < 16; b++)
            {
                tc::render<6>(s, c, inc, out);
                for (tc::i32 y : out)
                    h = fnv1a(h, y);
            }
        }
    }
    CHECK(h == kGoldenMatrix, "parity matrix hash 0x%016llx, expected 0x%016llx", (unsigned long long)h,
          (unsigned long long)kGoldenMatrix);
}

void testParityFuzz()
{
    // random patches, random voice seeds, pitch gliding up to two octaves either way across 12 blocks
    std::mt19937 rng(20261103);
    uint64_t h = kFnvSeed;
    tc::u32 inc[64];
    tc::i32 out[64];
    for (int k = 0; k < 4000; k++)
    {
        int32_t p[8];
        for (auto& v : p)
            v = static_cast<int32_t>(rng() >> 1);
        const double hz = 20.0 * std::pow(2.0, (rng() % 1000) / 1000.0 * 9.5);
        const double hz2 = hz * std::pow(2.0, (static_cast<int>(rng() % 49) - 24) / 12.0);
        const tc::u32 owner = 1 + rng() % 8;
        tc::State s;
        tc::reset(s, owner);
        const tc::Controls c = controlsFromQ31(p);
        for (int b = 0; b < 12; b++)
        {
            for (unsigned n = 0; n < 64; n++)
            {
                const double x = (b * 64 + n) / 768.0;
                inc[n] = incFor(hz + (hz2 - hz) * x);
            }
            tc::render<6>(s, c, inc, out);
            for (tc::i32 y : out)
                h = fnv1a(h, y);
        }
    }
    CHECK(h == kGoldenFuzz, "parity fuzz hash 0x%016llx, expected 0x%016llx", (unsigned long long)h,
          (unsigned long long)kGoldenFuzz);
}

struct Stats
{
    double rms = 0, bright = 0, mean = 0;
    int peak = 0;
};

// Render at the Tracker's block size and measure. bright = RMS of the first difference over RMS: a rough
// spectral-centroid stand-in that rises when highs come in.
Stats play(double hz, const tc::Controls& c, int blocks, tc::u32 owner = 1)
{
    tc::State s;
    tc::reset(s, owner);
    tc::u32 inc[128];
    tc::i32 out[128];
    for (auto& i : inc)
        i = incFor(hz);
    Stats st;
    double sum = 0, dsum = 0, mean = 0, prev = 0;
    long cnt = 0;
    for (int b = 0; b < blocks; b++)
    {
        tc::render<7>(s, c, inc, out);
        if (b < blocks / 4)
            continue; // let the DC blocker and filter settle
        for (tc::i32 y : out)
        {
            const double x = y;
            sum += x * x;
            dsum += (x - prev) * (x - prev);
            mean += x;
            prev = x;
            cnt++;
            const int a = y < 0 ? -y : y;
            if (a > st.peak)
                st.peak = a;
        }
    }
    st.rms = std::sqrt(sum / cnt);
    st.bright = sum > 0 ? std::sqrt(dsum / sum) : 0;
    st.mean = mean / cnt;
    return st;
}

void testPeakAtTrackerBlocks()
{
    const tc::i32 levels[3] = { 0, 16383, 32767 };
    const double notes[4] = { 32.7, 110.0, 880.0, 4186.0 };
    int over = 0, silentOpen = 0, combos = 0;
    for (int m = 0; m < 6561; m++)
    {
        tc::i32 p[8];
        int t = m;
        for (auto& v : p)
        {
            v = levels[t % 3];
            t /= 3;
        }
        const tc::Controls c{ p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7] };
        for (double hz : notes)
        {
            const Stats s = play(hz, c, 24);
            combos++;
            if (s.peak > tc::kOutputPeak)
                over++;
            if (c.cutoff == 32767 && hz < 1000.0 && s.rms < 100.0)
                silentOpen++;
        }
    }
    CHECK(over == 0, "%d of %d combinations went above the declared peak %d", over, combos, tc::kOutputPeak);
    CHECK(silentOpen == 0, "%d combinations were silent with the filter fully open", silentOpen);
}

void testCutoffAndFoldBrighten()
{
    tc::Controls c{ 2000, 0, 4000, 4000, 0, 0, 0, 0 };
    const Stats dark = play(110, c, 64);
    c.cutoff = 32767;
    const Stats open = play(110, c, 64);
    CHECK(open.bright > dark.bright * 1.5, "cutoff: dark %.3f open %.3f", dark.bright, open.bright);

    c.shape = 32767; // triangles fold more audibly than saws
    c.fold = 0;
    const Stats noFold = play(110, c, 64);
    c.fold = 32767;
    const Stats fold = play(110, c, 64);
    CHECK(fold.bright > noFold.bright * 1.2, "fold: off %.3f on %.3f", noFold.bright, fold.bright);
}

void testNoDcOnHeldNote()
{
    // Same bound as the OP-XY sound test (|mean| < 800 on its Q31 / 65536 scale, i.e. 1600 in Q15): the DC
    // blocker sits before the filter and the soft clip, so a small residual offset is by design.
    const tc::Controls c{ 4000, 20000, 32767, 20000, 16000, 0, 3000, 0 };
    for (double hz : { 55.0, 261.6, 1320.0 })
    {
        const Stats s = play(hz, c, 400);
        CHECK(std::fabs(s.mean) < 1600.0, "DC on a held note @%.0f Hz: mean %.1f (rms %.1f)", hz, s.mean, s.rms);
    }
}

void testAphxDeterministic()
{
    // Same key, same samples, every time.
    const tc::Controls c{ 3932, 12124, 21954, 8847, 5898, 0, 9830, 32767 };
    tc::u32 inc[128];
    tc::i32 a[128 * 20], b[128 * 20];
    for (auto& i : inc)
        i = incFor(277.18);
    for (tc::i32* out : { a, b })
    {
        tc::State s;
        tc::reset(s, 1);
        for (int blk = 0; blk < 20; blk++)
            tc::render<7>(s, c, inc, out + blk * 128);
    }
    bool same = true;
    for (int n = 0; n < 128 * 20; n++)
        same = same && a[n] == b[n];
    CHECK(same, "APHX: the same key must render the same samples every time");
}

void testAphxDrift()
{
    // Measure each key's actual detune from how far oscillator 1's phase moved in one block, as the OP-XY
    // test does: a few cents at 25 %, keys thrown well off pitch at 100 %.
    double worstLo = 0, worstHi = 0;
    int moved = 0;
    for (int key = 0; key < 24; key++)
    {
        const double hz = 27.5 * std::pow(2.0, key / 12.0);
        const tc::u32 i = incFor(hz);
        double cents[2];
        const tc::i32 amounts[2] = { 8192, 32767 };
        for (int k = 0; k < 2; k++)
        {
            const tc::Controls c{ 0, 0, 32767, 0, 0, 0, 0, amounts[k] };
            tc::u32 inc[128];
            tc::i32 out[128];
            for (auto& x : inc)
                x = i;
            tc::State s;
            tc::reset(s, 1);
            tc::render<7>(s, c, inc, out);
            cents[k] = 1200.0 * std::log2(static_cast<double>(s.ph1) / (128.0 * i));
        }
        worstLo = std::max(worstLo, std::fabs(cents[0]));
        worstHi = std::max(worstHi, std::fabs(cents[1]));
        moved += std::fabs(cents[1]) > 30.0;
    }
    CHECK(worstLo > 0.5 && worstLo < 25.0, "APHX 25%%: a few cents expected, worst %.1f", worstLo);
    CHECK(worstHi > 60.0 && moved >= 8, "APHX 100%%: keys should be thrown off pitch (worst %.0f cents, %d of 24 moved)",
          worstHi, moved);
}

void testSilentNoteIsQuiet()
{
    // noise and oscillators only feed the folder, so at cutoff 0 and no resonance the output is small
    const tc::Controls c{ 0, 0, 0, 0, 0, 0, 0, 0 };
    const Stats s = play(4186.0, c, 64);
    CHECK(s.rms < 2000.0, "closed filter on a high note should be quiet, rms %.0f", s.rms);
}

} // namespace

int main()
{
    testParityMatrix();
    testParityFuzz();
    testPeakAtTrackerBlocks();
    testCutoffAndFoldBrighten();
    testNoDcOnHeldNote();
    testAphxDeterministic();
    testAphxDrift();
    testSilentNoteIsQuiet();
    return toro_test::finish("toro core");
}
