// TORO as an open engine (see Toro.h).
//
// Everything below mirrors ToroSynthVoice.cpp: note-on (glide decision, envelope retrigger), the per-block
// pitch ramp, toro::render<7> and the per-sample ADSR, with the same expressions in the same order, so the
// two builds render the same floats. What differs is only what the open interface requires:
//  - Volume and velocity are applied by the host through the gain ramp passed to render() (the engine
//    contract); with a flat gain equal to Volume the output is identical to the SDK voice.
//  - A fresh voice starts zeroed, so "the previous note" for glide comes from the slot (Slot::lastPitch)
//    rather than from a voice object that lives forever. For one voice that's the same thing; with
//    polyphony, every voice glides from the slot's last note.
//  - The noise seed comes from SynthVoice::index, the host's voice number, as the SDK voice's comes from
//    its voice index (owner = index + 1).
//  - Pitch bend arrives as SynthVoice::bendStart -> bend over the block (the SDK has one value per block).
//  - SynthVoice::cutoffOct (the Performance filter offset) isn't applied yet: ToroCore.h has no cutoff input
//    besides its knob, and changing the core would break its OP-XY parity.
//
// Copyright (c) 2026 briannhinton. MIT.
#include "open/engines/Toro.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "open/engines/ToroCore.h"

namespace opensynth {

namespace {

using namespace toro_engine;
namespace core = polysynth::toro;

static_assert(kBlock == 128, "toro renders exactly one Tracker block (render<7>)");
constexpr int kBlockShift = 7;
constexpr float kNoteA4Freq = 440.0f;
constexpr float kNoteA4 = 69.0f;

// Prefixed kEnv: toro_engine::Param already has kAttack, kDecay, kSustain and kRelease.
enum EnvStage : uint8_t { kEnvIdle = 0, kEnvAttack, kEnvDecay, kEnvSustain, kEnvRelease };

struct Voice {
    core::State dsp;
    float envLevel;
    float currentPitch;
    float targetPitch;
    float glideStep;
    float glideSamplesLeft;
    uint8_t envStage;
    bool hasPlayedNote;
};

struct Slot {
    float lastPitch;  // the slot's last note, for glide from a fresh voice
    bool hasLast;
};

// 0..1 parameter -> toro's 0..32767 knob scale (as ToroSynthVoice's toKnob)
core::i32 toKnob(float v) {
    return static_cast<core::i32>(std::lround(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(core::kKnobMax)));
}

float pitchToInc(float midiPitch) {
    // 2^32 * f / fs, clamped so a wild bend can never wrap the increment
    constexpr float kPhaseScale = 4294967296.0f / kSampleRate;
    const float hz = kNoteA4Freq * std::exp2((midiPitch - kNoteA4) / 12.0f);
    return std::clamp(hz * kPhaseScale, 1.0f, 2147483648.0f);
}

void advanceGlide(Voice& a, float samples) {
    if (a.glideSamplesLeft <= 0.0f) return;
    const float step = std::min(samples, a.glideSamplesLeft);
    a.currentPitch += a.glideStep * step;
    a.glideSamplesLeft -= step;
    if (a.glideSamplesLeft <= 0.0f) a.currentPitch = a.targetPitch;
}

core::Controls controlsOf(const SynthParams& p) {
    core::Controls c;
    c.spread = toKnob(p.end[kSpread]);
    c.shape = toKnob(p.end[kShape]);
    c.noise = toKnob(p.end[kNoise]);
    c.fold = toKnob(p.end[kFold]);
    c.drive = toKnob(p.end[kDrive]);
    c.aphx = toKnob(p.end[kAphx]);
    c.cutoff = toKnob(p.end[kCutoff]);
    c.res = toKnob(p.end[kResonance]);
    return c;
}

class ToroEngine final : public ISynthEngine {
public:
    int32_t id() const override { return kOpenEngineToro; }
    const char* name() const override { return "TORO"; }

    void slotReset(SynthSlotScratch& scratch) override {
        Slot& s = scratch.as<Slot>();
        std::memset(&s, 0, sizeof(s));
    }

    void noteOn(SynthVoice& v, const SynthParams& p, const SynthNote& n) override {
        if (p.count < kParamCount) return;
        Voice& a = v.as<Voice>();
        Slot& s = v.slotScratch->as<Slot>();
        if (!n.reused) {
            // A fresh voice: its "previous note" is the slot's.
            a.hasPlayedNote = s.hasLast;
            a.currentPitch = s.hasLast ? s.lastPitch : n.pitch;
        }
        const int mode = std::clamp(static_cast<int>(std::lround(p.end[kGlideMode])), 0, 2);
        const float glideTimeSec = std::max(0.0f, p.end[kGlideTime]);
        const float target = n.pitch;
        // Overlap: this voice is still holding a note (attack, decay or sustain).
        const bool overlapping = a.envStage != kEnvIdle && a.envStage != kEnvRelease;
        bool glide = a.hasPlayedNote && glideTimeSec > 0.001f && !n.fxGlide;
        if (mode == kOverlap || mode == kLegato) glide = glide && overlapping;

        if (glide) {
            a.glideSamplesLeft = std::max(1.0f, glideTimeSec * kSampleRate);
            a.glideStep = (target - a.currentPitch) / a.glideSamplesLeft;
        } else {
            a.currentPitch = target;
            a.glideSamplesLeft = 0.0f;
        }
        a.targetPitch = target;
        a.hasPlayedNote = true;
        s.lastPitch = target;
        s.hasLast = true;

        // A voice coming out of silence starts toro fresh (phases, filter, no cutoff sweep). A voice that is
        // still sounding keeps its state, so a retrigger or a steal doesn't click.
        if (a.envStage == kEnvIdle) {
            core::reset(a.dsp, 1u + static_cast<core::u32>(v.index));
            a.envLevel = 0.0f;
        }
        // The SDK's legato step: a note while another is held, in Legato mode.
        const bool legatoStep = mode == kLegato && n.overlap;
        if (!legatoStep || a.envStage == kEnvIdle || a.envStage == kEnvRelease) a.envStage = kEnvAttack;
    }

    void noteOff(SynthVoice& v, const SynthParams& /*p*/) override {
        Voice& a = v.as<Voice>();
        if (a.envStage != kEnvIdle) a.envStage = kEnvRelease;
    }

    bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* L, float* R) override;
};

OPENSYNTH_HOT bool ToroEngine::render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* L, float* R) {
    Voice& a = v.as<Voice>();
    if (a.envStage == kEnvIdle || p.count < kParamCount) {
        v.level = 0.0f;
        return false;
    }

    // Pitch: one phase increment per sample, so glides and bends move smoothly inside the block.
    const float pitchStart = a.currentPitch;
    advanceGlide(a, static_cast<float>(kBlock));
    const float incStart = pitchToInc(pitchStart + v.bendStart);
    const float incEnd = pitchToInc(a.currentPitch + v.bend);
    const float incStep = (incEnd - incStart) / static_cast<float>(kBlock);
    core::u32 inc[kBlock];
    for (uint32_t n = 0; n < kBlock; n++) inc[n] = static_cast<core::u32>(incStart + incStep * static_cast<float>(n));

    const core::Controls controls = controlsOf(p);
    core::i32 raw[kBlock];
    core::render<kBlockShift>(a.dsp, controls, inc, raw);

    // Envelope, per sample
    const float attackSec = p.end[kAttack];
    const float decaySec = p.end[kDecay];
    const float sustainLevel = std::clamp(p.end[kSustain], 0.0f, 1.0f);
    const float releaseSec = p.end[kRelease];
    const float sr = kSampleRate;
    const float attackInc = attackSec > 0.001f ? 1.0f / (attackSec * sr) : 1.0f;
    const float decayDec = decaySec > 0.002f ? (1.0f - sustainLevel) / (decaySec * sr) : 1.0f;
    const float releaseDec = releaseSec > 0.002f ? 1.0f / (releaseSec * sr) : 1.0f;
    constexpr float kQ15 = 1.0f / 32768.0f;

    float envLevel = a.envLevel;
    uint8_t envStage = a.envStage;
    for (uint32_t n = 0; n < kBlock; n++) {
        switch (envStage) {
            case kEnvAttack:
                envLevel += attackInc;
                if (envLevel >= 1.0f) {
                    envLevel = 1.0f;
                    envStage = kEnvDecay;
                }
                break;
            case kEnvDecay:
                envLevel -= decayDec;
                if (envLevel <= sustainLevel) {
                    envLevel = sustainLevel;
                    envStage = sustainLevel <= 0.0001f ? kEnvIdle : kEnvSustain;
                }
                break;
            case kEnvSustain:
                envLevel = sustainLevel;
                break;
            case kEnvRelease:
                envLevel -= releaseDec;
                if (envLevel <= 0.0001f) {
                    envLevel = 0.0f;
                    envStage = kEnvIdle;
                }
                break;
            default:
                envLevel = 0.0f;
                break;
        }
        const float y = static_cast<float>(raw[n]) * kQ15 * envLevel * (g0 + gInc * static_cast<float>(n));
        L[n] += y;
        R[n] += y;
    }
    a.envLevel = envLevel;
    a.envStage = envStage;
    v.level = envLevel;
    return envStage != kEnvIdle;
}

ToroEngine gToro;

}  // namespace

ISynthEngine* toroEngine() { return &gToro; }

}  // namespace opensynth
