// AudioEffectLimiter (core/libs/Audio/effect_limiter.cpp) host tests.
//
// Golden hashes pin the limiter's exact output and per-block average gain across both limiter modes,
// thresholds from silent to full scale, fast and slow attack and release, main-stream and sidechain mode
// (with and without a sidechain signal, which exercises the shared zeroblock), and signals that sit below,
// cross and slam the threshold, including digital silence (the xpeak == 0 path, where the gain calculation
// divides by zero on purpose and relies on the result clamping to 1.0).
//
// Optimization A1 in docs/design/optimization-plan.md must leave every hash unchanged.

#include "effect_limiter.h"
#include "harness.h"
#include "stream_stub.h"

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace audio_test;

namespace
{

// Regenerate with: ./build/limiter_test --print-goldens (only after an intended behaviour change).
const std::map<std::string, uint64_t> kGoldens = {
#include "goldens/limiter.inc"
};

uint64_t golden(const std::string& name)
{
    auto it = kGoldens.find(name);
    return it == kGoldens.end() ? 0 : it->second;
}

struct Config
{
    const char* name;
    uint8_t mode;
    uint16_t threshold; // setThreshold input, 0..32767 (quantized to 101 steps)
    uint16_t attack;
    float release;
    bool sidechain;
};

enum class Signal
{
    kRampedSine,
    kLoudSquare,
    kNoise,
    kSilence,
    kQuietSine,
};

const char* signalName(Signal s)
{
    switch (s)
    {
        case Signal::kRampedSine: return "ramp";
        case Signal::kLoudSquare: return "square";
        case Signal::kNoise: return "noise";
        case Signal::kSilence: return "silence";
        case Signal::kQuietSine: return "quiet";
    }
    return "?";
}

Block make(Signal s, int blk, uint32_t& rng)
{
    switch (s)
    {
        case Signal::kRampedSine: return rampedSine(blk, 48, 110.0f);
        case Signal::kLoudSquare: return square(blk, 220.0f, 32767);
        case Signal::kNoise: return noise(rng, 28000);
        case Signal::kSilence: return constant(0);
        case Signal::kQuietSine: return sine(blk, 440.0f, 0.05f);
    }
    return constant(0);
}

// Render `blocks` blocks and hash the output samples plus each block's average gain (bit pattern).
uint64_t render(const Config& c, Signal main, Signal side, int blocks, int* blocksOut, float* minGain)
{
    resetStreams();
    mtProject.values.limiterMode = c.mode;
    mtProject.values.limiterAttack = c.attack;
    mtProject.values.limiterRelease = c.release;
    mtProject.values.limiterThreshold = c.threshold;
    mtProject.values.limiterSidechain = c.sidechain ? 1 : 0;

    Fresh<AudioEffectLimiter> lim;
    lim->begin();

    uint32_t rngMain = 12345, rngSide = 999;
    uint64_t h = kFnvSeed;
    *blocksOut = 0;
    *minGain = 1.0f;
    for (int b = 0; b < blocks; b++)
    {
        feed(lim.obj, 0, make(main, b, rngMain));
        if (c.sidechain && side != Signal::kSilence)
            feed(lim.obj, 1, make(side, b, rngSide)); // no block on input 1 -> the limiter keys off zeroblock
        lim->update();
        if (auto out = take(lim.obj, 0))
        {
            (*blocksOut)++;
            h = fnv1a(h, out->data(), sizeof(Block));
        }
        if (lim->isAvailableGainToGet())
        {
            const float g = lim->getAverageGainChange();
            *minGain = std::min(*minGain, g);
            h = fnv1a(h, &g, sizeof(g));
        }
        CHECK(blocksInUse() == 0, "%s: leaked audio blocks after block %d", c.name, b);
    }
    return h;
}

void testGoldens()
{
    const std::vector<Config> configs = {
        { "subtle_half_fast", limiterModeSubtle, 16384, 1, 0.05f, false },
        { "subtle_half_default", limiterModeSubtle, 16384, 100, 0.512f, false },
        { "subtle_low_slow", limiterModeSubtle, 3000, 1000, 2.0f, false },
        { "subtle_full", limiterModeSubtle, 32767, 100, 0.512f, false },
        { "subtle_zero", limiterModeSubtle, 0, 100, 0.512f, false },
        { "hard_half_fast", limiterModeHard, 16384, 1, 1.0f, false },
        { "hard_low_slowattack", limiterModeHard, 6000, 1000, 1000.0f, false },
        { "subtle_sidechain", limiterModeSubtle, 12000, 10, 0.2f, true },
        { "hard_sidechain", limiterModeHard, 12000, 5, 20.0f, true },
    };
    const Signal mains[] = { Signal::kRampedSine, Signal::kLoudSquare, Signal::kNoise, Signal::kSilence,
                             Signal::kQuietSine };

    for (const auto& c : configs)
    {
        for (Signal m : mains)
        {
            // In sidechain mode, try both a loud key signal and no key signal at all.
            const std::vector<Signal> sides = c.sidechain ? std::vector<Signal>{ Signal::kLoudSquare, Signal::kSilence }
                                                          : std::vector<Signal>{ Signal::kSilence };
            for (Signal s : sides)
            {
                const std::string name = std::string(c.name) + "/" + signalName(m) +
                                         (c.sidechain ? std::string("/key-") + signalName(s) : "");
                int blocksOut = 0;
                float minGain = 1.0f;
                const uint64_t h = render(c, m, s, 48, &blocksOut, &minGain);
                CHECK(blocksOut == 48, "%s: transmitted %d of 48 blocks", name.c_str(), blocksOut);
                CHECK_GOLDEN(name.c_str(), h, golden(name));
            }
        }
    }
}

void testBehaviour()
{
    int blocksOut = 0;
    float minGain = 1.0f;

    // The threshold knob maps through a log table (limiterThresholdValues): half the knob is -36.7 dBFS and
    // 90 % is -7.3 dBFS. At 90 %, a full-scale square must be limited and a 5 % sine (-26 dBFS) must not.
    const Config half{ "behaviour_90pct", limiterModeSubtle, 29490, 1, 0.05f, false };
    render(half, Signal::kLoudSquare, Signal::kSilence, 48, &blocksOut, &minGain);
    CHECK(minGain < 0.9f, "loud signal should be limited (min average gain %.3f)", minGain);
    render(half, Signal::kQuietSine, Signal::kSilence, 48, &blocksOut, &minGain);
    CHECK(minGain > 0.999f, "quiet signal should pass untouched (min average gain %.4f)", minGain);

    // Digital silence: xpeak stays 0, the gain divide yields inf, and the limiter must still output silence
    // with unity gain rather than NaN or garbage.
    render(half, Signal::kSilence, Signal::kSilence, 8, &blocksOut, &minGain);
    CHECK(minGain == 1.0f, "silence should keep unity gain (got %f)", minGain);

    // A blocked limiter consumes nothing and transmits nothing.
    resetStreams();
    Fresh<AudioEffectLimiter> lim;
    lim->begin();
    lim->blockUpdate();
    feed(lim.obj, 0, constant(1000));
    lim->update();
    CHECK(!take(lim.obj, 0).has_value(), "a blocked limiter must not transmit");
    resetStreams();
}

} // namespace

int main(int argc, char** argv)
{
    parseArgs(argc, argv);
    testGoldens();
    if (!printGoldens)
        testBehaviour();
    return finish("limiter");
}
