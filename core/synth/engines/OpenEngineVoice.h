#pragma once

// Hosts an open engine (core/synth/open, all three Trackers) as a PolySynth SDK voice on Tracker+ and Mini.
//
// The library does voice allocation, macros, patches, the Model menu and the patch editor; this adapter
// turns its calls into the open engine contract (open/OpenSynthEngine.h):
// - Parameters arrive through the SDK's handlers (setParam), clamped to the engine's range. Each block
//   becomes one SynthParams: start = the previous block's values, end = the current ones, with `moving`
//   and `serial` set when anything changed.
// - The SDK keeps one voice object for good, so the voice's state carries over between notes: after its
//   first note, every note-on is `reused`. `overlap` = a note while another is held, which is exactly the
//   SDK's own legato condition (it also covers the fall-back to a held note when the top one is released).
// - Volume (moved by the Velocity macro at each note-on) is applied as a step, the way the SDK engines apply
//   it. A ramp would fade a new note in from the previous note's velocity.
// - Pitch bend is the SDK's one value per block. The engine's mono output goes to the voice's buffer.
//
// The engine's own SDK file supplies the metadata (names, formatters, MIDI CCs, sections, macros) and a
// handler per parameter that calls setParam().
//
// Every call here comes from the audio interrupt (inside Synth::renderAudioBuffer): no allocation, no I/O.
//
// Copyright (c) 2026 briannhinton. MIT.

#include "PolySynth/sdk/SynthVoiceSDK.h"
#include "open/OpenSynthEngine.h"
#include "open/OpenSynthParams.h"

#include <algorithm>
#include <cstring>

namespace polysynth::sdk
{

template<class VoiceClass, class ParamIdEnum, int32_t kEngine>
class OpenEngineVoice : public CustomSynthVoice<VoiceClass, ParamIdEnum>
{
  public:
    using InstrumentVoiceContext = InstrumentVoice::InstrumentVoiceContext;

    explicit OpenEngineVoice(int voiceIdx)
      : engine(opensynth::openSynthEngine(kEngine))
      , table(opensynth::paramTable(kEngine, &paramCount))
    {
        params.engine = kEngine;
        params.count = table ? paramCount : 0;
        for (int i = 0; i < params.count; i++)
        {
            params.end[i] = table[i].def;
            values[i] = table[i].def;
        }
        std::memcpy(params.start, params.end, sizeof(params.end));
        volumeParam = opensynth::volumeParam(kEngine);
        std::memset(voice.state, 0, sizeof(voice.state));
        voice.slotScratch = &slot;
        voice.index = static_cast<uint8_t>(voiceIdx);
        if (engine)
            engine->slotReset(slot);
    }

    // Called by the engine's parameter handlers (the SDK's onParamChange).
    void setParam(int index, float value)
    {
        if (index >= 0 && index < params.count)
            values[index] = std::clamp(value, table[index].min, table[index].max);
    }

    bool isActive() final { return active; }

    void onNoteOn(const InstrumentVoiceContext& /*ctx*/, note_t note, float velocity, bool /*isLegatoStep*/) final
    {
        if (!engine)
            return;
        syncEnd();
        opensynth::SynthNote n;
        n.pitch = static_cast<float>(note);
        n.note = static_cast<uint8_t>(note);
        n.velocity = velocity;
        n.reused = started;
        n.overlap = gated;
        voice.note = n.note;
        voice.gate = true;
        engine->noteOn(voice, params, n);
        active = true;
        started = true;
        gated = true;
    }

    void onNoteOff(const InstrumentVoiceContext& /*ctx*/, note_t /*note*/) final
    {
        gated = false;
        if (!engine || !active)
            return;
        voice.gate = false;
        engine->noteOff(voice, params);
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          std::optional<AudioBufferStereoRef> /*inputBuffer*/,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> /*auxOutBuffer*/) final
    {
        static_assert(kAudioBufferSize == static_cast<int>(opensynth::kBlock), "one open-engine block per SDK block");
        auto out = outputBuffer[0];
        if (!active)
        {
            std::fill(out.begin(), out.end(), 0.0f);
            return;
        }
        std::memcpy(params.start, params.end, sizeof(params.end));
        syncEnd();
        params.moving = std::memcmp(params.start, params.end, sizeof(float) * params.count) != 0;

        voice.bendStart = this->pitchBendSemitones;
        voice.bend = this->pitchBendSemitones;

        engine->setTempo(ctx.tempo);
        engine->prepare(slot, params);
        float left[opensynth::kBlock] = {};
        float right[opensynth::kBlock] = {};
        const float gain = volumeParam >= 0 ? params.end[volumeParam] : 1.0f;
        active = engine->render(voice, params, gain, 0.0f, left, right);
        std::copy(left, left + opensynth::kBlock, out.begin());
    }

  private:
    // Pending parameter values become the block-end targets; the serial changes when any of them moved.
    void syncEnd()
    {
        if (std::memcmp(params.end, values, sizeof(float) * params.count) != 0)
        {
            std::memcpy(params.end, values, sizeof(float) * params.count);
            params.serial++;
        }
    }

    opensynth::ISynthEngine* const engine;
    int paramCount = 0;
    const opensynth::ParamInfo* const table;
    int volumeParam = -1;
    float values[opensynth::kSynthParamMax] = {};
    opensynth::SynthParams params;
    opensynth::SynthSlotScratch slot;
    opensynth::SynthVoice voice;
    bool active = false;
    bool started = false;
    bool gated = false;
};

} // namespace polysynth::sdk
