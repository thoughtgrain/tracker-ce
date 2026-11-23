// Force-included (-include) before every firmware source the suite compiles.
// It exists so the firmware files build on a 64-bit desktop without being edited.
// Keep it as small as possible: every line here is a place where the host build
// differs from the device build.
#ifndef TESTS_SEQUENCER_SHIM_HOST_PRELUDE_H_
#define TESTS_SEQUENCER_SHIM_HOST_PRELUDE_H_

// The standard headers go first, so the static_assert override below can't reach them.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <optional>
#include <type_traits>

// 1. Struct size contracts.
// mtStructs.h pins the size of serialized structs (strInstrument is 372 bytes, and so on).
// Those sizes assume 4-byte pointers, so on a 64-bit host the asserts fail even though
// nothing is wrong. I switch static_assert off for that one header only; its include
// guard keeps it off for every later include, and everything else keeps its asserts.
// Consequence: this suite can't say anything about file layout. Byte-identical files
// are a job for the ARM build and for a 32-bit suite.
#define static_assert(...) static_assert(true)
#include "mtStructs.h"
#undef static_assert

// 2. Interrupt masking.
// config.h masks interrupts with ARM instructions (CPSID/CPSIE). The host has no audio
// interrupt to mask, so I count the calls instead. A test can then check that every
// disable has its enable, which is the property that matters on the device.
#include "config.h"
#undef DISABLE_GLOBAL_INTERRUPTS
#undef ENABLE_GLOBAL_INTERRUPTS

namespace hostShim
{
extern int interruptMaskDepth;        // > 0 while "interrupts are masked"
extern int interruptMaskUnderflows;   // enables that had no matching disable
}

#define DISABLE_GLOBAL_INTERRUPTS() (++hostShim::interruptMaskDepth)
#define ENABLE_GLOBAL_INTERRUPTS()                                                                 \
    (hostShim::interruptMaskDepth > 0 ? --hostShim::interruptMaskDepth                             \
                                      : ++hostShim::interruptMaskUnderflows)

#endif
