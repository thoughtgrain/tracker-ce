// Patch editor layout of the open synth engines (see OpenSynthLayout.h).
//
// Copyright (c) 2026 briannhinton. MIT.
#include "open/OpenSynthLayout.h"

#include <cstring>

#include "open/OpenSynthEngine.h"
#include "open/engines/Acid.h"
#include "open/engines/Toro.h"

namespace opensynth {

namespace {

template <class T, int N>
constexpr int countOf(const T (&)[N]) { return N; }

#define SECTION(name, list) Section{ name, list, static_cast<uint8_t>(countOf(list)) }

// ACID: the sections of its signal path, named so each gets the matching icon.
namespace acid_layout {
using namespace acid;
constexpr uint8_t kOsc[] = {kSawMix, kSquareMix, kSubMix, kNoiseMix, kPw, kSubMode, kPwLfo, kPwEnv, kPitchLfo};
constexpr uint8_t kFlt[] = {kCutoff, kResonance, kFilterType, kFilterEnv, kNoteTrack, kFilterLfo};
constexpr uint8_t kAmp[] = {kAmpA, kAmpD, kAmpS, kAmpR};
constexpr uint8_t kMod[] = {kModA, kModD, kModS, kModR};
constexpr uint8_t kLfo[] = {kLfoHz, kLfoWave, kLfoRetrig};
constexpr uint8_t kVoi[] = {kGlideMode, kGlideTime, kVolume};
const Section kSections[] = {
    SECTION("Oscillator", kOsc), SECTION("Filter", kFlt),       SECTION("Amp Envelope", kAmp),
    SECTION("Mod Envelope", kMod), SECTION("LFO", kLfo),         SECTION("Voice", kVoi),
};
}  // namespace acid_layout

// TORO: exactly the SDK voice's layout (ToroSynthVoice.cpp), so it looks the same on every device;
// tests/toro/open_parity_test.cpp compares the two.
namespace toro_layout {
using namespace toro_engine;
constexpr uint8_t kOsc[] = {kSpread, kShape, kNoise};
constexpr uint8_t kShp[] = {kFold, kDrive, kAphx};
constexpr uint8_t kFlt[] = {kCutoff, kResonance};
constexpr uint8_t kAmp[] = {kAttack, kDecay, kSustain, kRelease};
constexpr uint8_t kVoi[] = {kGlideMode, kGlideTime, kVolume};
const Section kSections[] = {
    SECTION("Oscillator", kOsc), SECTION("Shaper", kShp), SECTION("Filter", kFlt),
    SECTION("Amplifier", kAmp),  SECTION("Voice", kVoi),
};
}  // namespace toro_layout

#undef SECTION

static_assert(countOf(acid_layout::kSections) <= kMaxSections, "ACID has too many sections for the grid");
static_assert(countOf(toro_layout::kSections) <= kMaxSections, "TORO has too many sections for the grid");

bool has(const char* name, const char* word) { return std::strstr(name, word) != nullptr; }

}  // namespace

const Section* sectionTable(int32_t engine, int* count) {
    switch (engine) {
        case kOpenEngineAcid: *count = countOf(acid_layout::kSections); return acid_layout::kSections;
        case kOpenEngineToro: *count = countOf(toro_layout::kSections); return toro_layout::kSections;
        default: *count = 0; return nullptr;
    }
}

SectionIcon sectionIcon(const char* name) {
    if (!name) return SectionIcon::kNone;
    // The order of SynthPatchEditor.cpp's checks: the envelope words come first.
    if (has(name, "Amp Envelope") || has(name, "Amplifier") || has(name, "Envelope")) return SectionIcon::kEnvelope;
    if (has(name, "Filter")) return SectionIcon::kFilter;
    if (has(name, "Voice")) return SectionIcon::kVoice;
    if (has(name, "LFO")) return SectionIcon::kLfo;
    if (has(name, "Modulation")) return SectionIcon::kModulation;
    if (has(name, "Oscillator")) return SectionIcon::kOscillator;
    if (has(name, "Shaper")) return SectionIcon::kShaper;
    if (has(name, "Kick")) return SectionIcon::kKick;
    if (has(name, "Tom")) return SectionIcon::kTom;
    if (has(name, "Snare")) return SectionIcon::kSnare;
    if (has(name, "Hihat")) return SectionIcon::kHihat;
    if (has(name, "Cymbal")) return SectionIcon::kCymbal;
    if (has(name, "Percussion")) return SectionIcon::kPercussion;
    if (has(name, "Mixer")) return SectionIcon::kMixer;
    if (has(name, "FX")) return SectionIcon::kFX;
    return SectionIcon::kNone;
}

}  // namespace opensynth
