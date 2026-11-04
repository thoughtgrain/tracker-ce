#pragma once

// The SDK side of the TORO host tests: a stub Patch, the real TORO InstrumentType, and helpers that drive a
// voice the way PolySynth does (MIDI events in, one 128-sample block per render). Shared by voice_test.cpp
// and open_parity_test.cpp so both drive the SDK voice identically.
//
// Copyright (c) 2026 briannhinton. MIT.

#include "ToroSynthVoice.h"

#include "PolySynth/sdk/SynthVoiceSDK.h"
#include "stub_patch.h"

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace
{

using namespace polysynth;

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

inline void noteOn(InstrumentVoice& v, int note, int vel = 100)
{
    MidiEvent e{};
    e.type = MidiEvent::Type::kNoteOn;
    e.data.note = { static_cast<uint8_t>(note), static_cast<uint8_t>(vel) };
    v.onMidiEvent(kCtx, e);
}

inline void noteOff(InstrumentVoice& v, int note)
{
    MidiEvent e{};
    e.type = MidiEvent::Type::kNoteOff;
    e.data.note = { static_cast<uint8_t>(note), 0 };
    v.onMidiEvent(kCtx, e);
}

inline void bend(InstrumentVoice& v, int16_t value)
{
    MidiEvent e{};
    e.type = MidiEvent::Type::kPitchBendChange;
    e.data.pitchBendChange = { value };
    v.onMidiEvent(kCtx, e);
}

inline std::vector<float> render(InstrumentVoice& v, int blocks, float prefill = 0.0f)
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

} // namespace
