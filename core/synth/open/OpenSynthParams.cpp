// Parameter tables of the open synth engines (see OpenSynthParams.h for provenance and licence).
#include "open/OpenSynthParams.h"

#include "open/OpenSynthEngine.h"
#include "open/engines/Acid.h"
#include "open/engines/Toro.h"

namespace opensynth {

namespace {

// ACID = OpenTracker's ACD (its spec 21 §8.2). Index order is acid::Param; the static_assert below keeps
// the two in step.
const ParamInfo kAcid[] = {
    {"Saw Mix", 1.0f, 0.0f, 1.0f},           {"Square Mix", 0.0f, 0.0f, 1.0f},
    {"Sub Mix", 0.0f, 0.0f, 1.0f},           {"Noise Mix", 0.0f, -1.0f, 1.0f},
    {"PW", 0.5f, 0.0f, 1.0f},                {"Sub Mode", 0.0f, 0.0f, 5.0f},
    {"PW LFO", 0.0f, -1.0f, 1.0f},           {"PW Env", 0.0f, -1.0f, 1.0f},
    {"Pitch LFO", 0.0f, -3.0f, 3.0f},        {"Cutoff", 400.0f, 20.0f, 20000.0f},
    {"Resonance", 0.5f, 0.0f, 1.5f},         {"Filter Type", 1.0f, 0.0f, 2.0f},
    {"Note Track", 1.0f, -2.0f, 2.0f},       {"Filter LFO", 0.0f, -1.0f, 1.0f},
    {"Filter Env", 0.14f, -1.0f, 1.0f},      {"Amp Attack", 0.0f, 0.0f, 10.0f},
    {"Amp Decay", 0.5f, 0.0f, 10.0f},        {"Amp Sustain", 0.0f, 0.0f, 1.0f},
    {"Amp Release", 0.5f, 0.0f, 10.0f},      {"Mod Attack", 0.0f, 0.0f, 10.0f},
    {"Mod Decay", 0.5f, 0.0f, 10.0f},        {"Mod Sustain", 0.0f, 0.0f, 1.0f},
    {"Mod Release", 0.5f, 0.0f, 10.0f},      {"LFO Rate", 1.0f, 0.1f, 100.0f},
    {"LFO Wave", 1.0f, 0.0f, 5.0f},          {"LFO Retrigger", 2.0f, 0.0f, 2.0f},
    {"Glide Mode", 0.0f, 0.0f, 3.0f},        {"Glide Time", 0.0f, 0.0f, 3.0f},
    {"Volume", 1.0f, 0.0f, 2.0f},
};
static_assert(sizeof(kAcid) / sizeof(kAcid[0]) == acid::kParamCount, "ACID table and acid::Param differ");

// TORO: the same names, defaults and ranges as the SDK voice's metadata (ToroSynthVoice.cpp), so a TORO
// patch means the same thing to both builds.
const ParamInfo kToro[] = {
    {"Spread", 0.06f, 0.0f, 1.0f},     {"Shape", 0.0f, 0.0f, 1.0f},       {"Noise", 0.0f, 0.0f, 1.0f},
    {"Fold", 0.18f, 0.0f, 1.0f},       {"Drive", 0.0f, 0.0f, 1.0f},       {"APHX", 0.0f, 0.0f, 1.0f},
    {"Cutoff", 0.55f, 0.0f, 1.0f},     {"Resonance", 0.24f, 0.0f, 1.0f},  {"Attack", 0.005f, 0.0f, 5.0f},
    {"Decay", 0.6f, 0.01f, 5.0f},      {"Sustain", 0.7f, 0.0f, 1.0f},     {"Release", 0.3f, 0.005f, 8.0f},
    {"Glide Mode", 0.0f, 0.0f, 2.0f},  {"Glide Time", 0.0f, 0.0f, 2.0f},  {"Volume", 1.0f, 0.0f, 2.0f},
};
static_assert(sizeof(kToro) / sizeof(kToro[0]) == toro_engine::kParamCount, "TORO table and toro_engine::Param differ");

template <class T, int N>
constexpr int countOf(const T (&)[N]) { return N; }

}  // namespace

const ParamInfo* paramTable(int32_t engine, int* count) {
    switch (engine) {
        case kOpenEngineAcid: *count = countOf(kAcid); return kAcid;
        case kOpenEngineToro: *count = countOf(kToro); return kToro;
        default: *count = 0; return nullptr;
    }
}

int volumeParam(int32_t engine) {
    switch (engine) {
        case kOpenEngineAcid: return acid::kVolume;
        case kOpenEngineToro: return toro_engine::kVolume;
        default: return -1;
    }
}

float clampParam(const ParamInfo& p, float value) {
    // NaN fails both comparisons, so it takes the default too.
    if (!(value >= p.min && value <= p.max)) return p.def;
    return value;
}

}  // namespace opensynth
