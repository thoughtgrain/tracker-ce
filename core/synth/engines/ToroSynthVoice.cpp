// TORO synth engine for Tracker+ and Tracker Mini.
//
// This file is the Tracker side of toro: parameters, envelope, glide and pitch. The sound itself is
// ToroCore.h, the same integer DSP as the OP-1 and OP-XY builds, so a patch here sounds like it does there.
//
// What the Tracker adds on top of the original voice:
// - an ADSR amp envelope (the OP-1 and OP-XY hosts supplied their own),
// - glide with Always / Overlap / Legato modes, done per sample in the pitch domain so APHX's key blending
//   slides smoothly the way it was designed to,
// - values shown in real units where toro's knobs map to one (Hz, ratio, x gain).
//
// Velocity works like the factory engines and CHIP: the system Velocity macro scales Volume.
//
// Copyright (c) 2026 briannhinton. MIT.

#include "config.h"
#ifdef FEATURE_SYNTH

#include "ToroSynthVoice.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"
#include "open/engines/ToroCore.h"

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

class ToroSynthVoice : public sdk::CustomSynthVoice<ToroSynthVoice, ToroParamId>
{
  public:
    explicit ToroSynthVoice(int voiceIdx)
      : owner(static_cast<toro::u32>(voiceIdx) + 1u)
    {
    }

    bool isActive() final { return envStage != EnvStage::kIdle; }

    void onNoteOn(const InstrumentVoiceContext& /*ctx*/, note_t note, float /*velocity*/, bool isLegatoStep) final
    {
        const float target = static_cast<float>(note);
        // Overlap: this voice is still holding a note (attack, decay or sustain). That covers a new note played
        // over a held one and the fall-back to a still-held note when the top one is released.
        const bool overlapping = envStage != EnvStage::kIdle && envStage != EnvStage::kRelease;
        bool glide = hasPlayedNote && glideTimeSec > 0.001f;
        if (playMode == PlayMode::kOverlap || playMode == PlayMode::kLegato)
        {
            glide = glide && overlapping;
        }

        if (glide)
        {
            glideSamplesLeft = std::max(1.0f, glideTimeSec * sdk::kSampleRate);
            glideStep = (target - currentPitch) / glideSamplesLeft;
        }
        else
        {
            currentPitch = target;
            glideSamplesLeft = 0.0f;
        }
        targetPitch = target;
        hasPlayedNote = true;

        // A voice coming out of silence starts toro fresh (phases, filter, no cutoff sweep). A voice that is
        // still sounding keeps its state, so a retrigger or a steal doesn't click.
        if (envStage == EnvStage::kIdle)
        {
            toro::reset(dsp, owner);
            envLevel = 0.0f;
        }
        if (!isLegatoStep || envStage == EnvStage::kIdle || envStage == EnvStage::kRelease)
        {
            envStage = EnvStage::kAttack;
        }
    }

    void onNoteOff(const InstrumentVoiceContext& /*ctx*/, note_t /*note*/) final
    {
        if (envStage != EnvStage::kIdle)
        {
            envStage = EnvStage::kRelease;
        }
    }

    void renderVoiceAudio(const InstrumentVoiceContext& /*ctx*/,
                          std::optional<AudioBufferStereoRef> /*inputBuffer*/,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> /*auxOutBuffer*/) final
    {
        auto out = outputBuffer[0];
        if (envStage == EnvStage::kIdle)
        {
            std::fill(out.begin(), out.end(), 0.0f);
            return;
        }

        static_assert(kAudioBufferSize == (1 << kBlockShift), "toro renders exactly one Tracker block");

        // Pitch: one phase increment per sample, so glides and bends move smoothly inside the block.
        // The increment is exponential in pitch; across one 2.9 ms block a linear ramp between the two
        // ends is within a fraction of a cent of the exact curve.
        const float pitchStart = currentPitch;
        advanceGlide(static_cast<float>(kAudioBufferSize));
        const float incStart = pitchToInc(pitchStart + pitchBendSemitones);
        const float incEnd = pitchToInc(currentPitch + pitchBendSemitones);
        const float incStep = (incEnd - incStart) / static_cast<float>(kAudioBufferSize);
        std::array<toro::u32, kAudioBufferSize> inc;
        for (int n = 0; n < kAudioBufferSize; n++)
        {
            inc[n] = static_cast<toro::u32>(incStart + incStep * static_cast<float>(n));
        }

        std::array<toro::i32, kAudioBufferSize> raw;
        toro::render<kBlockShift>(dsp, controls, inc.data(), raw.data());

        // Envelope, per sample
        const float sr = sdk::kSampleRate;
        const float attackInc = attackSec > 0.001f ? 1.0f / (attackSec * sr) : 1.0f;
        const float decayDec = decaySec > 0.002f ? (1.0f - sustainLevel) / (decaySec * sr) : 1.0f;
        const float releaseDec = releaseSec > 0.002f ? 1.0f / (releaseSec * sr) : 1.0f;
        constexpr float kQ15 = 1.0f / 32768.0f;

        for (int n = 0; n < kAudioBufferSize; n++)
        {
            switch (envStage)
            {
                case EnvStage::kAttack:
                    envLevel += attackInc;
                    if (envLevel >= 1.0f)
                    {
                        envLevel = 1.0f;
                        envStage = EnvStage::kDecay;
                    }
                    break;
                case EnvStage::kDecay:
                    envLevel -= decayDec;
                    if (envLevel <= sustainLevel)
                    {
                        envLevel = sustainLevel;
                        envStage = sustainLevel <= 0.0001f ? EnvStage::kIdle : EnvStage::kSustain;
                    }
                    break;
                case EnvStage::kSustain:
                    envLevel = sustainLevel;
                    break;
                case EnvStage::kRelease:
                    envLevel -= releaseDec;
                    if (envLevel <= 0.0001f)
                    {
                        envLevel = 0.0f;
                        envStage = EnvStage::kIdle;
                    }
                    break;
                case EnvStage::kIdle:
                    envLevel = 0.0f;
                    break;
            }
            out[n] = static_cast<float>(raw[n]) * kQ15 * envLevel * volume;
        }
    }

    static void createMetadata(const Patch& /*patch*/, Patch::Metadata& metadata);

  private:
    static constexpr int kBlockShift = 7; // 128-sample Tracker blocks

    enum class EnvStage : uint8_t
    {
        kIdle = 0,
        kAttack,
        kDecay,
        kSustain,
        kRelease,
    };

    static float pitchToInc(float midiPitch)
    {
        // 2^32 * f / fs, clamped so a wild bend can never wrap the increment
        constexpr float kPhaseScale = 4294967296.0f / sdk::kSampleRate;
        const float hz = sdk::kNoteA4Freq * std::exp2((midiPitch - static_cast<float>(kNoteA4)) / 12.0f);
        return std::clamp(hz * kPhaseScale, 1.0f, 2147483648.0f);
    }

    void advanceGlide(float samples)
    {
        if (glideSamplesLeft <= 0.0f)
        {
            return;
        }
        const float step = std::min(samples, glideSamplesLeft);
        currentPitch += glideStep * step;
        glideSamplesLeft -= step;
        if (glideSamplesLeft <= 0.0f)
        {
            currentPitch = targetPitch;
        }
    }

    // Parameters
    toro::Controls controls{};
    float attackSec = 0.005f;
    float decaySec = 0.6f;
    float sustainLevel = 0.7f;
    float releaseSec = 0.3f;
    float glideTimeSec = 0.0f;
    float volume = 1.0f;

    // Voice state
    toro::State dsp{};
    const toro::u32 owner;
    EnvStage envStage = EnvStage::kIdle;
    float envLevel = 0.0f;
    bool hasPlayedNote = false;
    float currentPitch = 60.0f;
    float targetPitch = 60.0f;
    float glideStep = 0.0f;
    float glideSamplesLeft = 0.0f;
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
                    [](ToroSynthVoice& voice, Value v) { voice.controls.spread = toKnob(*v); });

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
                    [](ToroSynthVoice& voice, Value v) { voice.controls.shape = toKnob(*v); });

    helper.addParam(id(ToroParamId::kNoise),
                    {
                      .displayName = "Noise",
                      .defaultValue = 0.0f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kOscParam0, 2),
                      .valueFormatter = createPercentFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.controls.noise = toKnob(*v); });

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
                    [](ToroSynthVoice& voice, Value v) { voice.controls.fold = toKnob(*v); });

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
                    [](ToroSynthVoice& voice, Value v) { voice.controls.drive = toKnob(*v); });

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
                    [](ToroSynthVoice& voice, Value v) { voice.controls.aphx = toKnob(*v); });

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
                    [](ToroSynthVoice& voice, Value v) { voice.controls.cutoff = toKnob(*v); });

    helper.addParam(id(ToroParamId::kResonance),
                    {
                      .displayName = "Resonance",
                      .defaultValue = 0.24f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kFilterResonance),
                      .valueFormatter = createPercentFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.controls.res = toKnob(*v); });

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
                    [](ToroSynthVoice& voice, Value v) { voice.attackSec = *v; });

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
                    [](ToroSynthVoice& voice, Value v) { voice.decaySec = *v; });

    helper.addParam(id(ToroParamId::kSustain),
                    {
                      .displayName = "Sustain",
                      .defaultValue = 0.7f,
                      .minValue = 0.0f,
                      .maxValue = 1.0f,
                      .midiCC = cc(MidiCCs::kAmpEnvSustain),
                      .valueFormatter = createPercentFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.sustainLevel = std::clamp(*v, 0.0f, 1.0f); });

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
                    [](ToroSynthVoice& voice, Value v) { voice.releaseSec = *v; });

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
                    [](ToroSynthVoice& voice, Value v)
                    {
                        voice.playMode = static_cast<PlayMode>(std::clamp<int>(
                          static_cast<int>(std::lround(*v)), 0, static_cast<int>(kGlideModeNames.size()) - 1));
                    });

    helper.addParam(id(ToroParamId::kGlideTime),
                    {
                      .displayName = "Glide Time",
                      .defaultValue = 0.0f,
                      .minValue = 0.0f,
                      .maxValue = 2.0f,
                      .editCurve = Curve::kExponential,
                      .valueFormatter = createTimeFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.glideTimeSec = std::max(0.0f, *v); });

    helper.addParam(id(ToroParamId::kVolume),
                    {
                      .displayName = "Volume",
                      .defaultValue = 1.0f,
                      .minValue = 0.0f,
                      .maxValue = 2.0f,
                      .macroOperation = MacroOp::kMult,
                      .valueFormatter = createPercentFormatter(),
                    },
                    [](ToroSynthVoice& voice, Value v) { voice.volume = std::max(0.0f, *v); });

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
