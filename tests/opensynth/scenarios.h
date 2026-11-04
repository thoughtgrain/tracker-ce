// Render scenarios shared by the CE test (core/synth/open) and the golden generator (OpenTracker's own
// engine). Both sides run exactly this code through a traits type, so a matching hash means the CE port
// renders the same samples as OpenTracker for every scenario.
//
// The framework is emulated the way OpenTracker's synth_bank drives an engine: slotReset once, then per
// block setTempo -> prepare -> render each sounding voice; note-on / note-off between blocks. Gain is a
// flat 1.0 (Volume and velocity belong to the framework, not the engine).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace scenarios {

constexpr int kAcidParams = 29;
constexpr int kMaxBlocksAfterOff = 3000;  // ~8.7 s: every scenario's release ends well before this

// FNV-1a over the exact bits of everything an engine produces.
struct Hash {
    uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    }
    void f(float x) { bytes(&x, 4); }
    void u(uint32_t x) { bytes(&x, 4); }
};

struct Event {
    int block;      // the block before which it happens
    int voice;      // 0 or 1
    bool on;
    float pitch;
    bool reused;    // note-on reusing the voice (mono legato path)
    bool overlap;
};

struct Scenario {
    const char* name;
    std::vector<std::pair<int, float>> params;  // overrides of the init patch
    std::vector<Event> events;
    int blocks;               // render this many blocks, then until both voices end
    float bendTo = 0.0f;      // pitch bend ramped linearly over the first `blocks` blocks
    float cutoffOct = 0.0f;
    int rampParam = -1;       // ramp this parameter's value start -> end across every block
    float rampTo = 0.0f;
};

inline uint32_t lcg(uint32_t& s) { return s = s * 1664525u + 1013904223u; }

// Every scenario. Order matters: the goldens are in this order.
inline std::vector<Scenario> all() {
    using P = std::pair<int, float>;
    std::vector<Scenario> out;
    auto note = [](float pitch, int offBlock) {
        return std::vector<Event>{{0, 0, true, pitch, false, false}, {offBlock, 0, false, 0, false, false}};
    };
    out.push_back({"init", {}, note(48, 60), 60});
    for (int type = 0; type <= 2; ++type)
        out.push_back({type == 0 ? "filter svf12" : type == 1 ? "filter svf24" : "filter rd3",
                       {P{11, float(type)}, P{10, 1.2f}, P{9, 1200}, P{14, 0.6f}, P{20, 0.3f}}, note(45, 40), 40});
    out.push_back({"rd3 self-osc", {P{11, 2}, P{10, 1.5f}, P{9, 800}}, note(36, 30), 30});
    for (int sub = 0; sub <= 5; ++sub)
        out.push_back({"sub mode", {P{1, 0.6f}, P{2, 0.8f}, P{5, float(sub)}, P{4, 0.3f}}, note(36, 20), 20});
    out.push_back({"noise pre", {P{3, 0.7f}}, note(60, 20), 20});
    out.push_back({"noise post", {P{3, -0.7f}}, note(60, 20), 20});
    for (int wave = 0; wave <= 5; ++wave)
        for (int retrig = 0; retrig <= 2; ++retrig)
            out.push_back({"lfo", {P{24, float(wave)}, P{25, float(retrig)}, P{23, 7.5f}, P{6, 0.5f}, P{8, 0.4f},
                                   P{13, -0.6f}, P{1, 0.5f}},
                           note(52, 50), 50});
    out.push_back({"pw env", {P{1, 1}, P{0, 0}, P{7, 0.8f}, P{19, 0.05f}, P{20, 0.4f}, P{21, 0.2f}}, note(50, 40), 40});
    out.push_back({"amp adsr", {P{15, 0.2f}, P{16, 0.3f}, P{17, 0.6f}, P{18, 1.2f}}, note(57, 80), 80});
    for (int mode = 0; mode <= 3; ++mode) {
        Scenario s{"glide", {P{26, float(mode)}, P{27, 0.15f}}, {}, 60};
        s.events = {{0, 0, true, 40, false, false},
                    {20, 0, true, 52, true, true},     // legato: still gated
                    {35, 0, false, 0, false, false},
                    {38, 0, true, 47, true, false},    // reused from its release
                    {55, 0, false, 0, false, false}};
        out.push_back(s);
    }
    {
        Scenario s{"two voices", {P{14, -0.4f}}, {}, 50};
        s.events = {{0, 0, true, 48, false, false}, {10, 1, true, 55, false, false},
                    {30, 0, false, 0, false, false}, {40, 1, false, 0, false, false}};
        out.push_back(s);
    }
    {
        Scenario s{"cutoff ramp", {P{11, 1}, P{10, 1.0f}}, note(43, 60), 60};
        s.rampParam = 9;
        s.rampTo = 9000.0f;
        out.push_back(s);
    }
    {
        Scenario s{"bend", {}, note(48, 40), 40};
        s.bendTo = 7.0f;
        out.push_back(s);
    }
    for (float oct : {-2.0f, 2.0f}) {
        Scenario s{"cutoff offset", {P{9, 600}}, note(48, 30), 30};
        s.cutoffOct = oct;
        out.push_back(s);
    }
    // Seeded random patches: every parameter anywhere in a generous range (the framework would clamp;
    // here the engine's own clamps are exercised too).
    uint32_t seed = 12345;
    for (int k = 0; k < 24; ++k) {
        Scenario s{"random", {}, {}, 30};
        for (int i = 0; i < kAcidParams; ++i) {
            if (i == 28) continue;  // Volume: the framework's
            const float u = float(lcg(seed) >> 8) * (1.0f / 16777216.0f);
            s.params.push_back(P{i, u});  // scaled by the caller to the parameter's range
        }
        const float pitch = 24.0f + float(lcg(seed) % 72);
        s.events = note(pitch, 25);
        out.push_back(s);
    }
    return out;
}

// Runs one scenario through an engine and returns its hash. T supplies the engine-side types.
// `defs` / `lo` / `hi` are the parameter defaults and ranges (the same table on both sides).
template <class T>
uint64_t run(const Scenario& sc, const float* defs, const float* lo, const float* hi) {
    using Params = typename T::Params;
    using Voice = typename T::Voice;
    using Slot = typename T::Slot;
    using Note = typename T::Note;
    auto* eng = T::engine();

    Params p;
    p.engine = T::kEngineId;
    p.count = kAcidParams;
    for (int i = 0; i < kAcidParams; ++i) p.end[i] = defs[i];
    const bool random = std::strcmp(sc.name, "random") == 0;
    for (const auto& [i, v] : sc.params) p.end[i] = random ? lo[i] + v * (hi[i] - lo[i]) : v;
    std::memcpy(p.start, p.end, sizeof(p.end));
    p.serial = 1;

    Slot slot;
    eng->slotReset(slot);
    Voice voices[2];
    bool active[2] = {false, false};
    for (auto& v : voices) {
        std::memset(v.state, 0, sizeof(v.state));
        v.slotScratch = &slot;
    }

    Hash h;
    std::vector<float> L(T::kBlock), R(T::kBlock);
    const float rampFrom = sc.rampParam >= 0 ? p.end[sc.rampParam] : 0.0f;
    int block = 0;
    for (;; ++block) {
        for (const auto& e : sc.events) {
            if (e.block != block) continue;
            Voice& v = voices[e.voice];
            if (e.on) {
                Note n;
                n.pitch = e.pitch;
                n.note = uint8_t(e.pitch);
                n.reused = e.reused;
                n.overlap = e.overlap;
                if (!e.reused) std::memset(v.state, 0, sizeof(v.state));
                v.note = n.note;
                v.gate = true;
                eng->noteOn(v, p, n);
                active[e.voice] = true;
            } else if (active[e.voice]) {
                v.gate = false;
                eng->noteOff(v, p);
            }
        }
        if (block >= sc.blocks && !active[0] && !active[1]) break;
        if (block >= sc.blocks + kMaxBlocksAfterOff) {
            h.u(0xDEADu);  // a voice that never ends changes the hash
            break;
        }
        if (sc.rampParam >= 0) {
            const float t0 = float(block) / float(sc.blocks), t1 = float(block + 1) / float(sc.blocks);
            p.start[sc.rampParam] = rampFrom + (sc.rampTo - rampFrom) * (t0 > 1 ? 1 : t0);
            p.end[sc.rampParam] = rampFrom + (sc.rampTo - rampFrom) * (t1 > 1 ? 1 : t1);
            p.moving = true;
            ++p.serial;
        }
        for (auto& v : voices) {
            const float t0 = float(block) / float(sc.blocks), t1 = float(block + 1) / float(sc.blocks);
            v.bendStart = sc.bendTo * (t0 > 1 ? 1 : t0);
            v.bend = sc.bendTo * (t1 > 1 ? 1 : t1);
            v.cutoffOct = sc.cutoffOct;
        }
        eng->setTempo(120.0f);
        eng->prepare(slot, p);
        std::fill(L.begin(), L.end(), 0.0f);
        std::fill(R.begin(), R.end(), 0.0f);
        for (int k = 0; k < 2; ++k) {
            if (!active[k]) continue;
            active[k] = eng->render(voices[k], p, 1.0f, 0.0f, L.data(), R.data());
            h.u(active[k]);
            h.f(voices[k].level);
        }
        for (uint32_t i = 0; i < T::kBlock; ++i) {
            h.f(L[i]);
            h.f(R[i]);
        }
        if (sc.rampParam >= 0) std::memcpy(p.start, p.end, sizeof(p.end));
    }
    h.u(uint32_t(block));
    return h.h;
}

}  // namespace scenarios
