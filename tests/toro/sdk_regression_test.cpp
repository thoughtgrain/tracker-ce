// TORO on Tracker+ and Mini, through the SDK interface: every case must render exactly the samples recorded
// in sdk_goldens.inc.
//
// Why: the goldens were recorded from the original SDK TORO voice (its own DSP wrapper) before TORO switched
// to the open engine hosted through the SDK adapter. The switched build has to reproduce them sample for
// sample, so existing TORO projects sound exactly the same. Regenerate only for an intended change, and say
// so in the PR (make print-sdk-goldens).
//
// It drives the voice only the way PolySynth does: a voice index at creation, parameter changes (also in the
// middle of a note), MIDI note on / off and pitch bend, one 128-sample block per render.
//
// Copyright (c) 2026 briannhinton. MIT.

#include "harness.h"
#include "sdk_fixture.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace
{

// Parameter ranges, mirrored from ToroSynthVoice.cpp (the ids, enum P, come from sdk_fixture.h).
struct Range
{
    float min, max;
};
const Range kRange[kCount] = { { 0, 1 }, { 0, 1 },     { 0, 1 },     { 0, 1 }, { 0, 1 }, { 0, 1 }, { 0, 1 }, { 0, 1 },
                               { 0, 5 }, { 0.01f, 5 }, { 0, 1 },     { 0.005f, 8 }, { 0, 2 }, { 0, 2 }, { 0, 2 } };

struct Event
{
    enum Kind
    {
        kOn,
        kOff,
        kParam,
        kBend
    } kind;
    int block;
    int a;     // note, parameter id or bend value
    float v;   // parameter value
};

struct Case
{
    const char* name;
    int voiceIdx;
    std::map<int, float> params;
    std::vector<Event> events;
};

constexpr int kCap = 4000;

uint64_t run(const Case& c)
{
    Engine e;
    auto v = e.voice(c.voiceIdx, c.params);
    uint64_t h = toro_test::kFnvSeed;
    int last = 0;
    for (const auto& ev : c.events)
        last = std::max(last, ev.block);
    std::array<float, kBlock> l, r;
    int b = 0;
    for (; b < kCap; b++)
    {
        for (const auto& ev : c.events)
        {
            if (ev.block != b)
                continue;
            switch (ev.kind)
            {
                case Event::kOn: noteOn(*v, ev.a); break;
                case Event::kOff: noteOff(*v, ev.a); break;
                case Event::kParam: v->onParamChange(ev.a, ev.v); break;
                case Event::kBend: bend(*v, static_cast<int16_t>(ev.a)); break;
            }
        }
        const bool active = v->isActive();
        h = toro_test::fnv1a(h, active ? 1 : 0);
        if (!active && b > last)
            break;
        l.fill(0.0f);
        r.fill(0.0f);
        AudioBufferStereoRef out{ AudioBufferRef(l), AudioBufferRef(r) };
        v->renderVoiceAudio(kCtx, std::nullopt, out, {});
        for (float x : l)
        {
            // -0.0 and +0.0 hash the same: the open engine contract adds into a zeroed buffer, which turns
            // the old voice's -0.0 (silence times a negative sample) into +0.0. The sign of zero isn't audible.
            if (x == 0.0f)
                x = 0.0f;
            int32_t bits;
            std::memcpy(&bits, &x, 4);
            h = toro_test::fnv1a(h, bits);
        }
    }
    return toro_test::fnv1a(h, b);
}

std::vector<Event> oneNote(int note, int off) { return { { Event::kOn, 0, note, 0 }, { Event::kOff, off, note, 0 } }; }

std::vector<Case> allCases()
{
    std::vector<Case> cases;
    cases.push_back({ "default patch", 0, {}, oneNote(48, 40) });
    for (int i = 0; i < kCount; i++)
        for (float v : { kRange[i].min, 0.5f * (kRange[i].min + kRange[i].max), kRange[i].max })
        {
            Case c{ "param", 0, { { i, v } }, oneNote(52, 30) };
            if (i == kNoise)
                c.params[kAphx] = 0.3f;
            cases.push_back(c);
        }
    for (float mode : { 0.0f, 1.0f, 2.0f })
    {
        Case c{ "glide", 0, { { kGlideMode, mode }, { kGlideTime, 0.15f }, { kRelease, 0.4f } }, {} };
        c.events = { { Event::kOn, 0, 40, 0 },   { Event::kOn, 20, 52, 0 },  { Event::kOff, 30, 40, 0 },
                     { Event::kOff, 45, 52, 0 }, { Event::kOn, 50, 47, 0 }, { Event::kOff, 70, 47, 0 } };
        cases.push_back(c);
        // Release the top note while the lower one is held: the SDK falls back to the held note.
        Case f{ "fallback", 0, { { kGlideMode, mode }, { kGlideTime, 0.1f } }, {} };
        f.events = { { Event::kOn, 0, 45, 0 }, { Event::kOn, 10, 57, 0 }, { Event::kOff, 20, 57, 0 },
                     { Event::kOff, 35, 45, 0 } };
        cases.push_back(f);
    }
    cases.push_back({ "instant attack", 0, { { kAttack, 0.0f } }, oneNote(60, 10) });
    cases.push_back({ "decay to silence", 0, { { kSustain, 0.0f }, { kDecay, 0.2f } }, oneNote(55, 200) });
    cases.push_back({ "long release", 0, { { kRelease, 6.0f } }, oneNote(36, 20) });
    {
        Case c{ "restart after end", 0, { { kRelease, 0.05f }, { kNoise, 0.6f } }, {} };
        c.events = { { Event::kOn, 0, 45, 0 }, { Event::kOff, 10, 45, 0 }, { Event::kOn, 60, 57, 0 },
                     { Event::kOff, 80, 57, 0 } };
        cases.push_back(c);
    }
    // Voice index seeds the noise: every voice has its own.
    for (int idx = 0; idx < 4; idx++)
        cases.push_back({ "voice index", idx, { { kNoise, 0.5f }, { kAphx, 0.3f } }, oneNote(50, 25) });
    // Parameters that change during a note (the Velocity macro moves Volume at every note-on).
    {
        Case c{ "volume change", 0, { { kVolume, 0.4f } }, oneNote(50, 40) };
        c.events.push_back({ Event::kParam, 15, kVolume, 1.3f });
        cases.push_back(c);
        Case k{ "cutoff change", 0, {}, oneNote(43, 40) };
        k.events.push_back({ Event::kParam, 10, kCutoff, 0.9f });
        k.events.push_back({ Event::kParam, 20, kResonance, 0.8f });
        cases.push_back(k);
    }
    // Pitch bend, up and down.
    {
        Case c{ "bend", 0, {}, oneNote(48, 40) };
        c.events.push_back({ Event::kBend, 10, 8191, 0 });
        c.events.push_back({ Event::kBend, 25, -8192, 0 });
        cases.push_back(c);
    }
    std::mt19937 rng(20261104);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int k = 0; k < 40; k++)
    {
        Case c{ "random", k % 4, {}, {} };
        for (int i = 0; i < kCount; i++)
            c.params[i] = kRange[i].min + u(rng) * (kRange[i].max - kRange[i].min);
        c.params[kAttack] = std::min(c.params[kAttack], 0.5f);
        c.params[kRelease] = std::min(c.params[kRelease], 2.0f);
        const int a = 30 + static_cast<int>(u(rng) * 40.0f), b2 = 30 + static_cast<int>(u(rng) * 40.0f);
        c.events = { { Event::kOn, 0, a, 0 }, { Event::kOn, 12, b2, 0 }, { Event::kOff, 25, a, 0 },
                     { Event::kOff, 40, b2, 0 } };
        cases.push_back(c);
    }
    return cases;
}

struct Golden
{
    const char* name;
    unsigned long long hash;
};
#ifndef TORO_PRINT_GOLDENS
const Golden kGoldens[] = {
#include "sdk_goldens.inc"
};
#endif

} // namespace

int main()
{
    const auto cases = allCases();
#ifdef TORO_PRINT_GOLDENS
    std::printf("// Recorded from the SDK TORO voice by `make print-sdk-goldens`. Do not edit.\n");
    for (const auto& c : cases)
        std::printf("{ \"%s\", 0x%016llxull },\n", c.name, static_cast<unsigned long long>(run(c)));
    return 0;
#else
    const size_t n = sizeof(kGoldens) / sizeof(kGoldens[0]);
    CHECK(cases.size() == n, "%zu cases, %zu goldens", cases.size(), n);
    int matched = 0;
    for (size_t k = 0; k < cases.size() && k < n; k++)
    {
        const uint64_t got = run(cases[k]);
        const bool ok = got == kGoldens[k].hash && std::strcmp(cases[k].name, kGoldens[k].name) == 0;
        CHECK(ok, "case %zu (%s): %016llx, golden %016llx", k, cases[k].name, static_cast<unsigned long long>(got),
              kGoldens[k].hash);
        matched += ok ? 1 : 0;
    }
    std::printf("sdk regression: %d of %zu cases match the recorded SDK TORO\n", matched, cases.size());
    return toro_test::finish("toro sdk regression");
#endif
}
