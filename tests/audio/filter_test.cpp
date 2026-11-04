// AudioFilterStateVariable (core/libs/Audio/filter_variable.cpp) host tests.
//
// Golden hashes pin the filter's exact output for every filter type, cutoffs across the lookup table,
// resonance from minimum to maximum, with and without overdrive, the bypass path, and switching type and
// enable state mid-stream (which crossfades and calls stabilize()). The filter is integer DSP built on two
// ARM intrinsics (SMMULR, SSAT), reproduced exactly in shim/host_prelude.h.
//
// Optimization A2 in docs/design/optimization-plan.md must leave every hash unchanged.

#include "filter_variable.h"
#include "harness.h"
#include "stream_stub.h"

#include <map>
#include <string>
#include <vector>

using namespace audio_test;

namespace
{

const std::map<std::string, uint64_t> kGoldens = {
#include "goldens/filter.inc"
};

uint64_t golden(const std::string& name)
{
    auto it = kGoldens.find(name);
    return it == kGoldens.end() ? 0 : it->second;
}

constexpr uint8_t kLowPass = 0, kHighPass = 1, kBandPass = 2; // AudioFilterStateVariable's private enum order

struct Setup
{
    const char* name;
    uint8_t type;
    float cutoff; // 0..1 across the lookup table
    float q;      // clamped to 0.7..5 by resonance()
    uint8_t overdrive;
    bool enabled;
};

uint64_t run(const Setup& s, int blocks, bool sawtooth)
{
    resetStreams();
    Fresh<AudioFilterStateVariable> f;
    f->setType(s.type);
    f->setCutoff(s.cutoff);
    f->resonance(s.q);
    f->setOverdriveAmount(s.overdrive);
    if (s.enabled)
        f->connect();
    else
        f->disconnect();

    uint32_t rng = 4242;
    uint64_t h = kFnvSeed;
    for (int b = 0; b < blocks; b++)
    {
        feed(f.obj, 0, sawtooth ? square(b, 110.0f, 20000) : noise(rng, 24000));
        f->update();
        auto out = take(f.obj, 0);
        CHECK(out.has_value(), "%s: no output on block %d", s.name, b);
        if (out)
            h = fnv1a(h, out->data(), sizeof(Block));
        CHECK(blocksInUse() == 0, "%s: leaked audio blocks after block %d", s.name, b);
    }
    return h;
}

void testGoldens()
{
    const std::vector<Setup> setups = {
        { "lp_low_q07", kLowPass, 0.15f, 0.7f, 0, true },
        { "lp_mid_q2", kLowPass, 0.5f, 2.0f, 0, true },
        { "lp_high_q5", kLowPass, 0.95f, 5.0f, 0, true },
        { "lp_top", kLowPass, 1.0f, 5.0f, 0, true },
        { "hp_mid_q1", kHighPass, 0.5f, 1.0f, 0, true },
        { "hp_low_q5", kHighPass, 0.1f, 5.0f, 0, true },
        { "bp_mid_q3", kBandPass, 0.6f, 3.0f, 0, true },
        { "bp_bottom", kBandPass, 0.0f, 0.7f, 0, true },
        { "lp_mid_drive", kLowPass, 0.5f, 2.0f, 200, true },
        { "bp_mid_drive", kBandPass, 0.4f, 4.0f, 60, true },
        { "bypass", kLowPass, 0.5f, 2.0f, 0, false },
        { "bypass_drive", kLowPass, 0.5f, 2.0f, 120, false },
    };
    for (const auto& s : setups)
    {
        for (bool saw : { true, false })
        {
            const std::string name = std::string(s.name) + (saw ? "/square" : "/noise");
            CHECK_GOLDEN(name.c_str(), run(s, 32, saw), golden(name));
        }
    }
}

void testSwitching()
{
    // Type and enable changes mid-stream go through stabilize() and the crossfade in update().
    resetStreams();
    Fresh<AudioFilterStateVariable> f;
    f->setCutoff(0.5f);
    f->resonance(3.0f);
    f->connect();
    uint32_t rng = 77;
    uint64_t h = kFnvSeed;
    const uint8_t types[] = { kLowPass, kBandPass, kHighPass, kLowPass };
    for (int b = 0; b < 40; b++)
    {
        if (b % 10 == 5)
            f->setType(types[(b / 10) % 4]);
        if (b == 18)
            f->disconnect();
        if (b == 26)
            f->connect();
        feed(f.obj, 0, noise(rng, 24000));
        f->update();
        if (auto out = take(f.obj, 0))
            h = fnv1a(h, out->data(), sizeof(Block));
    }
    CHECK_GOLDEN("switching", h, golden("switching"));
}

} // namespace

int main(int argc, char** argv)
{
    parseArgs(argc, argv);
    AudioFilterStateVariable::initCutoffLookup();
    testGoldens();
    testSwitching();
    return finish("filter");
}
