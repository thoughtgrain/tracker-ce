// PTFX_NAME: Drum Loop Gen
// PTFX_AUTHOR: Tracker CE
// PTFX_FLAGS: GENERATOR
// PTFX_SCRATCH: 0

// Generates a 16-24 step drum loop (kick, snare, clap, closed and open hat) as one sample.
//
// Workflow: set Style and Density, press Preview, then turn Seed one click and Preview again
// to hear another pattern. Apply renders exactly what you last previewed, because the pattern
// comes only from the parameters (Seed included), never from the host's rand().
//
// The sample is exactly Steps x one 16th long at the chosen BPM, and tails that run past the end
// wrap around to the start, so the loop repeats seamlessly. Every voice is computed in closed
// form from the time since its hit (no per-voice state), which is what makes that wrap possible
// without a buffer: the plugin needs no scratch RAM.

#include "tracker_plugin.h"

#define MAX_STEPS 24
#define VOICES 5
#define MAX_HITS (MAX_STEPS * VOICES)
#define TWO_PI 6.28318530718f

enum { V_KICK, V_SNARE, V_CLAP, V_HAT, V_OPEN };

const PluginParam PLUGIN_PARAMS[] = {
    PTFX_PARAM_INT("Steps", 16.0f, 24.0f, 16.0f, 1.0f, ""),
    PTFX_PARAM_INT("BPM", 60.0f, 200.0f, 120.0f, 1.0f, ""),
    PTFX_PARAM_INT("Seed", 1.0f, 9999.0f, 1.0f, 1.0f, ""),
    PTFX_PARAM_LIST("Style", 0, "Four|Break|Electro|Sparse"),
    PTFX_PARAM_FLOAT("Density", 0.0f, 1.0f, 0.5f, 0.05f, ""),
    PTFX_PARAM_INT("Swing", 0.0f, 60.0f, 0.0f, 1.0f, "%"),
    PTFX_PARAM_FLOAT("Tone", 0.0f, 1.0f, 0.5f, 0.05f, ""),
    PTFX_PARAM_FLOAT("Decay", 0.0f, 1.0f, 0.5f, 0.05f, ""),
    PTFX_PARAM_FLOAT("Hats", 0.0f, 1.0f, 0.5f, 0.05f, ""),
    PTFX_PARAM_FLOAT("Drive", 0.0f, 1.0f, 0.2f, 0.05f, ""),
};

static const PtfxHostApi* s_host = 0;

static int s_steps = 16;
static int s_bpm = 120;
static uint32_t s_seed = 1;
static int s_style = 0;
static float s_density = 0.5f;
static float s_swing = 0.0f;
static float s_tone = 0.5f;
static float s_decay = 0.5f;
static float s_hats = 0.5f;
static float s_drive = 0.2f;

typedef struct {
    uint32_t start; // sample offset of the hit inside the loop
    uint8_t voice;
    float vel;      // 0..1
    uint32_t salt;  // per-hit noise seed
} Hit;

static Hit s_hits[MAX_HITS];
static int s_hit_count = 0;
static uint32_t s_loop_len = 0;
static uint32_t s_pos = 0;
static int s_dirty = 1;

// ---- Deterministic randomness ------------------------------------------------------------

static uint32_t s_rng = 1;

static uint32_t rng_next(void) {
    // xorshift32: small, fast, and identical on every device and on the host test.
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

static float rng_unit(void) { return (float)(rng_next() >> 8) * (1.0f / 16777216.0f); }

// Stateless white noise in -1..1 for sample n of a hit, so a voice needs no running state.
static float noise_at(uint32_t n, uint32_t salt) {
    uint32_t x = n * 0x9E3779B1u ^ salt;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

// ---- Pattern ---------------------------------------------------------------------------------

// Per-style chance (0..100) of each voice on each 16th of a bar. Steps past 16 reuse the bar.
static const uint8_t KICK_P[4][16] = {
    { 100, 0, 5, 0, 100, 0, 10, 0, 100, 0, 5, 10, 100, 0, 10, 5 },  // Four
    { 100, 0, 30, 0, 0, 0, 40, 0, 0, 60, 70, 0, 0, 0, 30, 10 },     // Break
    { 100, 0, 0, 60, 0, 0, 50, 0, 0, 0, 70, 0, 20, 0, 0, 40 },      // Electro
    { 100, 0, 0, 0, 0, 0, 0, 20, 0, 0, 40, 0, 0, 0, 0, 0 },         // Sparse
};
static const uint8_t SNARE_P[4][16] = {
    { 0, 0, 0, 0, 95, 0, 0, 5, 0, 0, 0, 0, 95, 0, 10, 15 },
    { 0, 0, 0, 0, 100, 0, 0, 20, 0, 25, 0, 0, 100, 0, 30, 40 },
    { 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 30 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 90, 0, 0, 0, 0, 0, 0, 10 },
};

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static float step_seconds(void) { return 60.0f / (float)s_bpm / 4.0f; }

static void add_hit(uint32_t start, uint8_t voice, float vel) {
    if (s_hit_count >= MAX_HITS) return;
    s_hits[s_hit_count].start = start;
    s_hits[s_hit_count].voice = voice;
    s_hits[s_hit_count].vel = clampf(vel, 0.0f, 1.0f);
    s_hits[s_hit_count].salt = rng_next();
    s_hit_count++;
}

static void build_pattern(void) {
    const float sr = (float)s_host->sample_rate;
    const float step_len = step_seconds() * sr;
    s_loop_len = (uint32_t)(step_len * (float)s_steps + 0.5f);
    s_hit_count = 0;
    s_rng = (s_seed * 2654435761u) ^ (uint32_t)(s_style * 40503 + 1);
    if (s_rng == 0) s_rng = 1;

    // Density shifts every chance up or down by up to 40 points.
    const float bias = (s_density - 0.5f) * 80.0f;

    for (int i = 0; i < s_steps; i++) {
        const int b = i % 16;
        // Swing delays the off 16ths (2nd and 4th of each beat).
        float t = step_len * (float)i;
        if (i & 1) t += step_len * (s_swing / 100.0f) * 0.5f;
        const uint32_t start = (uint32_t)t;
        const float accent = (b % 4 == 0) ? 1.0f : 0.8f;

        if (rng_unit() * 100.0f < clampf((float)KICK_P[s_style][b] + (b ? bias : 0.0f), 0.0f, 100.0f))
            add_hit(start, V_KICK, accent * (0.85f + 0.15f * rng_unit()));

        const float sn = (float)SNARE_P[s_style][b];
        if (rng_unit() * 100.0f < clampf(sn + (sn >= 90.0f ? 0.0f : bias * 0.5f), 0.0f, 100.0f)) {
            // Backbeats are full; the rest are quieter ghost notes.
            const float vel = sn >= 90.0f ? 1.0f : 0.35f + 0.25f * rng_unit();
            add_hit(start, V_SNARE, vel);
            if (sn >= 90.0f && rng_unit() < 0.35f + s_density * 0.3f) add_hit(start, V_CLAP, 0.8f);
        }

        // Hats: 8ths always eligible, 16ths only as density and Hats rise.
        const float hat_chance = (b % 2 == 0) ? 70.0f + s_hats * 30.0f : (s_hats * 70.0f + bias * 0.5f);
        if (s_hats > 0.0f && rng_unit() * 100.0f < clampf(hat_chance, 0.0f, 100.0f)) {
            const int open = (b % 4 == 2) && rng_unit() < 0.15f + s_hats * 0.35f;
            add_hit(start, open ? V_OPEN : V_HAT, ((b % 2) ? 0.55f : 0.8f) * (0.8f + 0.2f * rng_unit()));
        }
    }
    s_dirty = 0;
}

// ---- Voices (closed form in t seconds since the hit) --------------------------------------

static float voice_kick(float t, uint32_t n, uint32_t salt) {
    // Amplitude time constant: about 0.06 s (tight) to 0.24 s (boomy). Gone after six of them.
    const float decay = 0.06f + s_decay * 0.18f;
    if (t > decay * 6.0f) return 0.0f;
    const float f_end = 40.0f + s_tone * 25.0f;
    const float f_top = f_end + 110.0f + s_tone * 80.0f;
    const float tau = 0.035f;
    // Phase of an exponential pitch sweep, integrated exactly so no running phase is needed.
    const float e = s_host->expf(-t / tau);
    const float phase = TWO_PI * (f_end * t + (f_top - f_end) * tau * (1.0f - e));
    float out = s_host->sinf(phase) * s_host->expf(-t / decay);
    if (t < 0.004f) out += noise_at(n, salt) * 0.4f * (1.0f - t / 0.004f);
    return out;
}

static float voice_snare(float t, uint32_t n, uint32_t salt) {
    const float decay = 0.05f + s_decay * 0.15f;
    if (t > decay * 6.0f) return 0.0f;
    const float f = 160.0f + s_tone * 120.0f;
    const float body = s_host->sinf(TWO_PI * f * t) * s_host->expf(-t / 0.05f) * 0.6f;
    // First difference of white noise tilts it bright without a stateful filter.
    const float hiss = (noise_at(n, salt) - noise_at(n - 1, salt)) * 0.45f;
    return (body + hiss * s_host->expf(-t / decay)) * 0.9f;
}

static float voice_clap(float t, uint32_t n, uint32_t salt) {
    if (t > 0.35f) return 0.0f;
    // Three quick bursts, then a short tail.
    float env;
    if (t < 0.03f) {
        const float ph = t / 0.01f;
        env = 1.0f - (ph - (float)(int)ph);
    } else {
        env = s_host->expf(-(t - 0.03f) / (0.05f + s_decay * 0.08f));
    }
    return (noise_at(n, salt) - noise_at(n - 2, salt)) * 0.5f * env;
}

static float voice_hat(float t, uint32_t n, uint32_t salt, int open) {
    const float decay = open ? 0.12f + s_decay * 0.3f : 0.02f + s_decay * 0.04f;
    if (t > decay * 5.0f) return 0.0f;
    // Second difference of noise: brighter still, closer to a high-passed hat.
    const float x = noise_at(n, salt) - 2.0f * noise_at(n - 1, salt) + noise_at(n - 2, salt);
    return x * 0.22f * (0.7f + s_tone * 0.6f) * s_host->expf(-t / decay);
}

// ---- Plugin entry points --------------------------------------------------------------------

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    s_host = host;
    s_pos = 0;
    s_dirty = 1;
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0: s_steps = (int)clampf(value + 0.5f, 16.0f, (float)MAX_STEPS); break;
        case 1: s_bpm = (int)clampf(value + 0.5f, 60.0f, 200.0f); break;
        case 2: s_seed = (uint32_t)clampf(value + 0.5f, 1.0f, 9999.0f); break;
        case 3: s_style = (int)clampf(value + 0.5f, 0.0f, 3.0f); break;
        case 4: s_density = clampf(value, 0.0f, 1.0f); break;
        case 5: s_swing = clampf(value, 0.0f, 60.0f); break;
        case 6: s_tone = clampf(value, 0.0f, 1.0f); break;
        case 7: s_decay = clampf(value, 0.0f, 1.0f); break;
        case 8: s_hats = clampf(value, 0.0f, 1.0f); break;
        case 9: s_drive = clampf(value, 0.0f, 1.0f); break;
        default: return; // the host also calls this for unused slots
    }
    s_dirty = 1;
    // The output is exactly one loop long; the host reads this after it has set every parameter.
    if (s_host) {
        const uint32_t len = (uint32_t)(step_seconds() * (float)s_host->sample_rate * (float)s_steps + 0.5f);
        s_host->set_extra_samples(0, len);
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    (void)in;
    if (!s_host) return 0;
    if (s_dirty) build_pattern();
    if (s_pos >= s_loop_len) return 0;

    const float sr = (float)s_host->sample_rate;
    const float inv_sr = 1.0f / sr;
    const float gain = 1.0f + s_drive * 3.0f;
    uint32_t todo = samples;
    if (todo > s_loop_len - s_pos) todo = s_loop_len - s_pos;

    for (uint32_t i = 0; i < todo; i++) {
        const uint32_t n = s_pos + i;
        float mix = 0.0f;
        for (int h = 0; h < s_hit_count; h++) {
            const Hit* hit = &s_hits[h];
            // Time since the hit, wrapping so the previous pass's tails continue into this one.
            const uint32_t d = n >= hit->start ? n - hit->start : n + s_loop_len - hit->start;
            const float t = (float)d * inv_sr;
            if (t > 1.5f) continue;
            float v;
            switch (hit->voice) {
                case V_KICK: v = voice_kick(t, d, hit->salt); break;
                case V_SNARE: v = voice_snare(t, d, hit->salt); break;
                case V_CLAP: v = voice_clap(t, d, hit->salt); break;
                case V_HAT: v = voice_hat(t, d, hit->salt, 0); break;
                default: v = voice_hat(t, d, hit->salt, 1); break;
            }
            mix += v * hit->vel;
        }
        // Soft clip: tanh keeps peaks musical as Drive pushes the mix harder.
        const float y = s_host->tanhf(mix * gain * 0.7f);
        const int16_t pcm = (int16_t)SATURATE_16BIT((int32_t)(y * 32000.0f));
        for (uint8_t ch = 0; ch < channels; ch++) out[ch][i] = pcm;
    }
    s_pos += todo;
    return (int32_t)todo;
}
