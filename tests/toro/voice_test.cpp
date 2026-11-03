// TORO voice tests (host build): the real ToroSynthVoice.cpp, driven the way PolySynth drives it.
//
// - Metadata: every parameter is defined, defaults sit in range, formatters work at min / default / max,
//   MIDI CCs don't collide, the patch editor shows every parameter exactly once in at most 6 sections whose
//   names give the editor an icon, macros point at real parameters.
// - Sound: with an instant envelope the voice renders exactly what ToroCore renders, so the Tracker build
//   keeps toro's sound sample for sample.
// - Behaviour: silence when idle, release ends the voice, glide modes, legato doesn't retrigger,
//   pitch bend, and a randomized stress run for NaNs, runaway output and memory errors.
//
// Copyright (c) 2026 briannhinton. MIT.

#include "ToroSynthVoice.h"
#include "ToroCore.h"
#include "harness.h"

#include "PolySynth/sdk/SynthVoiceSDK.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <vector>

using namespace polysynth;

namespace
{

// A Patch that answers parameter reads from a map and ignores everything else. Only formatters read the patch.
class StubPatch final : public Patch
{
  public:
    std::map<int, paramvalue_t> values;
    Metadata metadata;

    optional_paramvalue_t getParamValue(int paramId) const override
    {
        auto it = values.find(paramId);
        return it == values.end() ? optional_paramvalue_t{} : optional_paramvalue_t{ it->second };
    }
    tiny_optional<float> getParamValueEditMapped(int) const override { return std::nullopt; }
    void setParamValueEditMapped(int, tiny_optional<float>, ParamChangeSource) override {}
    optional_paramvalue_t getParamFinalValue(int paramId) const override { return getParamValue(paramId); }
    void setParamValue(int paramId, optional_paramvalue_t v, ParamChangeSource) override
    {
        if (v)
            values[paramId] = *v;
    }
    void subscribeParamUpdates(std::weak_ptr<ParamChangeListener>) override {}
    void unsubscribeParamUpdates(const ParamChangeListener&) override {}
    void startBatchUpdate() override {}
    void finishBatchUpdate() override {}
    optional_paramvalue_t getMacroValue(int) const override { return std::nullopt; }
    void setMacroValue(int, optional_paramvalue_t, tiny_optional<int>) override {}
    void setMacroModifier(int, int, tiny_optional<int>) override {}
    void setMacroModifierAmount(int, int, float) override {}
    tiny_optional<float> getMacroModifierAmount(int, int) override { return std::nullopt; }
    void clearMacroModifiers() override {}
    void changeMacroModifierAmount(int, int, paramvalue_t) override {}
    void setMacroBipolar(int, bool) override {}
    void setMacroDisplayName(int, std::string) override {}
    const Sample* getSample(int) const override { return nullptr; }
    LoadStatus loadSample(int, const std::string&) override { return LoadStatus{}; }
    const Metadata& getMetadata() const override { return metadata; }
    const Sequencer& getSequencer() const override { return sequencer; }
    void setSequence(const Sequencer::Sequence&, int) override {}
    void setSequencerConfig(const Sequencer::Config&) override {}

  private:
    Sequencer sequencer;
};

// Parameter ids, mirrored from ToroSynthVoice.cpp (they're serialized, so they must never change).
enum P : int
{
    kSpread = 0,
    kShape,
    kNoise,
    kFold,
    kDrive,
    kAphx,
    kCutoff,
    kResonance,
    kAttack,
    kDecay,
    kSustain,
    kRelease,
    kGlideMode,
    kGlideTime,
    kVolume,
    kCount
};

constexpr int kBlock = kAudioBufferSize;
const InstrumentVoice::InstrumentVoiceContext kCtx{ 120.0f };

struct Engine
{
    std::unique_ptr<InstrumentType> type = createToroSynth();
    StubPatch patch;

    Engine() { type->createMetadata(patch, patch.metadata); }

    std::unique_ptr<InstrumentVoice> voice(int idx = 0, std::map<int, float> overrides = {})
    {
        auto v = type->createVoice(idx);
        for (int p = 0; p < static_cast<int>(patch.metadata.params.size()); p++)
        {
            const auto& def = patch.metadata.params[p];
            float value = def.defaultValue ? *def.defaultValue : 0.0f;
            if (auto it = overrides.find(p); it != overrides.end())
                value = it->second;
            v->onParamChange(p, value);
        }
        return v;
    }
};

void noteOn(InstrumentVoice& v, int note, int vel = 100)
{
    MidiEvent e{};
    e.type = MidiEvent::Type::kNoteOn;
    e.data.note = { static_cast<uint8_t>(note), static_cast<uint8_t>(vel) };
    v.onMidiEvent(kCtx, e);
}

void noteOff(InstrumentVoice& v, int note)
{
    MidiEvent e{};
    e.type = MidiEvent::Type::kNoteOff;
    e.data.note = { static_cast<uint8_t>(note), 0 };
    v.onMidiEvent(kCtx, e);
}

void bend(InstrumentVoice& v, int16_t value)
{
    MidiEvent e{};
    e.type = MidiEvent::Type::kPitchBendChange;
    e.data.pitchBendChange = { value };
    v.onMidiEvent(kCtx, e);
}

std::vector<float> render(InstrumentVoice& v, int blocks, float prefill = 0.0f)
{
    std::vector<float> all;
    std::array<float, kBlock> l, r;
    for (int b = 0; b < blocks; b++)
    {
        l.fill(prefill);
        r.fill(prefill);
        AudioBufferStereoRef out{ AudioBufferRef(l), AudioBufferRef(r) };
        v.renderVoiceAudio(kCtx, std::nullopt, out, {});
        all.insert(all.end(), l.begin(), l.end());
    }
    return all;
}

double rms(const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to; i++)
        s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(to - from));
}

// Rising zero crossings per second over a window: a pitch estimate that works on a clean, filtered wave.
double crossingHz(const std::vector<float>& x, size_t from, size_t to)
{
    int n = 0;
    for (size_t i = from + 1; i < to; i++)
        n += (x[i - 1] < 0.0f && x[i] >= 0.0f);
    return n * 44100.0 / static_cast<double>(to - from);
}

// A clean tone for pitch measurements: one oscillator's worth (spread 0), triangle, no fold, filter open.
std::map<int, float> cleanTone(std::map<int, float> extra = {})
{
    std::map<int, float> m{ { kSpread, 0.0f }, { kShape, 1.0f }, { kFold, 0.0f }, { kNoise, 0.0f },
                            { kDrive, 0.0f },  { kAphx, 0.0f },  { kCutoff, 1.0f }, { kResonance, 0.0f },
                            { kAttack, 0.0f }, { kSustain, 1.0f } };
    for (auto& [k, v] : extra)
        m[k] = v;
    return m;
}

// ----------------------------------------------------------------------------------------------------------

void testMetadata(Engine& e)
{
    const auto& md = e.patch.metadata;
    CHECK(static_cast<int>(md.params.size()) == kCount, "expected %d params, got %zu", kCount, md.params.size());
    CHECK(md.instrumentTypeId == static_cast<int>(InstrumentTypeId::kToroSynth), "wrong instrument type id");
    CHECK(md.outputChannels == Patch::Metadata::OutputChannels::kMono, "TORO renders mono");

    std::set<int> ccs;
    for (int p = 0; p < static_cast<int>(md.params.size()); p++)
    {
        const auto& d = md.params[p];
        const float lo = static_cast<float>(d.minValue), hi = static_cast<float>(d.maxValue);
        CHECK(!d.displayName.empty() && d.displayName.size() <= 18, "param %d name '%s'", p, d.displayName.c_str());
        CHECK(d.defaultValue.has_value(), "param %d has no default", p);
        if (d.defaultValue)
            CHECK(*d.defaultValue >= lo && *d.defaultValue <= hi, "param %d default out of range", p);
        CHECK(lo < hi && std::fabs(lo) <= 65500.0f && std::fabs(hi) <= 65500.0f, "param %d range", p);
        CHECK(static_cast<bool>(d.valueFormatter), "param %d has no formatter", p);
        if (d.valueFormatter)
        {
            for (float v : { lo, d.defaultValue ? *d.defaultValue : lo, hi })
            {
                const std::string s = d.valueFormatter(e.patch, v);
                CHECK(!s.empty() && s.size() <= 12, "param %d formats %.3f as '%s'", p, v, s.c_str());
            }
        }
        if (d.midiCC)
        {
            CHECK(ccs.insert(*d.midiCC).second, "param %d reuses CC %d", p, *d.midiCC);
            CHECK(*d.midiCC < Patch::Metadata::Macro::kMacroCCStart, "param %d CC %d collides with macro CCs", p,
                  *d.midiCC);
        }
    }

    // Formatter spot checks: real units where toro's knobs have them.
    auto fmtOf = [&](int p, float v) { return md.params[p].valueFormatter(e.patch, v); };
    CHECK(fmtOf(kSpread, 0.0f) == "1.000x" && fmtOf(kSpread, 1.0f) == "1.500x", "spread shows the ratio");
    CHECK(fmtOf(kFold, 0.0f) == "1.0x" && fmtOf(kFold, 1.0f) == "8.0x", "fold shows the gain");
    CHECK(fmtOf(kShape, 0.0f) == "Saw" && fmtOf(kShape, 1.0f) == "Triangle", "shape names its ends");
    CHECK(fmtOf(kAphx, 0.0f) == "Off", "APHX 0 reads Off");
    e.patch.values[kResonance] = 0.0f;
    const std::string lowCut = fmtOf(kCutoff, 0.0f), highCut = fmtOf(kCutoff, 1.0f);
    CHECK(lowCut == "64 Hz", "cutoff at 0 shows '%s', expected 64 Hz", lowCut.c_str());
    CHECK(highCut.find("kHz") != std::string::npos, "cutoff at 1 shows '%s'", highCut.c_str());

    // Editor layout
    const auto& sections = md.layoutFlat.sections;
    CHECK(!sections.empty() && sections.size() <= 6, "editor shows at most 6 sections, got %zu", sections.size());
    const char* iconWords[] = { "Oscillator", "Modulation", "Amplifier", "Envelope", "LFO", "Filter",
                                "Shaper",     "Voice",      "FX",        "Mixer" };
    std::map<int, int> seen;
    for (const auto& s : sections)
    {
        bool icon = false;
        for (const char* w : iconWords)
            icon = icon || s.name.find(w) != std::string::npos;
        CHECK(icon, "section '%s' has no editor icon", s.name.c_str());
        for (const auto& el : s.elements)
            seen[el.id]++;
    }
    for (int p = 0; p < kCount; p++)
        CHECK(seen[p] == 1, "param %d appears %d times in the editor", p, seen[p]);

    // Macros
    std::vector<Patch::Metadata::Macro> macros(kMacroCount);
    md.initDefaultMacros(macros);
    for (size_t m = 0; m < kUserMacroCount; m++)
    {
        CHECK(!macros[m].displayName.empty() &&
                macros[m].displayName.size() <= static_cast<size_t>(Patch::Metadata::Macro::kMaxDisplayNameLen),
              "macro %zu name", m);
        CHECK(!macros[m].modifiers.empty(), "macro %zu has no modifiers", m);
        for (const auto& mod : macros[m].modifiers)
        {
            CHECK(mod.paramId && *mod.paramId >= 0 && *mod.paramId < kCount, "macro %zu points at a missing param", m);
            CHECK(std::fabs(mod.amount) <= Patch::Metadata::Macro::ParamModifier::kMaxAmount, "macro %zu amount", m);
        }
    }
    const auto& vel = macros[static_cast<int>(Patch::SystemMacroId::kVelocity)];
    CHECK(vel.modifiers.size() == 1 && vel.modifiers[0].paramId && *vel.modifiers[0].paramId == kVolume,
          "velocity macro drives Volume");
}

void testMatchesCore(Engine& e)
{
    // Instant attack, full sustain, volume 1: the voice output must be the core's output, sample for sample.
    const std::map<int, float> o{ { kSpread, 0.3f }, { kFold, 0.6f }, { kCutoff, 0.7f }, { kResonance, 0.5f },
                                  { kShape, 0.2f },  { kNoise, 0.1f }, { kDrive, 0.5f },  { kAphx, 0.6f },
                                  { kAttack, 0.0f }, { kSustain, 1.0f } };
    const int voiceIdx = 2;
    auto v = e.voice(voiceIdx, o);
    noteOn(*v, 57);
    const auto got = render(*v, 40);

    toro::Controls c;
    auto knob = [](float x) { return static_cast<toro::i32>(std::lround(x * 32767.0f)); };
    c.spread = knob(0.3f);
    c.fold = knob(0.6f);
    c.cutoff = knob(0.7f);
    c.res = knob(0.5f);
    c.shape = knob(0.2f);
    c.noise = knob(0.1f);
    c.drive = knob(0.5f);
    c.aphx = knob(0.6f);
    toro::State s;
    toro::reset(s, voiceIdx + 1);
    const float hz = 440.0f * std::exp2((57.0f - 69.0f) / 12.0f);
    const auto inc = static_cast<toro::u32>(std::clamp(hz * (4294967296.0f / 44100.0f), 1.0f, 2147483648.0f));
    std::array<toro::u32, kBlock> incs;
    incs.fill(inc);
    std::array<toro::i32, kBlock> raw;
    int mismatches = 0;
    for (int b = 0; b < 40; b++)
    {
        toro::render<7>(s, c, incs.data(), raw.data());
        for (int n = 0; n < kBlock; n++)
            mismatches += got[b * kBlock + n] != static_cast<float>(raw[n]) * (1.0f / 32768.0f);
    }
    CHECK(mismatches == 0, "voice differs from the core in %d samples", mismatches);
}

void testIdleAndRelease(Engine& e)
{
    auto v = e.voice(0, { { kRelease, 0.05f } });
    CHECK(!v->isActive(), "a fresh voice is idle");
    const auto idle = render(*v, 2, 123.0f);
    CHECK(std::all_of(idle.begin(), idle.end(), [](float x) { return x == 0.0f; }), "idle voice writes zeros");

    noteOn(*v, 60);
    CHECK(v->isActive() && v->handlesNote(60), "note on activates the voice");
    const auto on = render(*v, 40);
    CHECK(rms(on, 20 * kBlock, 40 * kBlock) > 0.01, "a held note is audible");

    noteOff(*v, 60);
    CHECK(v->isActive(), "release keeps the voice active");
    render(*v, 20); // 50 ms release = 17.2 blocks
    CHECK(!v->isActive(), "release ends the voice");
    const auto after = render(*v, 2, 9.0f);
    CHECK(std::all_of(after.begin(), after.end(), [](float x) { return x == 0.0f; }), "silent after release");
}

void testGlide(Engine& e)
{
    // Always mode, 0.2 s glide from C3 to C4: the pitch must be between the two partway through.
    {
        auto v = e.voice(0, cleanTone({ { kGlideTime, 0.2f }, { kGlideMode, 0.0f } }));
        noteOn(*v, 48);
        render(*v, 40);
        noteOn(*v, 60);
        const auto x = render(*v, 140); // ~0.4 s
        const double early = crossingHz(x, 0, 1024), mid = crossingHz(x, 3000, 5000), late = crossingHz(x, 12000, 17000);
        CHECK(early < 200.0, "glide starts near C3, measured %.1f Hz", early);
        CHECK(mid > early + 20.0 && mid < late - 20.0, "glide passes between: early %.1f mid %.1f late %.1f", early,
              mid, late);
        CHECK(std::fabs(late - 261.6) < 6.0, "glide lands on C4, measured %.1f Hz", late);
    }
    // Glide time 0: jumps straight to C4.
    {
        auto v = e.voice(0, cleanTone({ { kGlideTime, 0.0f } }));
        noteOn(*v, 48);
        render(*v, 40);
        noteOn(*v, 60);
        const auto x = render(*v, 40);
        CHECK(std::fabs(crossingHz(x, 256, 5000) - 261.6) < 6.0, "no glide when glide time is 0");
    }
    // Overlap mode: separated notes don't glide, overlapping ones do.
    {
        auto v = e.voice(0, cleanTone({ { kGlideTime, 0.2f }, { kGlideMode, 1.0f }, { kRelease, 0.01f } }));
        noteOn(*v, 48);
        render(*v, 40);
        noteOff(*v, 48);
        render(*v, 20); // fully released
        noteOn(*v, 60);
        const auto apart = render(*v, 40);
        // Skip the onset (a note from silence restarts the filter and DC blocker) and use a wide window, where
        // one stray crossing is ~11 Hz. A glide up from C3 would read far below 261.6 here (it takes 8820 samples).
        const double apartHz = crossingHz(apart, 256, 4352);
        CHECK(std::fabs(apartHz - 261.6) < 15.0, "overlap mode: separated notes don't glide (%.1f Hz)", apartHz);
        noteOn(*v, 48); // played over the held C4
        const auto over = render(*v, 20);
        CHECK(crossingHz(over, 0, 1024) > 180.0, "overlap mode: overlapping notes glide (%.1f Hz)",
              crossingHz(over, 0, 1024));
    }
}

void testLegatoDoesNotRetrigger(Engine& e)
{
    // A slow envelope that settles at a low sustain: retriggering would swell back up, legato must not.
    const std::map<int, float> env{ { kAttack, 0.3f }, { kDecay, 0.3f }, { kSustain, 0.3f } };
    auto measure = [&](float mode)
    {
        auto o = cleanTone({ { kGlideMode, mode }, { kGlideTime, 0.0f } });
        for (auto& [k, val] : env)
            o[k] = val;
        auto v = e.voice(0, o);
        noteOn(*v, 60);
        const auto settle = render(*v, 300); // ~0.87 s: well into sustain
        const double before = rms(settle, settle.size() - 4096, settle.size());
        noteOn(*v, 62); // overlapping
        const auto x = render(*v, 60);
        return rms(x, x.size() - 4096, x.size()) / before;
    };
    const double always = measure(0.0f), legato = measure(2.0f);
    CHECK(always > 1.8, "Always mode retriggers the envelope (level ratio %.2f)", always);
    CHECK(legato < 1.2 && legato > 0.8, "Legato mode keeps the envelope (level ratio %.2f)", legato);
}

void testPitchBend(Engine& e)
{
    auto v = e.voice(0, cleanTone());
    noteOn(*v, 57); // A3, 220 Hz
    render(*v, 20);
    bend(*v, 8191); // +2 semitones
    const auto x = render(*v, 60);
    const double hz = crossingHz(x, 512, x.size());
    CHECK(std::fabs(hz - 246.9) < 6.0, "pitch bend +2 st: expected ~246.9 Hz, measured %.1f", hz);
}

void testVolume(Engine& e)
{
    auto loud = e.voice(0, cleanTone({ { kVolume, 1.0f } }));
    auto soft = e.voice(0, cleanTone({ { kVolume, 0.5f } }));
    noteOn(*loud, 60);
    noteOn(*soft, 60);
    const auto a = render(*loud, 20), b = render(*soft, 20);
    const double ratio = rms(b, 0, b.size()) / rms(a, 0, a.size());
    CHECK(std::fabs(ratio - 0.5) < 0.001, "volume 0.5 halves the level (ratio %.4f)", ratio);
}

void testStress(Engine& e)
{
    // Random parameter changes, notes, bends and voice reuse: output must stay finite and bounded.
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    float worst = 0.0f;
    bool finite = true;
    for (int voiceIdx = 0; voiceIdx < 8; voiceIdx++)
    {
        auto v = e.voice(voiceIdx);
        for (int step = 0; step < 400; step++)
        {
            const int p = static_cast<int>(rng() % kCount);
            const auto& d = e.patch.metadata.params[p];
            const float lo = static_cast<float>(d.minValue), hi = static_cast<float>(d.maxValue);
            v->onParamChange(p, lo + (hi - lo) * u(rng));
            const int note = static_cast<int>(rng() % 128);
            switch (rng() % 4)
            {
                case 0: noteOn(*v, note, static_cast<int>(rng() % 128)); break;
                case 1: noteOff(*v, note); break;
                case 2: bend(*v, static_cast<int16_t>(static_cast<int>(rng() % 16384) - 8192)); break;
                default: break;
            }
            for (float x : render(*v, 1))
            {
                finite = finite && std::isfinite(x);
                worst = std::max(worst, std::fabs(x));
            }
        }
    }
    CHECK(finite, "stress: non-finite output");
    // core peak 0.75, envelope <= 1, volume <= 2
    CHECK(worst <= 0.75f * 2.0f + 1e-6f, "stress: output peaked at %.3f", worst);
}

} // namespace

int main()
{
    Engine e;
    testMetadata(e);
    testMatchesCore(e);
    testIdleAndRelease(e);
    testGlide(e);
    testLegatoDoesNotRetrigger(e);
    testPitchBend(e);
    testVolume(e);
    testStress(e);
    return toro_test::finish("toro voice");
}
