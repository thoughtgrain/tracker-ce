// Open TORO vs SDK TORO: the open engine (core/synth/open/engines/Toro.cpp, for all three Trackers) must
// render exactly the samples the SDK voice (core/synth/engines/ToroSynthVoice.cpp, Tracker+ and Mini)
// renders, for the same notes and parameters, and end on the same block.
//
// How the two are driven: the SDK voice gets MIDI note events, the way PolySynth drives it; the open engine
// gets noteOn / noteOff with the flags an open host sets (a note while one is held reuses the gated voice:
// reused + overlap). The open engine gets the host gain equal to Volume, since by the open contract the
// host applies Volume.
//
// Not compared, by design:
//  - Restarting a voice from silence with Noise above 0: the open engine seeds its noise from a per-slot
//    counter (it has no voice index), so the noise differs. Those cases run with Noise at 0.
//  - Pitch bend: the open host ramps it across a block, the SDK holds one value per block.
//
// Copyright (c) 2026 briannhinton. MIT.

#include "harness.h"
#include "sdk_fixture.h"

#include "open/OpenSynthEngine.h"
#include "open/OpenSynthParams.h"
#include "open/engines/Toro.h"

#include <cstring>
#include <random>

namespace
{

struct Step
{
    int block;
    bool on;
    int note;
};

struct Case
{
    const char* name;
    std::map<int, float> params;
    std::vector<Step> steps;
    int maxBlocks = 4000; // ~11.6 s, past the longest release (8 s)
};

// One case through both builds; returns false (and reports) at the first sample that differs.
bool same(const Case& c)
{
    // SDK voice, index 0 (its noise owner is 1, the same as the open engine's first start in a slot).
    Engine sdkEngine;
    auto sdk = sdkEngine.voice(0, c.params);

    // Open engine.
    auto* eng = opensynth::openSynthEngine(opensynth::kOpenEngineToro);
    int n = 0;
    const opensynth::ParamInfo* t = opensynth::paramTable(opensynth::kOpenEngineToro, &n);
    opensynth::SynthParams p;
    p.engine = opensynth::kOpenEngineToro;
    p.count = n;
    for (int i = 0; i < n; i++)
        p.end[i] = t[i].def;
    for (const auto& [i, v] : c.params)
        p.end[i] = v;
    std::memcpy(p.start, p.end, sizeof(p.end));
    opensynth::SynthSlotScratch slot;
    eng->slotReset(slot);
    opensynth::SynthVoice v;
    std::memset(v.state, 0, sizeof(v.state));
    v.slotScratch = &slot;
    bool openActive = false;
    bool started = false;
    // Held notes by pitch, like the SDK's note stack: replaying a held note doesn't add a second one.
    std::vector<int> held;
    const float gain = p.end[opensynth::toro_engine::kVolume];

    std::array<float, kBlock> l, r, ol, orr;
    for (int b = 0; b < c.maxBlocks; b++)
    {
        for (const auto& s : c.steps)
        {
            if (s.block != b)
                continue;
            if (s.on)
            {
                noteOn(*sdk, s.note);
                opensynth::SynthNote note;
                note.pitch = static_cast<float>(s.note);
                note.note = static_cast<uint8_t>(s.note);
                note.reused = started;      // one voice record for the whole case
                note.overlap = !held.empty(); // a note while another is held
                if (!started)
                    std::memset(v.state, 0, sizeof(v.state));
                v.gate = true;
                eng->noteOn(v, p, note);
                openActive = true;
                started = true;
                std::erase(held, s.note);
                held.push_back(s.note);
            }
            else
            {
                noteOff(*sdk, s.note);
                std::erase(held, s.note);
                if (held.empty())
                {
                    v.gate = false;
                    eng->noteOff(v, p);
                }
            }
        }

        const bool sdkActive = sdk->isActive();
        if (!sdkActive && !openActive && b > c.steps.back().block)
            return true;
        if (sdkActive != openActive)
        {
            CHECK(false, "%s: block %d: SDK voice %s, open voice %s", c.name, b, sdkActive ? "active" : "ended",
                  openActive ? "active" : "ended");
            return false;
        }
        if (!sdkActive)
            continue;

        l.fill(0.0f);
        r.fill(0.0f);
        AudioBufferStereoRef out{ AudioBufferRef(l), AudioBufferRef(r) };
        sdk->renderVoiceAudio(kCtx, std::nullopt, out, {});

        ol.fill(0.0f);
        orr.fill(0.0f);
        openActive = eng->render(v, p, gain, 0.0f, ol.data(), orr.data());

        for (int i = 0; i < kBlock; i++)
        {
            if (l[i] != ol[i] || ol[i] != orr[i])
            {
                CHECK(false, "%s: block %d sample %d: SDK %.9g, open %.9g / %.9g", c.name, b, i, l[i], ol[i],
                      orr[i]);
                return false;
            }
        }
    }
    CHECK(false, "%s: still sounding after %d blocks", c.name, c.maxBlocks);
    return false;
}

std::vector<Step> oneNote(int note, int offBlock) { return { { 0, true, note }, { offBlock, false, note } }; }

} // namespace

int main()
{
    std::vector<Case> cases;
    cases.push_back({ "default patch", {}, oneNote(48, 40) });

    // Every parameter at its range ends and the middle.
    int n = 0;
    const opensynth::ParamInfo* t = opensynth::paramTable(opensynth::kOpenEngineToro, &n);
    CHECK(t && n == kCount, "the open table has the SDK voice's %d parameters", static_cast<int>(kCount));
    for (int i = 0; t && i < n; i++)
    {
        for (float v : { t[i].min, 0.5f * (t[i].min + t[i].max), t[i].max })
        {
            Case c{ t[i].name, { { i, v } }, oneNote(52, 30) };
            if (i == kNoise)
                c.params[kAphx] = 0.3f;
            cases.push_back(c);
        }
    }

    // Glide: legato step (B while A is held), the release of the lower note, then a note from the release.
    for (float mode : { 0.0f, 1.0f, 2.0f })
    {
        Case c{ "glide", { { kGlideMode, mode }, { kGlideTime, 0.15f }, { kRelease, 0.4f } }, {} };
        c.steps = { { 0, true, 40 },   { 20, true, 52 },  { 30, false, 40 },
                    { 45, false, 52 }, { 50, true, 47 }, { 70, false, 47 } };
        cases.push_back(c);
    }

    // Envelope edges: no attack, decay to silence, a long release, a note restarted after the voice ended.
    cases.push_back({ "instant attack", { { kAttack, 0.0f } }, oneNote(60, 10) });
    cases.push_back({ "decay to silence", { { kSustain, 0.0f }, { kDecay, 0.2f } }, oneNote(55, 200) });
    cases.push_back({ "long release", { { kRelease, 6.0f } }, oneNote(36, 20) });
    {
        Case c{ "restart after end", { { kRelease, 0.05f } }, {} };
        c.steps = { { 0, true, 45 }, { 10, false, 45 }, { 60, true, 57 }, { 80, false, 57 } };
        cases.push_back(c);
    }
    cases.push_back({ "volume", { { kVolume, 0.7f } }, oneNote(50, 30) });

    // Seeded random patches (Noise only on the first start, see the header).
    std::mt19937 rng(20261104);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int k = 0; k < 40; k++)
    {
        Case c{ "random", {}, {} };
        for (int i = 0; t && i < n; i++)
            c.params[i] = t[i].min + u(rng) * (t[i].max - t[i].min);
        c.params[kAttack] = std::min(c.params[kAttack], 0.5f);
        c.params[kRelease] = std::min(c.params[kRelease], 2.0f);
        const int a = 30 + static_cast<int>(u(rng) * 40.0f), b2 = 30 + static_cast<int>(u(rng) * 40.0f);
        c.steps = { { 0, true, a }, { 12, true, b2 }, { 25, false, a }, { 40, false, b2 } };
        cases.push_back(c);
    }

    int matched = 0;
    for (const auto& c : cases)
        matched += same(c) ? 1 : 0;
    CHECK(matched == static_cast<int>(cases.size()), "%d of %zu cases render identically", matched, cases.size());
    std::printf("open parity: %d of %zu cases identical\n", matched, cases.size());
    return toro_test::finish("toro open parity");
}
