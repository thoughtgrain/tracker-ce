// Patch editor layout of the open synth engines: which parameters go in which section of the editor's grid,
// and the icon each section gets.
//
// This is data, with no UI code, so every device compiles it. The Tracker+ / Mini patch editor
// (core/interface/instrumentEditor/SynthPatchEditor.cpp) picks a section's icon from words in its name;
// sectionIcon() applies the same rules, so open engines can be shown there and, later, on an editor for
// the Original Tracker that reads this layout.
//
// Copyright (c) 2026 briannhinton. MIT.
#pragma once

#include <cstdint>

namespace opensynth {

// The editor's section icons. kNone = a section whose name matches no rule (the editor shows no icon).
enum class SectionIcon : uint8_t {
    kNone = 0,
    kEnvelope,
    kFilter,
    kVoice,  // the gear
    kLfo,
    kModulation,
    kOscillator,
    kShaper,
    kKick,
    kTom,
    kSnare,
    kHihat,
    kCymbal,
    kPercussion,
    kMixer,
    kFX,
};

struct Section {
    const char* name;      // full name; its words pick the icon
    const uint8_t* params; // parameter indices in display order
    uint8_t count;
};

// The editor's grid has 16 cells, shared with the macro sections it adds; the SDK engines use at most 6
// parameter sections, and so do these.
constexpr int kMaxSections = 6;

// The layout for an engine id (OpenEngineId), or nullptr. *count gets the number of sections.
const Section* sectionTable(int32_t engine, int* count);

// The icon the patch editor gives a section with this name. Same words, same order (first match wins),
// as SynthPatchEditor.cpp, so "Filter Envelope" gets the envelope icon, as it does there.
SectionIcon sectionIcon(const char* name);

}  // namespace opensynth
