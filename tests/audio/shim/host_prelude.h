#pragma once

// Force-included (-include) ahead of every translation unit in the audio host tests.
//
// The firmware's audio classes pull in two headers that can't build on a desktop compiler, and both are included
// with quotes relative to the source file, so a shadowing -I path can't replace them. Instead, this file defines
// their include guards first, so the real headers become empty, and provides what the code under test needs:
//
//   dspinst.h   ARM DSP intrinsics written as inline asm, with no portable fallback. Below are exact C++
//               equivalents of the ones the tested code calls, from the ARMv7E-M instruction definitions.
//   mtStructs.h The whole project struct. On a 64-bit host its size static_asserts fail. The limiter only reads
//               five settings and one enum from it, so a minimal stand-in is enough.
//
// Add an intrinsic here only when a newly tested file needs it, and write it from the instruction definition,
// never from "what the code seems to expect".

#include <cstdint>

// ---- dspinst.h ------------------------------------------------------------------------------------------
#define dspinst_h_

// SSAT Rd, #bits, Rn, ASR #rshift: arithmetic shift right, then saturate to a signed `bits`-bit range.
static inline int32_t signed_saturate_rshift(int32_t val, int bits, int rshift)
{
    const int32_t v = val >> rshift; // arithmetic shift (defined for negative values since C++20)
    const int32_t max = (int32_t(1) << (bits - 1)) - 1;
    const int32_t min = -(int32_t(1) << (bits - 1));
    return v > max ? max : (v < min ? min : v);
}

// SMMULR Rd, Rn, Rm: the most significant 32 bits of the signed 64-bit product, rounded (+0x80000000).
static inline int32_t multiply_32x32_rshift32_rounded(int32_t a, int32_t b)
{
    return static_cast<int32_t>((static_cast<int64_t>(a) * b + 0x80000000LL) >> 32);
}

// ---- mtStructs.h ----------------------------------------------------------------------------------------
#define MT_STRUCTS_H

const int16_t MAX_SIGNED_16BIT = 32767;

enum limiterMode
{
    limiterModeSubtle,
    limiterModeHard
};

struct HostProjectValues
{
    uint16_t limiterAttack = 100;
    float limiterRelease = 0.512f;
    uint16_t limiterThreshold = 16384;
    uint8_t limiterMode = limiterModeSubtle;
    uint8_t limiterSidechain = 0;
};

struct HostProject
{
    HostProjectValues values;
};

// Defined once in stream_stub.cpp; tests set fields before calling begin() / setters.
extern HostProject mtProject;
