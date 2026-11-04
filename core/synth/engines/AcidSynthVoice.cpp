// ACID synth engine for Tracker+ and Tracker Mini: its PolySynth metadata.
//
// The voice is the open ACID engine (core/synth/open/engines/Acid.cpp, ported from OpenTracker's ACD, which
// can run on all three Trackers), hosted through the SDK by OpenEngineVoice. Names, defaults and ranges come
// from the open engine's table and the editor layout from its section table, so none of them can drift from
// what the engine does. This file adds only what the patch editor shows on top: edit curves, value
// formatters, enum names, MIDI CCs and the default macros.
//
// Velocity works like the factory engines: the system Velocity macro scales Volume.
//
// Copyright (c) 2026 briannhinton. MIT.

#include "config.h"
#ifdef FEATURE_SYNTH

#include "AcidSynthVoice.h"
#include "OpenEngineVoice.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"
#include "open/engines/Acid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iterator>
#include <optional>

namespace polysynth
{

namespace
{

// The open engine's parameter list (acid::Param). Serialized in patch files: only ever append, never reorder.
enum class AcidParamId : int
{
    kSawMix = 0,
    kSquareMix,
    kSubMix,
    kNoiseMix,
    kPw,
    kSubMode,
    kPwLfo,
    kPwEnv,
    kPitchLfo,
    kCutoff,
    kResonance,
    kFilterType,
    kNoteTrack,
    kFilterLfo,
    kFilterEnv,
    kAmpAttack,
    kAmpDecay,
    kAmpSustain,
    kAmpRelease,
    kModAttack,
    kModDecay,
    kModSustain,
    kModRelease,
    kLfoRate,
    kLfoWave,
    kLfoRetrigger,
    kGlideMode,
    kGlideTime,
    kVolume,
};
static_assert(static_cast<int>(AcidParamId::kVolume) == opensynth::acid::kVolume &&
                static_cast<int>(AcidParamId::kVolume) + 1 == opensynth::acid::kParamCount,
              "AcidParamId and the open engine's parameters must be the same list");

constexpr int id(AcidParamId p) { return static_cast<int>(p); }
constexpr uint8_t cc(sdk::MidiCCs c, int offset = 0) { return static_cast<uint8_t>(static_cast<int>(c) + offset); }

// Enum names, in the engine's value order (Acid.cpp).
constexpr const char* kSubModeNames[] = { "-1 Square", "-1 Pulse", "-1 Triangle",
                                          "-2 Square", "-2 Pulse", "-2 Triangle" };
constexpr const char* kFilterTypeNames[] = { "SVF 12 dB", "SVF 24 dB", "Ladder" };
constexpr const char* kLfoWaveNames[] = { "Triangle", "Sine", "Ramp Up", "Ramp Down", "Square", "S&H" };
constexpr const char* kRetriggerNames[] = { "Free", "One Shot", "Note" };
constexpr const char* kGlideModeNames[] = { "Always", "Overlap", "Legato", "Legato Overlap" };

template<const auto& kNames>
std::string enumName(const Patch&, Patch::paramvalue_t v)
{
    const int n = static_cast<int>(std::size(kNames));
    return kNames[std::clamp(static_cast<int>(std::lround(v)), 0, n - 1)];
}

std::string signedPercent(const Patch&, Patch::paramvalue_t v)
{
    return std::lround(v * 100.0f) == 0 ? std::string("0 %") : fmt::format("{:+.0f} %", v * 100.0f);
}

// Noise Mix: the level is |value|; above 0 the noise goes through the filter, below 0 it's added after it.
std::string noiseMix(const Patch&, Patch::paramvalue_t v)
{
    if (std::lround(v * 100.0f) == 0)
        return "Off";
    return fmt::format("{} {:.0f} %", v > 0.0f ? "Pre" : "Post", std::fabs(v) * 100.0f);
}

std::string octaves(const Patch&, Patch::paramvalue_t v)
{
    return std::fabs(v) < 0.005f ? std::string("0 oct") : fmt::format("{:+.2f} oct", v);
}

using Param = Patch::Metadata::Param;
using Curve = Param::Curve;
using MacroOp = Param::MacroOperation;

// One parameter's presentation. Name, default and range are left empty: addOpenEngineParams fills them from
// the open engine's table.
Param ui(std::function<std::string(const Patch&, Patch::paramvalue_t)> formatter,
         std::optional<uint8_t> midi = std::nullopt,
         Curve curve = Curve::kLinear,
         bool isEnum = false,
         MacroOp op = MacroOp::kAdd)
{
    Param p;
    p.editCurve = curve;
    p.macroOperation = op;
    p.isEnum = isEnum;
    if (midi)
        p.midiCC = *midi;
    p.valueFormatter = std::move(formatter);
    return p;
}
Param pct(std::optional<uint8_t> midi = std::nullopt) { return ui(sdk::createPercentFormatter(), midi); }
Param bipolar(std::optional<uint8_t> midi = std::nullopt) { return ui(&signedPercent, midi); }
Param seconds(std::optional<uint8_t> midi = std::nullopt)
{
    return ui(sdk::createTimeFormatter(), midi, Curve::kExponential);
}
Param hertz(std::optional<uint8_t> midi = std::nullopt)
{
    return ui(sdk::createFrequencyFormatter(), midi, Curve::kExponential);
}
template<const auto& kNames>
Param enumParam(std::optional<uint8_t> midi = std::nullopt)
{
    return ui(&enumName<kNames>, midi, Curve::kLinear, true);
}

class AcidSynthVoice : public sdk::OpenEngineVoice<AcidSynthVoice, AcidParamId, opensynth::kOpenEngineAcid>
{
  public:
    using OpenEngineVoice::OpenEngineVoice;

    static void createMetadata(const Patch& patch, Patch::Metadata& metadata);
};

void AcidSynthVoice::createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata)
{
    using namespace polysynth::sdk;

    constexpr auto kParamCount = enumutil::EnumMax<AcidParamId>::value + 1;
    metadata.params.resize(kParamCount);
    metadata.params.shrink_to_fit();
    if (paramHandlers.empty())
    {
        paramHandlers.resize(kParamCount);
    }
    ParamHelper<AcidSynthVoice> helper(metadata, paramHandlers);

    // Presentation per parameter, in AcidParamId order; names, defaults and ranges come from the open engine.
    addOpenEngineParams<AcidSynthVoice, opensynth::kOpenEngineAcid>(
      helper,
      std::array<Param, kParamCount>{
        pct(cc(MidiCCs::kOscParam0, 0)),                              // Saw Mix
        pct(cc(MidiCCs::kOscParam0, 1)),                              // Square Mix
        pct(cc(MidiCCs::kOscParam0, 2)),                              // Sub Mix
        ui(&noiseMix, cc(MidiCCs::kOscParam0, 3)),                    // Noise Mix
        pct(cc(MidiCCs::kOscParam0, 4)),                              // PW
        enumParam<kSubModeNames>(cc(MidiCCs::kOscParam0, 5)),         // Sub Mode
        bipolar(cc(MidiCCs::kOscParam0, 6)),                          // PW LFO
        bipolar(cc(MidiCCs::kOscParam0, 7)),                          // PW Env
        ui(&octaves, cc(MidiCCs::kOscParam0, 8)),                     // Pitch LFO
        hertz(cc(MidiCCs::kFilterCutoff)),                            // Cutoff
        pct(cc(MidiCCs::kFilterResonance)),                           // Resonance
        enumParam<kFilterTypeNames>(cc(MidiCCs::kFilterType)),        // Filter Type
        bipolar(cc(MidiCCs::kOscParam0, 9)),                          // Note Track
        bipolar(cc(MidiCCs::kFilterLfoAmount)),                       // Filter LFO
        bipolar(cc(MidiCCs::kFilterEnvAmount)),                       // Filter Env
        seconds(cc(MidiCCs::kAmpEnvAttack)),                          // Amp Attack
        seconds(cc(MidiCCs::kAmpEnvDecay)),                           // Amp Decay
        pct(cc(MidiCCs::kAmpEnvSustain)),                             // Amp Sustain
        seconds(cc(MidiCCs::kAmpEnvRelease)),                         // Amp Release
        seconds(cc(MidiCCs::kFilterEnvAttack)),                       // Mod Attack (drives filter and PW)
        seconds(cc(MidiCCs::kFilterEnvDecay)),                        // Mod Decay
        pct(cc(MidiCCs::kFilterEnvSustain)),                          // Mod Sustain
        seconds(cc(MidiCCs::kFilterEnvRelease)),                      // Mod Release
        hertz(cc(MidiCCs::kLfo1Frequency)),                           // LFO Rate
        enumParam<kLfoWaveNames>(cc(MidiCCs::kOscParam0, 10)),        // LFO Wave
        enumParam<kRetriggerNames>(cc(MidiCCs::kOscParam0, 11)),      // LFO Retrigger
        enumParam<kGlideModeNames>(),                                 // Glide Mode
        seconds(),                                                    // Glide Time
        ui(createPercentFormatter(), std::nullopt, Curve::kLinear, false, MacroOp::kMult), // Volume
      });

    // ---- Patch editor layout (core/synth/open/OpenSynthLayout.cpp) ----------------------------------------
    metadata.layoutFlat = openEngineLayout(opensynth::kOpenEngineAcid, { "Osc", "Filter", "Amp Env", "Mod Env", "LFO", "Voice" });
    metadata.layout23 = metadata.layoutFlat;

    // ---- Default macros ----------------------------------------------------------------------------------
    // The acid controls: cutoff, resonance, envelope amount and decay, plus a saw-to-square wave fade and glide.
    metadata.initDefaultMacros = [](auto& macros)
    {
        macros[0] = Patch::Metadata::Macro{
            .displayName = "Cutoff",
            .modifiers = { { id(AcidParamId::kCutoff), 1.0f } },
            .bipolar = true,
        };
        macros[1] = Patch::Metadata::Macro{
            .displayName = "Resonance",
            .modifiers = { { id(AcidParamId::kResonance), 1.0f } },
            .bipolar = true,
        };
        macros[2] = Patch::Metadata::Macro{
            .displayName = "Env Amount",
            .modifiers = { { id(AcidParamId::kFilterEnv), 1.0f } },
            .bipolar = true,
        };
        macros[3] = Patch::Metadata::Macro{
            .displayName = "Decay",
            .modifiers = {
              { id(AcidParamId::kModDecay), 0.8f },
              { id(AcidParamId::kAmpDecay), 0.5f },
            },
            .bipolar = true,
        };
        macros[4] = Patch::Metadata::Macro{
            .displayName = "Wave",
            .modifiers = {
              { id(AcidParamId::kSawMix), -1.0f },
              { id(AcidParamId::kSquareMix), 1.0f },
            },
            .bipolar = false,
        };
        macros[5] = Patch::Metadata::Macro{
            .displayName = "Glide",
            .modifiers = { { id(AcidParamId::kGlideTime), 1.0f } },
            .bipolar = false,
        };

        macros[static_cast<int>(Patch::SystemMacroId::kVelocity)].modifiers = {
            { id(AcidParamId::kVolume), 1.0f },
        };
    };

    metadata.instrumentTypeId = static_cast<int>(InstrumentTypeId::kAcidSynth);
    metadata.outputChannels = Patch::Metadata::OutputChannels::kMono;
}

} // namespace

std::unique_ptr<InstrumentType> createAcidSynth()
{
    return std::make_unique<InstrumentType>(
      InstrumentType{ .name = "ACID",
                      .createMetadata = &AcidSynthVoice::createMetadata,
                      .createVoice = [](int voiceIdx) -> std::unique_ptr<InstrumentVoice>
                      { return std::make_unique<AcidSynthVoice>(voiceIdx); } });
}

} // namespace polysynth

#endif
