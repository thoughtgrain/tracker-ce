#pragma once

// toro (Spanish, "bull"): the DSP core of the TORO synth engine.
//
// Ported from my OP-XY machine (thoughtgrain/labyrinth, opxy/machines/toro/engine.cpp, MIT), which is itself a
// port of the OP-1 kennel plugin. The integer math is kept exactly as it is there, so TORO on the Tracker sounds
// like toro on the OP-1 and OP-XY. With 64-sample blocks this renders bit-identical output to the OP-XY build;
// tests/toro checks that against golden hashes.
//
// Signal path per voice:
//   osc 1 + osc 2 (saw -> triangle morph) + noise -> wavefolder -> DC blocker -> 2-pole state-variable low-pass
//   -> soft-clip drive -> output
//
// Controls are on the OP-1's knob scale (0..32767):
//   spread  osc 2 ratio 1.0 .. 1.5             shape  saw .. triangle (both oscillators)
//   fold    wavefolder gain 1x .. 8x           noise  noise into the folder (up to 25 % of the mix)
//   cutoff  low-pass, squared response         drive  output soft clip 1x .. 4x
//   res     low-pass resonance                 aphx   key-tracked drift that turns to chaos
//
// APHX gives every key its own fixed, random-looking offsets to the two oscillators' tuning, the cutoff, fold,
// shape and resonance. The offsets come from a hash of the note's position on the keyboard, computed from the
// phase increment and blended between neighbouring keys, so the same key always sounds the same, a glide slides
// through the changes smoothly, and the voice needs no per-note memory.
//
// Real-time rules: integers only, no division in the sample loop, coefficients once per block and ramped across
// it, filter state clamped so it can't run away, and every product kept below 2^31.
//
// Copyright (c) 2026 briannhinton. MIT.

#include <cstdint>

namespace polysynth::toro
{

using i32 = int32_t;
using u32 = uint32_t;
using i64 = int64_t;

static constexpr i32 kKnobMax = 32767;   // control scale, same as the OP-1's knobs
static constexpr i32 kFMin = 300;        // cutoff coefficient range, Q15 (about 64 Hz .. 6.5 kHz)
static constexpr i32 kFMax = 29491;
static constexpr i32 kStateLimit = 60000; // filter state clamp: keeps every product below 2^31
static constexpr i32 kOutputPeak = 24576; // |output| never exceeds this (Q15, 0.75 full scale)

struct Controls
{
    i32 spread = 2000;
    i32 fold = 6000;
    i32 cutoff = 18000;
    i32 res = 8000;
    i32 shape = 0;
    i32 noise = 0;
    i32 drive = 0;
    i32 aphx = 0;
};

struct State
{
    u32 ph1 = 0, ph2 = 0;   // oscillator phases, 2^32 = one cycle
    i32 low = 0, band = 0;  // state-variable filter, Q15
    u32 rng = 0;            // noise
    i32 fLast = 0;          // cutoff coefficient of the last block, 0 = none yet (no sweep on a new note)
    i32 dcX = 0, dcY = 0;   // DC blocker after the folder: an asymmetric wave folds to an offset
};

namespace detail
{
inline i32 clampSym(i32 v, i32 lim) { return v > lim ? lim : (v < -lim ? -lim : v); }
inline i32 clampRange(i32 v, i32 lo, i32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

// knob 0..32767 -> coefficient, squared for feel
inline i32 cutoffCoef(i32 p)
{
    const i32 sq = (p * p) >> 15;
    return kFMin + ((sq * (kFMax - kFMin)) >> 15);
}

// saw -> triangle, Q15
inline i32 osc(u32 ph, i32 shape)
{
    const i32 saw = static_cast<i32>(ph) >> 16;
    const i32 tri = ((saw < 0 ? -saw : saw) << 1) - 32768;
    return saw + (((tri - saw) * (shape >> 1)) >> 14); // |tri - saw| <= 2^16: keeps the product < 2^31
}

// 2^(x / 4096) as Q12, x in Q12 octaves (-4 .. +4): whole octaves by shift, the fraction by a quadratic
// (within 0.3 %). Once per block.
inline i32 exp2q(i32 x)
{
    if (x > 4 * 4096)
        x = 4 * 4096;
    else if (x < -4 * 4096)
        x = -4 * 4096;
    const i32 n = x >> 12, f = x & 4095;
    const i32 m = 4096 + ((f * (2689 + ((f * 1407) >> 12))) >> 12); // 1 + f (0.6565 + 0.3435 f)
    return n >= 0 ? m << n : m >> -n;
}

// key position of a phase increment, Q8 semitones (log2 by the top bit, the mantissa with a parabola correction)
inline i32 keyQ8(u32 inc)
{
    if (inc < 2u)
        return 0;
    i32 msb = 31;
    while (!(inc & 0x80000000u))
    {
        inc <<= 1;
        msb--;
    }
    const i32 man = static_cast<i32>((inc >> 15) & 0xFFFF);                  // Q16 mantissa above 1.0
    const i32 frac = man + ((((man * 22713) >> 16) * (65536 - man)) >> 16); // log2(1 + m), within 0.005 octave
    return static_cast<i32>(((static_cast<i64>(msb) << 16) + frac) * 12 >> 8);
}

// six fixed, signed per-key values (Q15) for key k
inline void keyRand(i32 k, i32* r)
{
    u32 h = static_cast<u32>(k) * 2654435761u ^ 0x5BD1E995u;
    for (int i = 0; i < 6; i++)
    {
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        h *= 0x297A2D39u;
        h ^= h >> 15;
        r[i] = static_cast<i32>(h >> 16) - 32768;
    }
}
} // namespace detail

// Start a note from silence. The two oscillators start a quarter cycle apart so they don't cancel at unison;
// owner seeds the noise so voices don't hiss in lockstep.
inline void reset(State& v, u32 owner)
{
    v = State{};
    v.ph2 = 0x40000000u;
    v.rng = 0x9E3779B9u ^ (owner * 0x85EBCA6Bu);
}

// Render one block of 2^kBlockShift samples. inc holds one phase increment per sample (2^32 = one cycle per
// sample); out receives Q15 samples with |out| <= kOutputPeak.
template<int kBlockShift>
inline void render(State& v, const Controls& c, const u32* inc, i32* out)
{
    using namespace detail;
    constexpr int kFrames = 1 << kBlockShift;

    const i32 drv = clampRange(c.drive, 0, kKnobMax);
    const i32 aphx = clampRange(c.aphx, 0, kKnobMax);
    i32 pf1 = clampRange(c.fold, 0, kKnobMax);
    i32 pf3 = clampRange(c.res, 0, kKnobMax);
    i32 ps5 = clampRange(c.shape, 0, kKnobMax);
    i32 det1 = 0, det2 = 0, fmul = 4096, hold = 1;

    if (aphx)
    {
        // the keyboard position, and this key's six offsets blended with the next key's
        const i32 kq = keyQ8(inc[0]), k = kq >> 8, kf = kq & 255;
        i32 ra[6], rb[6], r[6];
        keyRand(k, ra);
        keyRand(k + 1, rb);
        for (int i = 0; i < 6; i++)
            r[i] = ra[i] + (((rb[i] - ra[i]) * kf) >> 8);
        const i32 a = aphx, a2 = (a * a) >> 15, a3 = (a2 * a) >> 15; // Q15 curves: gentle, then wild
        // tuning: up to 0.15 semitone at a quarter, 3 semitones at the top; each oscillator its own way
        const i32 semis = ((a * 614) >> 15) + ((a3 * 11674) >> 15); // Q12 semitones: 0.15 a + 2.85 a^3
        det1 = exp2q(((r[0] * semis) >> 15) / 12) - 4096;            // Q12 ratio - 1
        det2 = exp2q(((r[1] * semis) >> 15) / 12) - 4096;
        // cutoff: up to half an octave early on, three octaves at the top
        fmul = exp2q((r[2] * (((a * 2048) >> 15) + ((a3 * 10240) >> 15))) >> 15);
        // fold, shape, resonance: pushed by the key, more as APHX rises
        pf1 = clampRange(pf1 + ((r[3] * (a2 >> 1)) >> 15) + ((r[3] < 0 ? -r[3] : r[3]) * a3 >> 16), 0, kKnobMax);
        ps5 = clampRange(ps5 + ((r[4] * a2) >> 15), 0, kKnobMax);
        pf3 = clampRange(pf3 + ((((r[5] < 0 ? -r[5] : r[5]) * a3) >> 15) * 3 >> 2), 0, kKnobMax);
        // the top fifth: each key holds every 1..8th sample, its own rate
        if (a > 26214)
            hold = 1 + static_cast<i32>(((static_cast<u32>(r[5] + 32768) >> 13) * static_cast<u32>(a - 26214)) >> 13);
        if (hold > 8)
            hold = 8;
    }

    const i32 spread = clampRange(c.spread, 0, kKnobMax) >> 1; // ratio 1.0 .. 1.5, Q15 above 1
    const i32 fold = 32768 + pf1 * 7;                           // 1x .. 8x, Q15
    const i32 damp = 32768 - (pf3 * 30000 >> 15);               // 2.0 (none) .. 0.17 (ringing), Q14
    // Chamberlin stability: f^2 + 2 f damp must stay well below 4. The linear cap runs from 0.9 at the lowest
    // damping to 0.739 at the highest, under the exact bound everywhere on that range.
    const i32 fcap = kFMax - (((damp - 2768) * 5764) >> 15);
    i32 f1 = (cutoffCoef(clampRange(c.cutoff, 0, kKnobMax)) * fmul) >> 12;
    if (f1 > fcap)
        f1 = fcap;
    if (f1 < kFMin)
        f1 = kFMin;
    const i32 shape = ps5;
    const i32 noise = clampRange(c.noise, 0, kKnobMax) >> 2; // up to 25 % of the mix
    const i32 drive = 32768 + drv * 3;                        // 1x .. 4x

    const i32 f0 = v.fLast ? v.fLast : f1;
    const i32 fstep = (f1 - f0) >> kBlockShift; // ramp the cutoff across the block
    i32 f = f0;
    u32 ph1 = v.ph1, ph2 = v.ph2, rng = v.rng;
    i32 low = v.low, band = v.band, dcx = v.dcX, dcy = v.dcY, held = 0, cnt = 0;

    for (int n = 0; n < kFrames; n++)
    {
        const u32 i = inc[n];
        const u32 i1 = i + static_cast<u32>(static_cast<i32>(i >> 12) * det1); // APHX detune
        const u32 i2 = i + static_cast<u32>(static_cast<i32>(i >> 12) * det2);
        ph1 += i1;
        ph2 += i2 + (i2 >> 15) * static_cast<u32>(spread); // i * ratio in 32-bit math
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        i32 s = (osc(ph1, shape) + osc(ph2, shape)) >> 1;
        s += ((static_cast<i32>(rng) >> 16) * noise) >> 15;
        // wavefolder: gain, then reflect at +-32767; 8x needs at most 5 reflections
        i32 x = (s * (fold >> 4)) >> 11;
        for (int k = 0; k < 5; k++)
        {
            if (x > 32767)
                x = 65534 - x;
            else if (x < -32767)
                x = -65534 - x;
            else
                break;
        }
        // DC blocker, pole 0.998 (about 15 Hz); |dcy| <= 65535 keeps the product below 2^31
        dcy = clampSym(x - dcx + ((dcy * 32700) >> 15), 65535);
        dcx = x;
        x = clampSym(dcy, 32767);
        // Chamberlin state-variable low-pass
        low = clampSym(low + ((f * band) >> 15), kStateLimit);
        const i32 high = clampSym(x - low - ((damp * band) >> 14), 65535);
        band = clampSym(band + ((f * (high >> 1)) >> 14), kStateLimit);
        f += fstep;
        // drive and soft clip x - x^3/3
        i32 y = clampSym((clampSym(low, 65535) * (drive >> 3)) >> 12, 32767);
        y -= (((((y * y) >> 15) * y) >> 15) * 10923) >> 15;
        y = (y * 3) >> 2; // headroom for several voices and the FX
        if (hold > 1)
        {
            // APHX top end: per-key sample and hold
            if (cnt == 0)
            {
                held = y;
                cnt = hold;
            }
            cnt--;
            y = held;
        }
        out[n] = y;
    }

    v.ph1 = ph1;
    v.ph2 = ph2;
    v.rng = rng;
    v.low = low;
    v.band = band;
    v.fLast = f1;
    v.dcX = dcx;
    v.dcY = dcy;
}

} // namespace polysynth::toro
