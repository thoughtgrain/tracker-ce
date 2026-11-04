// TORO synth engine for Tracker+ and Tracker Mini: its PolySynth metadata.
//
// The voice itself is the open engine (core/synth/open/engines/Toro.cpp, which can run on all three
// Trackers), hosted through the SDK by OpenEngineVoice. This file supplies what the Tracker+ / Mini patch
// editor needs on top: parameter names and ranges, values shown in real units where toro's knobs map to one
// (Hz, ratio, x gain), MIDI CCs, the editor sections and the default macros. Each parameter handler hands
// the value to the open engine through setParam().
//
// Velocity works like the factory engines and CHIP: the system Velocity macro scales Volume.
//
// tests/toro/sdk_regression_test.cpp checks this renders exactly what the original SDK TORO voice rendered.
//
// Copyright (c) 2026 briannhinton. MIT.

#include "config.h"
#ifdef FEATURE_SYNTH

#include "ToroSynthVoice.h"
#include "OpenEngineVoice.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"
#include "open/engines/ToroCore.h"
#include "open/engines/Toro.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace polysynth
{

namespace
{

// Serialized in patch files: only ever append, never reorder.
enum class ToroParamId : int
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
};

constexpr int id(ToroParamId p) { return static_cast<int>(p); }

constexpr uint8_t cc(sdk::MidiCCs c, int offset = 0) { return static_cast<uint8_t>(static_cast<int>(c) + offset); }

// 0..1 parameter -> toro's 0..32767 knob scale
toro::i32 toKnob(float v)
{
    return static_cast<toro::i32>(std::lround(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(toro::kKnobMax)));
}

// The low-pass's base cutoff in Hz for a knob position, with the resonance-dependent stability cap applied the
// same way the core applies it. APHX moves the real cutoff per key on top of this, so it's the base value.
float cutoffHz(float cutoff01, float res01)
{
    const toro::i32 coef = toro::detail::cutoffCoef(toKnob(cutoff01));
    const toro::i32 damp = 32768 - (toKnob(res01) * 30000 >> 15);
    const toro::i32 fcap = toro::kFMax - (((damp - 2768) * 5764) >> 15);
    const float f = static_cast<float>(std::min(coef, fcap)) / 32768.0f; // Chamberlin f = 2 sin(pi fc / fs)
    return sdk::kSampleRate / std::numbers::pi_v<float> * std::asin(std::clamp(f * 0.5f, 0.0f, 1.0f));
}

std::string formatHz(float hz)
{
    if (hz < 1000.0f)
    {
        return fmt::format("{:.0f} Hz", hz);
    }
    return fmt::format("{:.2f} kHz", hz / 1000.0f);
}

constexpr std::array kGlideModeNames = { "Always", "Overlap", "Legato" };

// The open TORO engine, hosted through the SDK. Its parameter order is ToroParamId's.
static_assert(static_cast<int>(ToroParamId::kVolume) == opensynth::toro_engine::kVolume,
              "ToroParamId and the open engine's parameters must be the same list");

class ToroSynthVoice : public sdk::OpenEngineVoice<ToroSynthVoice, ToroParamId, opensynth::kOpenEngineToro>
{
  public:
    using OpenEngineVoice::OpenEngineVoice;

    static void createMetadata(const Patch& patch, Patch::Metadata& metadata);
};

void ToroSynthVoice::createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata)
{
    using namespace polysynth::sdk;
    using Curve = Patch::Metadata::Param::Curve;
    using MacroOp = Patch::Metadata::Param::MacroOperation;
    using Value = Patch::optional_paramvalue_t;

    constexpr auto kParamCount = enumutil::EnumMax<ToroParamId>::value + 1;
    metadata.params.resize(kParamCount);
    metadata.params.shrink_to_fit();
    if (paramHandlers.empty())
    {
        paramHandlers.resize(kParamCount);
    }

    ParamHelper<ToroSynthVoice> helper(metadata, paramHandlers);

    // ---- Oscillator --------------------------------------------------------------------------------------

    helper.addParam(id(ToroParamId::kSpread),
                    {
                      .displayName = "Spread",
                      .defaultValue = 0.06f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kOscParam0, 0),
                      .valueFormatter = [](const Patch&, Patch::paramvalue_t v)
                      { return fmt::format("{:.3f}x", 1.0f + 0.5f * std::clamp(v, 0.0f, 1.0f)); },
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kSpread), *v); });

    helper.addParam(id(ToroParamId::kShape),
                    {
                      .displayName = "Shape",
                      .defaultValue = 0.0f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kOscParam0, 1),
                      .valueFormatter = [](const Patch&, Patch::paramvalue_t v) -> std::string
                      {
                          if (v <= 0.005f)
                          {
                              return "Saw";
                          }
                          if (v >= 0.995f)
                          {
                              return "Triangle";
                          }
                          return fmt::format("{:.0f} % Tri", v * 100.0f);
                      },
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kShape), *v); });

    helper.addParam(id(ToroParamId::kNoise),
                    {
                      .displayName = "Noise",
                      .defaultValue = 0.0f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kOscParam0, 2),
                      .valueFormatter = createPercentFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kNoise), *v); });

    // ---- Shaper ------------------------------------------------------------------------------------------

    helper.addParam(id(ToroParamId::kFold),
                    {
                      .displayName = "Fold",
                      .defaultValue = 0.18f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kOscParam0, 3),
                      .valueFormatter = [](const Patch&, Patch::paramvalue_t v)
                      { return fmt::format("{:.1f}x", 1.0f + 7.0f * std::clamp(v, 0.0f, 1.0f)); },
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kFold), *v); });

    helper.addParam(id(ToroParamId::kDrive),
                    {
                      .displayName = "Drive",
                      .defaultValue = 0.0f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kOscParam0, 4),
                      .valueFormatter = [](const Patch&, Patch::paramvalue_t v)
                      { return fmt::format("{:.2f}x", 1.0f + 3.0f * std::clamp(v, 0.0f, 1.0f)); },
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kDrive), *v); });

    helper.addParam(id(ToroParamId::kAphx),
                    {
                      .displayName = "APHX",
                      .defaultValue = 0.0f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kOscParam0, 5),
                      .valueFormatter = [](const Patch&, Patch::paramvalue_t v) -> std::string
                      { return v <= 0.0f ? std::string("Off") : fmt::format("{:.0f} %", v * 100.0f); },
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kAphx), *v); });

    // ---- Filter ------------------------------------------------------------------------------------------

    helper.addParam(id(ToroParamId::kCutoff),
                    {
                      .displayName = "Cutoff",
                      .defaultValue = 0.55f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kFilterCutoff),
                      .valueFormatter = [](const Patch& patch, Patch::paramvalue_t v)
                      {
                          const auto res = patch.getParamValue(id(ToroParamId::kResonance));
                          return formatHz(cutoffHz(v, res ? *res : 0.24f));
                      },
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kCutoff), *v); });

    helper.addParam(id(ToroParamId::kResonance),
                    {
                      .displayName = "Resonance",
                      .defaultValue = 0.24f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kFilterResonance),
                      .valueFormatter = createPercentFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kResonance), *v); });

    // ---- Amplifier ---------------------------------------------------------------------------------------

    helper.addParam(id(ToroParamId::kAttack),
                    {
                      .displayName = "Attack",
                      .defaultValue = 0.005f,
                      .minValue = 0.0f,
                      .maxValue = 5.0f,
                      .editCurve = Curve::kExponential,
                      .midiCC = cc(MidiCCs::kAmpEnvAttack),
                      .valueFormatter = createTimeFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kAttack), *v); });

    helper.addParam(id(ToroParamId::kDecay),
                    {
                      .displayName = "Decay",
                      .defaultValue = 0.6f,
                      .minValue = 0.01f,
                      .maxValue = 5.0f,
                      .editCurve = Curve::kExponential,
                      .midiCC = cc(MidiCCs::kAmpEnvDecay),
                      .valueFormatter = createTimeFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kDecay), *v); });

    helper.addParam(id(ToroParamId::kSustain),
                    {
                      .displayName = "Sustain",
                      .defaultValue = 0.7f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kAmpEnvSustain),
                      .valueFormatter = createPercentFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kSustain), *v); });

    helper.addParam(id(ToroParamId::kRelease),
                    {
                      .displayName = "Release",
                      .defaultValue = 0.3f,
                      .minValue = 0.005f,
                      .maxValue = 8.0f,
                      .editCurve = Curve::kExponential,
                      .midiCC = cc(MidiCCs::kAmpEnvRelease),
                      .valueFormatter = createTimeFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kRelease), *v); });

    // ---- Voice -------------------------------------------------------------------------------------------

    helper.addParam(id(ToroParamId::kGlideMode),
                    {
                      .displayName = "Glide Mode",
                      .defaultValue = 0,
                      .minValue = 0,
                      .maxValue = static_cast<Patch::paramvalue_short_t>(kGlideModeNames.size() - 1),
                      .isEnum = true,
                      .valueFormatter = [](const Patch&, Patch::paramvalue_t v)
                      {
                          const int idx = std::clamp<int>(static_cast<int>(std::lround(v)), 0,
                                                          static_cast<int>(kGlideModeNames.size()) - 1);
                          return std::string(kGlideModeNames[idx]);
                      },
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kGlideMode), *v); });

    helper.addParam(id(ToroParamId::kGlideTime),
                    {
                      .displayName = "Glide Time",
                      .defaultValue = 0.0f,
                      .minValue = 0.0f,
                      .maxValue = 2.0f,
                      .editCurve = Curve::kExponential,
                      .valueFormatter = createTimeFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kGlideTime), *v); });

    helper.addParam(id(ToroParamId::kVolume),
                    {
                      .displayName = "Volume",
                      .defaultValue = 1.0f,
                      .minValue = 0.0f,
                      .maxValue = 2.0f,
                      .macroOperation = MacroOp::kMult,
                      .valueFormatter = createPercentFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.setParam(id(ToroParamId::kVolume), *v); });

    // ---- Patch editor layout -----------------------------------------------------------------------------
    // Section names carry the substrings the patch editor uses to pick tile icons (Oscillator, Shaper,
    // Filter, Amplifier, Voice).

    auto param = [](ToroParamId p) { return EditorElement{ .type = EditorElement::Type::kParam, .id = id(p) }; };

    metadata.layoutFlat = Patch::Metadata::EditorLayout{
        .sections = {
          {
            .name = "Oscillator",
            .shortName = "Osc",
            .elements = { param(ToroParamId::kSpread), param(ToroParamId::kShape), param(ToroParamId::kNoise) },
          },
          {
            .name = "Shaper",
            .shortName = "Shp",
            .elements = { param(ToroParamId::kFold), param(ToroParamId::kDrive), param(ToroParamId::kAphx) },
          },
          {
            .name = "Filter",
            .shortName = "Filter",
            .elements = { param(ToroParamId::kCutoff), param(ToroParamId::kResonance) },
          },
          {
            .name = "Amplifier",
            .shortName = "Amp",
            .elements = { param(ToroParamId::kAttack),
                          param(ToroParamId::kDecay),
                          param(ToroParamId::kSustain),
                          param(ToroParamId::kRelease) },
          },
          {
            .name = "Voice",
            .shortName = "Voice",
            .elements = { param(ToroParamId::kGlideMode),
                          param(ToroParamId::kGlideTime),
                          param(ToroParamId::kVolume) },
          },
        },
    };
    metadata.layout23 = metadata.layoutFlat;

    // ---- Default macros ----------------------------------------------------------------------------------
    // M1-M4 are toro's four page-1 knobs, the same ones meikyu's MOD lane drives on the OP-1.

    metadata.initDefaultMacros = [](auto& macros)
    {
        macros[0] = Patch::Metadata::Macro{
            .displayName = "Spread",
            .modifiers = { { id(ToroParamId::kSpread), 1.0f } },
            .bipolar = false,
        };
        macros[1] = Patch::Metadata::Macro{
            .displayName = "Fold",
            .modifiers = { { id(ToroParamId::kFold), 1.0f } },
            .bipolar = true,
        };
        macros[2] = Patch::Metadata::Macro{
            .displayName = "Cutoff",
            .modifiers = { { id(ToroParamId::kCutoff), 1.0f } },
            .bipolar = true,
        };
        macros[3] = Patch::Metadata::Macro{
            .displayName = "Resonance",
            .modifiers = { { id(ToroParamId::kResonance), 1.0f } },
            .bipolar = true,
        };
        macros[4] = Patch::Metadata::Macro{
            .displayName = "APHX",
            .modifiers = { { id(ToroParamId::kAphx), 1.0f } },
            .bipolar = false,
        };
        macros[5] = Patch::Metadata::Macro{
            .displayName = "Env Decay / Rel",
            .modifiers = {
              { id(ToroParamId::kDecay), 0.8f },
              { id(ToroParamId::kRelease), 0.6f },
            },
            .bipolar = true,
        };

        macros[static_cast<int>(Patch::SystemMacroId::kVelocity)].modifiers = {
            { id(ToroParamId::kVolume), 1.0f },
        };
    };

    metadata.instrumentTypeId = static_cast<int>(InstrumentTypeId::kToroSynth);
    metadata.outputChannels = Patch::Metadata::OutputChannels::kMono;
}

} // namespace

std::unique_ptr<InstrumentType> createToroSynth()
{
    return std::make_unique<InstrumentType>(
      InstrumentType{ .name = "TORO",
                      .createMetadata = &ToroSynthVoice::createMetadata,
                      .createVoice = [](int voiceIdx) -> std::unique_ptr<InstrumentVoice>
                      { return std::make_unique<ToroSynthVoice>(voiceIdx); } });
}

} // namespace polysynth

#endif
