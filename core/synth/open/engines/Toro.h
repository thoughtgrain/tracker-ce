// TORO as an open engine: the same sound as the SDK TORO (core/synth/engines/ToroSynthVoice.cpp), on the
// open engine interface, so it can run on all three Trackers.
//
// The DSP is ToroCore.h, shared with the SDK build. Toro.cpp adds what ToroSynthVoice adds there (ADSR,
// glide, pitch), in the same order and with the same arithmetic; tests/toro/open_parity_test.cpp checks the
// two render identical samples.
//
// Copyright (c) 2026 briannhinton. MIT.
#pragma once

#include "open/OpenSynthEngine.h"

namespace opensynth {
namespace toro_engine {

// The same indices, order and meaning as ToroSynthVoice's ToroParamId, so a TORO patch means the same
// thing to both builds. Serialized in patch files: only ever append, never reorder.
enum Param : int {
    kSpread = 0,
    kShape,
    kNoise,
    kFold,
    kDrive,
    kAphx,
    kCutoff,
    kResonance,
    kAttack,
    kDecay,
    kSustain,
    kRelease,
    kGlideMode,
    kGlideTime,
    kVolume,
    kParamCount  // 15
};

enum GlideMode : int { kAlways = 0, kOverlap = 1, kLegato = 2 };

}  // namespace toro_engine

// The engine singleton (registered for kOpenEngineToro in OpenSynthRegistry.cpp).
ISynthEngine* toroEngine();

}  // namespace opensynth
