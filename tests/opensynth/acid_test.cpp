// ACID (core/synth/open) against OpenTracker's ACD: every scenario in scenarios.h must render the exact
// samples OpenTracker's engine renders (goldens.inc, from gen_goldens.cpp). Plus the registry and
// parameter-table basics.
//
//   make -C tests/opensynth                 build and run (AddressSanitizer + UBSan, -Werror)
//   make -C tests/opensynth goldens OT_ROOT=<OpenTracker checkout>   regenerate goldens.inc
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
const Golden kGoldens[] = {
#include "goldens.inc"
};

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void testParity() {
    int n = 0;
    const opensynth::ParamInfo* t = opensynth::paramTable(opensynth::kOpenEngineAcid, &n);
    float defs[scenarios::kAcidParams], lo[scenarios::kAcidParams], hi[scenarios::kAcidParams];
    for (int i = 0; i < n; ++i) defs[i] = t[i].def, lo[i] = t[i].min, hi[i] = t[i].max;

    const auto all = scenarios::all();
    check(all.size() == sizeof(kGoldens) / sizeof(kGoldens[0]), "scenario count matches goldens.inc");
    for (size_t k = 0; k < all.size() && k < sizeof(kGoldens) / sizeof(kGoldens[0]); ++k) {
        const unsigned long long got = scenarios::run<CeTraits>(all[k], defs, lo, hi);
        if (got != kGoldens[k].hash || std::strcmp(all[k].name, kGoldens[k].name) != 0) {
            std::printf("FAIL: scenario %zu (%s): hash %016llx, OpenTracker %016llx\n", k, all[k].name, got,
                        kGoldens[k].hash);
            ++failures;
        }
    }
    std::printf("parity: %zu scenarios\n", all.size());
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
    testParity();
    testRegistryAndParams();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
