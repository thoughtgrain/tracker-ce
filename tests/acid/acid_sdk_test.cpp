// ACID on Tracker+ and Mini: the real core/synth/engines/AcidSynthVoice.cpp, driven the way PolySynth drives it.
//
// What this proves:
//  - Metadata: every parameter's name, default and range equal the open engine's table (so the editor can't
//    offer a range the engine doesn't clamp to); every formatter works at min / default / max; every enum
//    names each of its values; MIDI CCs are unique and clear of the macro CCs; the editor layout is the open
//    engine's (at most 6 sections, every parameter once, every section with an icon); macros point at real
//    parameters, and Velocity scales Volume.
//  - Sound: ACID through the SDK adapter renders exactly the samples the open ACID engine renders when driven
//    directly through the open contract (as a CE host on the Original Tracker will drive it), and ends on the
//    same block. The engine's own sound is pinned by tests/opensynth's goldens.
//
//   make -C tests/acid
//
// Copyright (c) 2026 briannhinton. MIT.

#include "AcidSynthVoice.h"
#include "../toro/harness.h"
#include "../toro/stub_patch.h"

#include "PolySynth/sdk/SynthVoiceSDK.h"
#include "open/OpenSynthEngine.h"
#include "open/OpenSynthLayout.h"
#include "open/OpenSynthParams.h"
#include "open/engines/Acid.h"

#include <array>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace polysynth;

namespace
{

constexpr int kBlock = kAudioBufferSize;
const InstrumentVoice::InstrumentVoiceContext kCtx{ 120.0f };
constexpr int kVolume = opensynth::acid::kVolume;

struct Engine
{
    std::unique_ptr<InstrumentType> type = createAcidSynth();
    StubPatch patch;
    Engine() { type->createMetadata(patch, patch.metadata); }

    std::unique_ptr<InstrumentVoice> voice(int idx, const std::map<int, float>& overrides)
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

void midi(InstrumentVoice& v, bool on, int note)
{
    MidiEvent e{};
    e.type = on ? MidiEvent::Type::kNoteOn : MidiEvent::Type::kNoteOff;
    e.data.note = { static_cast<uint8_t>(note), static_cast<uint8_t>(on ? 100 : 0) };
    v.onMidiEvent(kCtx, e);
}

// ---- Metadata -----------------------------------------------------------------------------------------------

void testMetadata()
{
    Engine e;
    const auto& md = e.patch.metadata;
    int n = 0;
    const opensynth::ParamInfo* t = opensynth::paramTable(opensynth::kOpenEngineAcid, &n);
    CHECK(t && static_cast<int>(md.params.size()) == n, "ACID has the open engine's %d parameters, got %zu", n,
          md.params.size());
    CHECK(md.instrumentTypeId == static_cast<int>(InstrumentTypeId::kAcidSynth), "ACID's instrument type id");
    CHECK(md.outputChannels == Patch::Metadata::OutputChannels::kMono, "ACID is mono");

    std::set<int> ccs;
    for (int i = 0; t && i < n && i < static_cast<int>(md.params.size()); i++)
    {
        const auto& p = md.params[i];
        CHECK(p.displayName == t[i].name, "param %d name '%s' vs '%s'", i, p.displayName.c_str(), t[i].name);
        CHECK(p.defaultValue && *p.defaultValue == t[i].def, "param %d default", i);
        CHECK(static_cast<float>(p.minValue) == t[i].min && static_cast<float>(p.maxValue) == t[i].max,
              "param %d range", i);
        CHECK(static_cast<bool>(p.valueFormatter), "param %d has a formatter", i);
        if (p.valueFormatter)
        {
            for (float v : { t[i].min, t[i].def, t[i].max })
            {
                e.patch.values[i] = v;
                CHECK(!p.valueFormatter(e.patch, v).empty(), "param %d formats %g", i, v);
            }
            if (p.isEnum)
            {
                // Every value has its own name.
                std::set<std::string> names;
                for (int k = static_cast<int>(t[i].min); k <= static_cast<int>(t[i].max); k++)
                    names.insert(p.valueFormatter(e.patch, static_cast<float>(k)));
                CHECK(static_cast<int>(names.size()) == static_cast<int>(t[i].max - t[i].min) + 1,
                      "enum param %d names every value", i);
            }
        }
        if (p.midiCC)
        {
            CHECK(ccs.insert(*p.midiCC).second, "param %d: MIDI CC %d used twice", i, *p.midiCC);
            CHECK(*p.midiCC < Patch::Metadata::Macro::kMacroCCStart, "param %d: CC clear of the macro CCs", i);
        }
    }

    // Layout: the open engine's.
    const auto& sections = md.layoutFlat.sections;
    int ns = 0;
    const opensynth::Section* open = opensynth::sectionTable(opensynth::kOpenEngineAcid, &ns);
    CHECK(open && static_cast<int>(sections.size()) == ns && ns <= opensynth::kMaxSections, "ACID's sections");
    std::vector<int> seen(n, 0);
    for (int k = 0; open && k < ns && k < static_cast<int>(sections.size()); k++)
    {
        CHECK(sections[k].name == open[k].name, "section %d name", k);
        CHECK(opensynth::sectionIcon(sections[k].name.c_str()) != opensynth::SectionIcon::kNone,
              "section '%s' has an icon", sections[k].name.c_str());
        CHECK(static_cast<int>(sections[k].elements.size()) == open[k].count, "section %d size", k);
        for (const auto& el : sections[k].elements)
            if (el.id >= 0 && el.id < n)
                seen[el.id]++;
    }
    for (int i = 0; i < n; i++)
        CHECK(seen[i] == 1, "param %d appears %d times in the editor", i, seen[i]);

    // Macros point at real parameters; Velocity scales Volume.
    std::vector<Patch::Metadata::Macro> macros(8);
    md.initDefaultMacros(macros);
    for (size_t m = 0; m < macros.size(); m++)
        for (const auto& mod : macros[m].modifiers)
            CHECK(mod.paramId && *mod.paramId >= 0 && *mod.paramId < n, "macro %zu points at a real parameter", m);
    const auto& vel = macros[static_cast<int>(Patch::SystemMacroId::kVelocity)].modifiers;
    CHECK(vel.size() == 1 && vel[0].paramId && *vel[0].paramId == kVolume, "Velocity scales Volume");
    CHECK(md.params[kVolume].macroOperation == Patch::Metadata::Param::MacroOperation::kMult,
          "Volume multiplies with its macros");
}

// ---- Sound: through the SDK adapter vs the open engine driven directly ---------------------------------------

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
};

bool same(const Case& c)
{
    Engine e;
    auto sdk = e.voice(0, c.params);

    auto* eng = opensynth::openSynthEngine(opensynth::kOpenEngineAcid);
    int n = 0;
    const opensynth::ParamInfo* t = opensynth::paramTable(opensynth::kOpenEngineAcid, &n);
    opensynth::SynthParams p;
    p.engine = opensynth::kOpenEngineAcid;
    p.count = n;
    for (int i = 0; i < n; i++)
        p.end[i] = t[i].def;
    for (const auto& [i, v] : c.params)
        p.end[i] = std::clamp(v, t[i].min, t[i].max);
    std::memcpy(p.start, p.end, sizeof(p.end));
    p.serial = 1;  // the adapter's first sync bumps its serial once when a value differs from the default
    opensynth::SynthSlotScratch slot;
    eng->slotReset(slot);
    opensynth::SynthVoice v;
    std::memset(v.state, 0, sizeof(v.state));
    v.slotScratch = &slot;
    bool active = false, started = false;
    std::vector<int> held;
    const float gain = p.end[kVolume];
    int last = 0;
    for (const auto& s : c.steps)
        last = std::max(last, s.block);

    std::array<float, kBlock> l, r;
    float ol[kBlock], orr[kBlock];
    for (int b = 0; b < 8000; b++)
    {
        for (const auto& s : c.steps)
        {
            if (s.block != b)
                continue;
            midi(*sdk, s.on, s.note);
            if (s.on)
            {
                opensynth::SynthNote note;
                note.pitch = static_cast<float>(s.note);
                note.note = static_cast<uint8_t>(s.note);
                note.velocity = 100.0f / 127.0f;
                note.reused = started;
                note.overlap = !held.empty();
                v.note = note.note;
                v.gate = true;
                eng->noteOn(v, p, note);
                active = started = true;
                std::erase(held, s.note);
                held.push_back(s.note);
            }
            else
            {
                const bool top = !held.empty() && held.back() == s.note;
                std::erase(held, s.note);
                if (held.empty())
                {
                    v.gate = false;
                    if (active)
                        eng->noteOff(v, p);
                }
                else if (top)
                {
                    // The SDK falls back to the held note: a legato note-on.
                    opensynth::SynthNote note;
                    note.pitch = static_cast<float>(held.back());
                    note.note = static_cast<uint8_t>(held.back());
                    note.reused = true;
                    note.overlap = true;
                    v.note = note.note;
                    eng->noteOn(v, p, note);
                    active = true;
                }
            }
        }
        const bool sdkActive = sdk->isActive();
        if (!sdkActive && !active && b > last)
            return true;
        if (sdkActive != active)
        {
            CHECK(false, "%s: block %d: SDK %s, open %s", c.name, b, sdkActive ? "active" : "ended",
                  active ? "active" : "ended");
            return false;
        }
        if (!active)
            continue;
        l.fill(0.0f);
        r.fill(0.0f);
        AudioBufferStereoRef out{ AudioBufferRef(l), AudioBufferRef(r) };
        sdk->renderVoiceAudio(kCtx, std::nullopt, out, {});

        std::fill(ol, ol + kBlock, 0.0f);
        std::fill(orr, orr + kBlock, 0.0f);
        eng->setTempo(kCtx.tempo);
        eng->prepare(slot, p);
        active = eng->render(v, p, gain, 0.0f, ol, orr);
        for (int i = 0; i < kBlock; i++)
        {
            if (l[i] != ol[i])
            {
                CHECK(false, "%s: block %d sample %d: SDK %.9g, open %.9g", c.name, b, i, l[i], ol[i]);
                return false;
            }
        }
    }
    CHECK(false, "%s: still sounding after 8000 blocks", c.name);
    return false;
}

std::vector<Step> oneNote(int note, int off) { return { { 0, true, note }, { off, false, note } }; }

void testSound()
{
    std::vector<Case> cases;
    cases.push_back({ "default", {}, oneNote(45, 40) });
    int n = 0;
    const opensynth::ParamInfo* t = opensynth::paramTable(opensynth::kOpenEngineAcid, &n);
    for (int i = 0; t && i < n; i++)
        for (float v : { t[i].min, 0.5f * (t[i].min + t[i].max), t[i].max })
        {
            Case c{ t[i].name, { { i, v }, { 1, 0.5f }, { 2, 0.4f }, { 14, 0.5f }, { 17, 0.6f } }, oneNote(43, 25) };
            if (i == opensynth::acid::kAmpR || i == opensynth::acid::kModR)
                c.params[i] = std::min(v, 3.0f);  // keep the case short; long releases are covered in tests/opensynth
            cases.push_back(c);
        }
    for (float mode : { 0.0f, 1.0f, 2.0f, 3.0f })
    {
        Case g{ "glide", { { 26, mode }, { 27, 0.15f }, { 17, 0.7f } }, {} };
        g.steps = { { 0, true, 40 }, { 20, true, 52 }, { 30, false, 52 }, { 40, true, 47 }, { 55, false, 47 },
                    { 60, false, 40 } };
        cases.push_back(g);
    }
    std::mt19937 rng(20261105);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int k = 0; k < 30; k++)
    {
        Case c{ "random", {}, {} };
        for (int i = 0; t && i < n; i++)
            c.params[i] = t[i].min + u(rng) * (t[i].max - t[i].min);
        for (int i : { 15, 16, 18, 19, 20, 22 })
            c.params[i] = std::min(c.params[i], 1.5f);
        const int a = 30 + static_cast<int>(u(rng) * 40.0f), b2 = 30 + static_cast<int>(u(rng) * 40.0f);
        c.steps = { { 0, true, a }, { 12, true, b2 }, { 25, false, a }, { 40, false, b2 } };
        cases.push_back(c);
    }
    int matched = 0;
    for (const auto& c : cases)
        matched += same(c) ? 1 : 0;
    CHECK(matched == static_cast<int>(cases.size()), "%d of %zu cases identical", matched, cases.size());
    std::printf("acid sdk: %d of %zu cases render as the open engine does\n", matched, cases.size());
}

} // namespace

int main()
{
    testMetadata();
    testSound();
    return toro_test::finish("acid sdk");
}
