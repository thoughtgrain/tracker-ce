// ACID (core/synth/open/engines/Acid.*) on the host.
//
// What this proves:
//  - Regression goldens: every scenario in scenarios.h renders exactly the samples recorded in goldens.inc.
//    Any change to ACID's sound fails here; regenerate (make print-goldens) only for an intended change.
//  - Behaviour: output is finite and every voice ends, for every scenario including random patches; each
//    filter type sounds different; every parameter changes the sound except Volume, which by the engine
//    contract the host applies; the host gain scales the output linearly.
//  - The registry and the parameter table.
//
// Faithfulness to OpenTracker's ACD rests on the source: Acid.cpp is acd.cpp with only names, include
// paths and the fast-RAM marker changed (see the file header). These tests don't run OpenTracker's code.
//
//   make -C tests/opensynth                  build and run (AddressSanitizer + UBSan, -Werror)
//   make -C tests/opensynth print-goldens    print goldens.inc for the current ACID
#include <cmath>
#include <cstdio>
#include <cstring>

#include "open/OpenSynthEngine.h"
#include "open/OpenSynthParams.h"
#include "open/engines/Acid.h"
#include "scenarios.h"

namespace {

struct CeTraits {
    using Params = opensynth::SynthParams;
    using Voice = opensynth::SynthVoice;
    using Slot = opensynth::SynthSlotScratch;
    using Note = opensynth::SynthNote;
    static constexpr int32_t kEngineId = opensynth::kOpenEngineAcid;
    static constexpr uint32_t kBlock = opensynth::kBlock;
    static opensynth::ISynthEngine* engine() { return opensynth::openSynthEngine(opensynth::kOpenEngineAcid); }
};

struct Golden {
    const char* name;
    unsigned long long hash;
};
#ifndef OPENSYNTH_PRINT_GOLDENS
const Golden kGoldens[] = {
#include "goldens.inc"
};
#endif

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Table {
    float defs[scenarios::kAcidParams], lo[scenarios::kAcidParams], hi[scenarios::kAcidParams];
    Table() {
        int n = 0;
        const opensynth::ParamInfo* t = opensynth::paramTable(opensynth::kOpenEngineAcid, &n);
        for (int i = 0; i < n; ++i) defs[i] = t[i].def, lo[i] = t[i].min, hi[i] = t[i].max;
    }
};

scenarios::Result render(const scenarios::Scenario& sc, float gain = 1.0f) {
    static const Table t;
    return scenarios::run<CeTraits>(sc, t.defs, t.lo, t.hi, gain);
}

#ifndef OPENSYNTH_PRINT_GOLDENS
void testGoldens() {
    const auto all = scenarios::all();
    const size_t n = sizeof(kGoldens) / sizeof(kGoldens[0]);
    check(all.size() == n, "scenario count matches goldens.inc");
    for (size_t k = 0; k < all.size() && k < n; ++k) {
        const auto r = render(all[k]);
        if (r.hash != kGoldens[k].hash || std::strcmp(all[k].name, kGoldens[k].name) != 0) {
            std::printf("FAIL: scenario %zu (%s): hash %016llx, golden %016llx\n", k, all[k].name,
                        static_cast<unsigned long long>(r.hash), kGoldens[k].hash);
            ++failures;
        }
    }
    std::printf("goldens: %zu scenarios\n", all.size());
}
#endif

void testEveryScenarioIsFiniteAndEnds() {
    for (const auto& sc : scenarios::all()) {
        const auto r = render(sc);
        if (!r.finite) std::printf("FAIL: %s: non-finite output\n", sc.name), ++failures;
        if (!r.ended) std::printf("FAIL: %s: a voice never ended\n", sc.name), ++failures;
        if (!(r.peak > 0.0f)) std::printf("FAIL: %s: silent\n", sc.name), ++failures;
    }
}

void testFilterTypesDiffer() {
    uint64_t h[3];
    for (int type = 0; type < 3; ++type) {
        scenarios::Scenario s{"filter", {{11, float(type)}, {10, 1.0f}, {9, 900.0f}}, {}, 20};
        s.events = {{0, 0, true, 45, false, false}, {20, 0, false, 0, false, false}};
        h[type] = render(s).hash;
    }
    check(h[0] != h[1] && h[1] != h[2] && h[0] != h[2], "the three filter types sound different");
}

// A patch with every section engaged, so each parameter has something to act on.
scenarios::Scenario richPatch() {
    scenarios::Scenario s{"rich",
                          {{0, 0.5f}, {1, 0.5f}, {2, 0.5f}, {3, 0.2f}, {4, 0.4f}, {6, 0.3f}, {7, 0.3f},
                           {8, 0.1f}, {9, 1500.0f}, {13, 0.3f}, {14, 0.5f}, {15, 0.01f}, {17, 0.5f},
                           {21, 0.4f}, {23, 3.0f}, {25, 2.0f}, {27, 0.1f}},
                          {},
                          40};
    // Two overlapping notes, so glide mode and time matter.
    s.events = {{0, 0, true, 45, false, false}, {15, 0, true, 52, true, true}, {40, 0, false, 0, false, false}};
    return s;
}

void testEveryParameterMatters() {
    static const Table t;
    const uint64_t base = render(richPatch()).hash;
    for (int i = 0; i < scenarios::kAcidParams; ++i) {
        auto s = richPatch();
        float current = t.defs[i];
        for (const auto& [k, v] : s.params)
            if (k == i) current = v;
        // Move to whichever end of the range is further from the current value.
        const float moved = (t.hi[i] - current) > (current - t.lo[i]) ? t.hi[i] : t.lo[i];
        s.params.push_back({i, moved});
        const bool changed = render(s).hash != base;
        char what[96];
        if (i == opensynth::acid::kVolume) {
            std::snprintf(what, sizeof what, "parameter %d (Volume) is left to the host", i);
            check(!changed, what);
        } else {
            std::snprintf(what, sizeof what, "parameter %d changes the sound", i);
            check(changed, what);
        }
    }
}

void testGainIsLinear() {
    const auto s = richPatch();
    const auto full = render(s, 1.0f), half = render(s, 0.5f);
    check(half.peak > 0.0f && std::fabs(half.peak * 2.0f - full.peak) <= 1e-6f * full.peak + 1e-9f,
          "host gain scales the output linearly");
}

void testRegistryAndParams() {
    auto* e = opensynth::openSynthEngine(opensynth::kOpenEngineAcid);
    check(e && e->id() == opensynth::kOpenEngineAcid, "ACID is registered under its id");
    check(e && std::strcmp(e->name(), "ACID") == 0, "ACID's display name");
    check(opensynth::openSynthEngine(opensynth::kOpenEngineNone) == nullptr, "no engine for id 0");
    check(opensynth::openSynthEngine(999) == nullptr, "no engine for an unknown id");

    int n = 0;
    const opensynth::ParamInfo* t = opensynth::paramTable(opensynth::kOpenEngineAcid, &n);
    check(t && n == opensynth::acid::kParamCount, "ACID table covers every acid::Param");
    for (int i = 0; t && i < n; ++i) {
        check(t[i].min <= t[i].def && t[i].def <= t[i].max, "default inside its range");
        check(opensynth::clampParam(t[i], t[i].max + 1.0f) == t[i].def, "out of range takes the default");
        check(opensynth::clampParam(t[i], std::nanf("")) == t[i].def, "NaN takes the default");
        check(opensynth::clampParam(t[i], t[i].min) == t[i].min, "in range is kept");
    }
    int none = 1;
    check(opensynth::paramTable(999, &none) == nullptr && none == 0, "no table for an unknown id");
}

}  // namespace

int main() {
#ifdef OPENSYNTH_PRINT_GOLDENS
    std::printf("// Generated by `make print-goldens` from core/synth/open/engines/Acid.cpp. Do not edit.\n");
    for (const auto& sc : scenarios::all())
        std::printf("{\"%s\", 0x%016llxull},\n", sc.name, static_cast<unsigned long long>(render(sc).hash));
    return 0;
#else
    testGoldens();
    testEveryScenarioIsFiniteAndEnds();
    testFilterTypesDiffer();
    testEveryParameterMatters();
    testGainIsLinear();
    testRegistryAndParams();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
#endif
}
