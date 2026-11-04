// Open synth engine interface.
//
// Ported from OpenTracker (https://github.com/sandlbn/polyend-tracker-re, src/core/audio/synth_engine.h at
// 6c93bbb), Copyright the OpenTracker contributors, licensed under the Apache License 2.0
// (core/synth/open/LICENSE-APACHE-2.0). Changes for Tracker CE: namespace `opensynth`, the sample rate
// follows the device, hot code goes in AUDIO_CORE_RAM, engine ids are CE's own, the test engine and
// the patch-model dependency are dropped. Type names are kept as in OpenTracker so fixes there port over.
//
// Why this exists: Polyend's precompiled synth library only runs on the Cortex-M7 (Tracker+, Mini).
// These engines are plain C++ written for the Original Tracker's Cortex-M4, so a synth built on this
// interface can run on all three devices. Nothing here depends on FEATURE_SYNTH.
//
// The framework (slots, voice pool, macros, pitch FX, gain, mixing) owns everything shared. An engine is
// a stateless singleton: each voice keeps its state in SynthVoice::state, each slot in SynthSlotScratch.
//
// Contract for engines:
//  - Every call comes from the audio interrupt: no allocation, no I/O, no locks, no double.
//  - Parameters arrive in engineering units (enum indices as floats), clamped to their range, with
//    macros applied (SynthParams). Read p.end[i] at block rate, or ramp p.start[i] -> p.end[i] across
//    the block for continuous parameters (p.moving says whether any value changed this block).
//  - The engine's Volume parameter and the note velocity are applied by the framework through the gain
//    ramp passed to render(): engines must NOT apply them.
//  - Pitch: SynthNote::pitch is the note in semitones (MIDI numbering, 69 = A 440 Hz). Patch glide is
//    the engine's job (using SynthNote::overlap); step-FX pitch modulation arrives in SynthVoice::bend
//    (ramp bendStart -> bend over the block). A note with a Glide FX (SynthNote::fxGlide) skips the
//    patch glide.
//  - render() ADDS kBlock frames into L and R (the slot bus), multiplied by g0 + i * gInc, and returns
//    false once the voice is silent for good (the framework frees it). It also stores the amplitude
//    envelope level (0..1) in SynthVoice::level for voice stealing.
//  - An engine with a filter adds SynthVoice::cutoffOct (octaves) to its cutoff, inside its own clamp.
#pragma once

#include <cstddef>
#include <cstdint>

// Hot per-sample kernels go in fast code RAM on IMX (ITCM); on MK66 AUDIO_CORE_RAM is flash. Host tests
// define OPENSYNTH_HOST and get plain functions.
#if defined(OPENSYNTH_HOST)
#define OPENSYNTH_HOT
#else
#include "config.h"
// noinline / noclone as in OpenTracker: an inlined or cloned copy would run from flash instead.
#define OPENSYNTH_HOT AUDIO_CORE_RAM __attribute__((noinline, noclone))
#endif

namespace opensynth {

// The audio block and sample rate the engines are tuned for. They match Tracker.cpp: IMX runs at exactly
// 44.1 kHz, MK66 at 180 MHz * 16 / 255 / 256. Coefficients derive from these at compile time, so each
// device stays in tune.
constexpr uint32_t kBlock = 128;
#if defined(PLATFORM_IMX)
constexpr float kSampleRate = 44100.0f;
#else
constexpr float kSampleRate = 44117.647f;
#endif

// Engines in this host. The values are internal; the instrument type ids patches store are CE's
// InstrumentTypeId values, mapped by the framework.
enum OpenEngineId : int32_t {
    kOpenEngineNone = 0,
    kOpenEngineAcid = 1,   // OpenTracker's ACD
    kOpenEngineSwarm = 2,  // OpenTracker's FAT
    kOpenEnginePrism = 3,  // OpenTracker's VAP
    kOpenEngineGlint = 4,  // OpenTracker's WTFM
    kOpenEngineKit = 5,    // OpenTracker's PERC
    kOpenEngineToro = 6,   // TORO (CE's own; also an SDK engine on Tracker+ and Mini)
};

constexpr int kSynthSlots = 3;               // Synth 1..3
constexpr int kSynthMaxVoices = 8;           // pool records
constexpr int kSynthParamMax = 84;           // the largest engine (KIT)
constexpr int kSynthVoiceStateBytes = 128;   // engine state per voice
constexpr int kSynthSlotScratchBytes = 384;  // engine state per slot

// Effective parameters of one slot for the current block.
struct SynthParams {
    int32_t engine = 0;
    int count = 0;                     // parameters of the engine
    bool moving = false;               // some start[i] != end[i] this block
    uint32_t serial = 0;               // changes whenever an end[] value changes
    float start[kSynthParamMax] = {};  // value at the block start
    float end[kSynthParamMax] = {};    // value at the block end (the target)
    // Ramp helper: start value and per-frame increment of parameter i.
    float ramp(int i, float& inc) const {
        inc = (end[i] - start[i]) * (1.0f / float(kBlock));
        return start[i];
    }
};

// One note start.
struct SynthNote {
    float pitch = 60.0f;    // semitones; the note (Tune FX not included: see bend)
    float velocity = 1.0f;  // 0..1
    uint8_t note = 60;      // 0..127
    // True when a still-gated voice of a polyphony-1 slot is reused for the new note (legato / Overlap
    // glide modes). A reused voice that was in its release has overlap = false and reused = true.
    bool overlap = false;
    bool reused = false;
    // A Glide step FX owns this note's pitch start: the framework's bend starts at the track's previous
    // pitch, so the engine must not add its patch glide on top.
    bool fxGlide = false;
};

// Per-slot engine scratch (per-block coefficient caches, the drum kit's shared state).
struct alignas(8) SynthSlotScratch {
    // Zeroed: engines may keep fields across slotReset() and must never read what the memory held before.
    uint8_t bytes[kSynthSlotScratchBytes] = {};
    template <class T>
    T& as() {
        static_assert(sizeof(T) <= kSynthSlotScratchBytes, "slot state too large");
        return *reinterpret_cast<T*>(bytes);
    }
};

// One pool voice as an engine sees it. Fields above `state` are written by the framework (read-only for
// engines) except `level`.
struct alignas(8) SynthVoice {
    SynthSlotScratch* slotScratch = nullptr;  // the owning slot's scratch
    float bend = 0.0f;       // step-FX pitch offset at the block end (semitones)
    float bendStart = 0.0f;  // ... at the block start
    float level = 0.0f;      // engine: amplitude-envelope level 0..1
    uint8_t slot = 0;        // 0..2
    uint8_t index = 0;       // the host's number for this voice (stable for the voice's life): per-voice seeds
    uint8_t note = 0;
    bool gate = false;       // key held (false after noteOff)
    // This block is the voice's last (stolen / cut: the gain ramp falls to 0 and the record is freed
    // after it). Engines that keep state beyond the voice (the kit) release it here.
    bool fading = false;
    float cutoffOct = 0.0f;  // Performance filter offset of the note's track, octaves
    alignas(8) uint8_t state[kSynthVoiceStateBytes];
    template <class T>
    T& as() {
        static_assert(sizeof(T) <= kSynthVoiceStateBytes, "voice state too large");
        return *reinterpret_cast<T*>(state);
    }
};

class ISynthEngine {
public:
    virtual ~ISynthEngine() = default;
    virtual int32_t id() const = 0;        // OpenEngineId
    virtual const char* name() const = 0;  // "ACID", ...
    // Once per block for every slot with sounding voices, before its voices render. Optional.
    virtual void prepare(SynthSlotScratch& slot, const SynthParams& p) { (void)slot; (void)p; }
    // A slot starts using this engine (patch / model change, project load).
    virtual void slotReset(SynthSlotScratch& slot) { (void)slot; }
    // Song tempo in BPM, once per block before prepare(). Optional.
    virtual void setTempo(float bpm) { (void)bpm; }
    // Per-voice send outputs (the kit's per-drum delay / reverb sends). Called once per block before the
    // slot's voices render, and with nullptr after them. Returns false when the engine has none. Optional.
    virtual bool setSendTap(int slot, float* delay, float* reverb, float gainD, float gainR) {
        (void)slot; (void)delay; (void)reverb; (void)gainD; (void)gainR;
        return false;
    }
    // Start a note. A fresh voice has zeroed state; a reused one (n.reused) keeps its state.
    virtual void noteOn(SynthVoice& v, const SynthParams& p, const SynthNote& n) = 0;
    // Gate off: enter the release.
    virtual void noteOff(SynthVoice& v, const SynthParams& p) = 0;
    // Add one block into L / R (see the contract above). false = finished.
    virtual bool render(SynthVoice& v, const SynthParams& p, float g0, float gInc, float* L, float* R) = 0;
};

// The engine for an id, or nullptr when this build doesn't have it.
ISynthEngine* openSynthEngine(int32_t id);

}  // namespace opensynth
