// Parameter tables of the open synth engines: name, default and stored range of each parameter, in the
// engine's own index order.
//
// Ranges and defaults are ported from OpenTracker (https://github.com/sandlbn/polyend-tracker-re,
// src/core/formats/patch_pb.cpp kAcd, and names from src/core/ui/screen_synth_tables.cpp, at 6c93bbb),
// Copyright the OpenTracker contributors, Apache License 2.0 (core/synth/open/LICENSE-APACHE-2.0).
//
// The framework clamps every value to [min, max] before an engine sees it (the engine contract in
// OpenSynthEngine.h), and a patch that doesn't store a parameter gets `def`.
#pragma once

#include <cstdint>

namespace opensynth {

struct ParamInfo {
    const char* name;
    float def;
    float min;
    float max;
};

// The table for an engine id (OpenEngineId), or nullptr. *count gets its length.
const ParamInfo* paramTable(int32_t engine, int* count);

// A value clamped to parameter i's range; out-of-range or non-finite values take the default, as a
// patch load does.
float clampParam(const ParamInfo& p, float value);

}  // namespace opensynth
