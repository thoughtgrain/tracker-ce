// PTFX_NAME: Synth Loop Gen
// PTFX_AUTHOR: Tracker CE
// PTFX_FLAGS: GENERATOR
// PTFX_SCRATCH: 8192

// Generates a 16-24 step musical loop as one sample: a melody, an arpeggio, held chords or a
// drone, in any key and scale, with or without chords.
//
// Style blends the sound continuously:
//   0   = SH-101: one oscillator (saw + pulse + sub) into a squelchy resonant filter, snappy
//         envelopes;
//   50  = Juno: softer filter, slower envelopes, PWM and chorus;
//   100 = piano: inharmonic partials that die away high-first, a hammer thump and a damper.
// Values in between mix the neighbours (below 50 the filter and envelopes move from SH-101 to
// Juno; above 50 the Juno crossfades into the piano).
//
// APHX gives every note its own fixed drift, the same each time that key plays: tuning (a few
// cents at 25, semitones apart at 100), filter brightness, piano stretch, and a per-note
// sample-rate and bit crush. Low settings are old-synth character; high ones are broken and
// crushed. Notes are generated in octave 3, with Root setting the key.
//
// Workflow: pick Mode, Root, Scale and Chords, press Preview, then turn Seed one click and Preview
// again for another phrase. Apply renders exactly the last Preview: everything comes from the
// parameters, never from the host's rand().
//
// The sample is exactly Steps sixteenths long at BPM. Notes still sounding at the end are carried
// into the start (their voices are run forward first), so the loop repeats without a gap.

#include "tracker_plugin.h"

#define MAX_STEPS 24
#define MAX_NOTES 128
#define MAX_VOICES 48
#define PARTIALS 8
#define CHORUS_LEN 2048 // floats, in scratch: about 46 ms at 44.1 kHz
#define CTRL_RATE 32    // filter coefficients are updated every 32 samples
#define TAIL_PASSES 3   // a release can carry over at most this many loops (6 s loops: 18 s)

enum { MODE_SEQ, MODE_ARP, MODE_SUSTAIN, MODE_DRONE };
enum { CH_OFF, CH_TRIAD, CH_7TH, CH_SUS, CH_POWER, CH_OCTAVE };

const PluginParam PLUGIN_PARAMS[] = {
    PTFX_PARAM_LIST("Mode", 0, "Seq|Arp|Sustain|Drone"),
    PTFX_PARAM_INT("Style", 0.0f, 100.0f, 50.0f, 1.0f, ""),
    PTFX_PARAM_INT("Steps", 16.0f, 24.0f, 16.0f, 1.0f, ""),
    PTFX_PARAM_INT("BPM", 60.0f, 200.0f, 120.0f, 1.0f, ""),
    PTFX_PARAM_INT("Seed", 1.0f, 9999.0f, 1.0f, 1.0f, ""),
    PTFX_PARAM_LIST("Root", 0, "C|C#|D|D#|E|F|F#|G|G#|A|A#|B"),
    PTFX_PARAM_LIST("Scale", 1, "Major|Minor|Dorian|Phryg|Lydian|Mixolyd|PentMaj|PentMin|Blues|Chromat"),
    PTFX_PARAM_LIST("Chords", 0, "Off|Triad|7th|Sus|Power|Octave"),
    PTFX_PARAM_INT("APHX", 0.0f, 100.0f, 0.0f, 1.0f, ""),
    PTFX_PARAM_FLOAT("Density", 0.0f, 1.0f, 0.6f, 0.05f, ""),
    PTFX_PARAM_FLOAT("Length", 0.0f, 1.0f, 0.4f, 0.05f, ""),
    PTFX_PARAM_FLOAT("Tone", 0.0f, 1.0f, 0.5f, 0.05f, ""),
};

static const PtfxHostApi* s_host = 0;
static float* s_chorus = 0;

static int s_mode = MODE_SEQ;
static float s_style = 0.5f;
static int s_steps = 16;
static int s_bpm = 120;
static uint32_t s_seed = 1;
static int s_root = 0;
static int s_scale = 1;
static int s_chords = CH_OFF;
static const int s_octave = 3; // APHX took Octave's control slot; Root still sets the key
static float s_aphx = 0.0f;
static float s_density = 0.6f;
static float s_length = 0.4f;
static float s_tone = 0.5f;

// ---- Small maths ----------------------------------------------------------------------------

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float lerpf(float a, float b, float x) { return a + (b - a) * x; }
static float fabsf_(float x) { return x < 0.0f ? -x : x; }

// sin(2*pi*x) for a phase x in [0, 1). A refined parabola: cheap enough for eight piano partials
// per voice on the Original's Cortex-M4, and accurate to about 0.1 %.
static float sin01(float x) {
    const float t = 2.0f * x - 1.0f; // sin(pi*t) = -sin(2*pi*x)
    float y = 4.0f * t * (1.0f - fabsf_(t));
    y = 0.225f * (y * fabsf_(y) - y) + y;
    return -y;
}

static uint32_t s_rng = 1;
static uint32_t rng_next(void) {
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}
static float rng_unit(void) { return (float)(rng_next() >> 8) * (1.0f / 16777216.0f); }
static int rng_int(int n) { return (int)(rng_unit() * (float)n) % n; }

static float noise_at(uint32_t n, uint32_t salt) {
    uint32_t x = n * 0x9E3779B1u ^ salt;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

// ---- Scales and chords -------------------------------------------------------------------------

static const uint8_t SCALES[10][12] = {
    { 0, 2, 4, 5, 7, 9, 11 },       // Major
    { 0, 2, 3, 5, 7, 8, 10 },       // Minor
    { 0, 2, 3, 5, 7, 9, 10 },       // Dorian
    { 0, 1, 3, 5, 7, 8, 10 },       // Phrygian
    { 0, 2, 4, 6, 7, 9, 11 },       // Lydian
    { 0, 2, 4, 5, 7, 9, 10 },       // Mixolydian
    { 0, 2, 4, 7, 9 },              // Major pentatonic
    { 0, 3, 5, 7, 10 },             // Minor pentatonic
    { 0, 3, 5, 6, 7, 10 },          // Blues
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, // Chromatic
};
static const uint8_t SCALE_LEN[10] = { 7, 7, 7, 7, 7, 7, 5, 5, 6, 12 };

// MIDI note for a scale degree (degrees past the scale continue into the next octaves).
static int degree_note(int deg) {
    const int len = SCALE_LEN[s_scale];
    int oct = deg / len, idx = deg % len;
    if (idx < 0) { idx += len; oct -= 1; }
    return 12 * (s_octave + 1) + s_root + 12 * oct + SCALES[s_scale][idx];
}

// Chord tones on a degree, as note numbers. Returns how many (1-4).
static int chord_notes(int deg, int kind, int* out) {
    const int root = degree_note(deg);
    switch (kind) {
        case CH_TRIAD: out[0] = root; out[1] = degree_note(deg + 2); out[2] = degree_note(deg + 4); return 3;
        case CH_7TH: out[0] = root; out[1] = degree_note(deg + 2); out[2] = degree_note(deg + 4); out[3] = degree_note(deg + 6); return 4;
        case CH_SUS: out[0] = root; out[1] = degree_note(deg + 3); out[2] = degree_note(deg + 4); return 3;
        case CH_POWER:
            // The scale's own fifth keeps it in key; only the chromatic scale uses a plain fifth.
            out[0] = root;
            out[1] = SCALE_LEN[s_scale] == 12 ? root + 7 : degree_note(deg + 4);
            out[2] = root + 12;
            return 3;
        case CH_OCTAVE: out[0] = root; out[1] = root + 12; return 2;
        default: out[0] = root; return 1;
    }
}

// ---- Pattern --------------------------------------------------------------------------------

typedef struct {
    uint32_t start; // sample offset inside the loop
    uint32_t gate;  // samples until release
    int note;       // MIDI note
    float vel;      // 0..1
} Note;

static Note s_notes[MAX_NOTES];
static int s_note_count = 0;
static uint32_t s_loop_len = 0;
static float s_step_len = 0.0f;

static float step_seconds(void) { return 60.0f / (float)s_bpm / 4.0f; }

static void add_note(int step, float steps_long, int note, float vel) {
    if (s_note_count >= MAX_NOTES || note < 12 || note > 108) return;
    Note* n = &s_notes[s_note_count++];
    n->start = (uint32_t)((float)step * s_step_len);
    float g = steps_long * s_step_len;
    if (g < 64.0f) g = 64.0f;
    n->gate = (uint32_t)g;
    n->note = note;
    n->vel = clampf(vel, 0.0f, 1.0f);
}

static void add_chord(int step, float steps_long, int deg, int kind, float vel) {
    int notes[4];
    const int count = chord_notes(deg, kind, notes);
    // Quieter per note as chords get bigger, so a 7th chord isn't four times louder.
    const float each = vel * (count > 1 ? 0.75f : 1.0f);
    for (int i = 0; i < count; i++) add_note(step, steps_long, notes[i], each);
}

// A short progression of chord roots, as scale degrees, chosen by the seed.
static int progression_degree(int bar, int bars) {
    static const int8_t PROGS[6][4] = {
        { 0, 3, 4, 0 }, { 0, 5, 3, 4 }, { 0, 4, 5, 3 }, { 0, 2, 3, 4 }, { 0, 0, 3, 3 }, { 0, 6, 5, 4 },
    };
    static int prog = 0;
    if (bar == 0) prog = rng_int(6);
    (void)bars;
    return PROGS[prog][bar % 4];
}

static void build_pattern(void) {
    const float sr = (float)s_host->sample_rate;
    s_step_len = step_seconds() * sr;
    s_loop_len = (uint32_t)(s_step_len * (float)s_steps + 0.5f);
    s_note_count = 0;
    s_rng = (s_seed * 2654435761u) ^ (uint32_t)(s_mode * 7919 + s_scale * 104729 + 1);
    if (s_rng == 0) s_rng = 1;
    const int len = SCALE_LEN[s_scale];
    const int range = len * 2; // two octaves of melody

    if (s_mode == MODE_SEQ) {
        int deg = 0;
        for (int i = 0; i < s_steps; i++) {
            const int on_beat = (i % 4) == 0;
            const float chance = i == 0 ? 1.0f : 0.15f + s_density * 0.8f + (on_beat ? 0.1f : -0.05f);
            if (rng_unit() >= chance) continue;
            // A gentle random walk that leans towards the root and fifth.
            const int r = rng_int(10);
            const int step = r < 2 ? -2 : r < 4 ? -1 : r < 6 ? 1 : r < 8 ? 2 : (rng_unit() < 0.5f ? 0 : len);
            deg += step;
            if (deg < 0) deg += len;
            if (deg >= range) deg -= len;
            if (rng_unit() < 0.2f) deg = (rng_unit() < 0.5f) ? 0 : (len >= 5 ? 4 : 0);
            const float steps_long = 1.0f + s_length * 3.0f * rng_unit() + (s_length > 0.8f ? 1.0f : 0.0f);
            const float vel = (on_beat ? 0.95f : 0.75f) * (0.85f + 0.15f * rng_unit());
            if (s_chords == CH_OFF) add_note(i, steps_long, degree_note(deg), vel);
            else add_chord(i, steps_long, deg, s_chords, vel);
        }
    } else if (s_mode == MODE_ARP) {
        const int seg = s_density > 0.5f ? 4 : 8; // how often the chord changes
        int notes[4];
        int count = 0, idx = 0, bar = -1;
        for (int i = 0; i < s_steps; i++) {
            if (i / seg != bar) {
                bar = i / seg;
                const int deg = progression_degree(bar, s_steps / seg);
                if (s_chords == CH_OFF) {
                    // No chords: arpeggiate a run of four scale notes from the progression's root.
                    for (int k = 0; k < 4; k++) notes[k] = degree_note(deg + k);
                    count = 4;
                } else {
                    count = chord_notes(deg, s_chords, notes);
                }
                idx = 0;
            }
            // Two-octave arp, up then down. Low density skips some steps.
            if (i != 0 && rng_unit() > 0.35f + s_density * 0.65f) continue;
            // Ping-pong over the chord tones across two octaves: up, then back down.
            const int span = count * 2;
            const int period = span > 1 ? 2 * (span - 1) : 1;
            const int m = idx % period;
            const int k = m < span ? m : period - m;
            const int note = notes[k % count] + 12 * (k / count);
            add_note(i, 0.5f + s_length * 1.5f, note, (i % 4 == 0 ? 0.95f : 0.75f));
            idx++;
        }
    } else if (s_mode == MODE_SUSTAIN) {
        // Held chords: the denser, the more often they change.
        const int seg = s_density > 0.66f ? 4 : (s_density > 0.33f ? 8 : 16);
        for (int i = 0, bar = 0; i < s_steps; i += seg, bar++) {
            const int seg_len = (i + seg > s_steps) ? s_steps - i : seg;
            const float held = (float)seg_len * (0.6f + s_length * 0.5f);
            const int kind = s_chords == CH_OFF ? CH_OFF : s_chords;
            add_chord(i, held, progression_degree(bar, s_steps / seg), kind, 0.85f);
        }
    } else {
        // Drone: one sound for the whole loop. Without chords, a root with a fifth on top.
        const float held = (float)s_steps;
        if (s_chords == CH_OFF) {
            add_note(0, held, degree_note(0), 0.9f);
            if (rng_unit() < 0.7f) add_note(0, held, degree_note(0) + 7, 0.6f);
        } else {
            add_chord(0, held, 0, s_chords, 0.85f);
        }
        if (rng_unit() < 0.5f) add_note(0, held, degree_note(0) - 12, 0.7f); // sub octave
    }
}

// ---- Style ----------------------------------------------------------------------------------

typedef struct {
    float saw, pulse, sub;             // oscillator mix
    float pw, pwm;                     // pulse width and PWM depth
    float cutoff, env_mod, res;        // filter (Hz, Hz, 0..1)
    float a, d, s, r;                  // amp envelope (seconds, level)
    float fdecay;                      // filter envelope decay (s)
    float chorus;                      // chorus wet mix
    float piano;                       // 0 = all subtractive, 1 = all piano
} StyleParams;

static StyleParams s_sp;

static void compute_style(void) {
    // Anchors: SH-101 (0), Juno (0.5). Above 0.5 the Juno settings hold while the piano fades in.
    const float x = s_style < 0.5f ? s_style * 2.0f : 1.0f;
    const float pad = (s_mode == MODE_SUSTAIN || s_mode == MODE_DRONE) ? 1.0f : 0.0f;
    StyleParams* p = &s_sp;
    p->saw = lerpf(1.0f, 0.8f, x);
    p->pulse = lerpf(0.5f, 0.6f, x);
    p->sub = lerpf(0.6f, 0.3f, x);
    p->pw = lerpf(0.5f, 0.35f, x);
    p->pwm = lerpf(0.0f, 0.12f, x);
    p->cutoff = lerpf(250.0f, 700.0f, x) * (0.5f + s_tone * 4.0f);
    p->env_mod = lerpf(4500.0f, 1800.0f, x) * (0.6f + s_tone * 0.8f);
    p->res = lerpf(0.72f, 0.25f, x);
    p->a = lerpf(0.002f, 0.02f, x) + pad * (0.15f + s_length * 0.8f);
    p->d = lerpf(0.18f, 0.6f, x);
    p->s = lerpf(0.35f, 0.7f, x) + pad * 0.2f;
    p->r = lerpf(0.05f, 0.4f, x) + pad * (0.3f + s_length);
    p->fdecay = lerpf(0.12f, 0.45f, x) + pad * 0.6f;
    p->chorus = lerpf(0.0f, 0.55f, x);
    p->piano = s_style > 0.5f ? (s_style - 0.5f) * 2.0f : 0.0f;
}

// ---- Voices ---------------------------------------------------------------------------------

typedef struct {
    int active;
    int released;
    uint32_t age;      // samples since note on
    uint32_t gate;     // samples until release
    float freq, vel;
    uint32_t salt;
    // subtractive
    float ph_saw, ph_pulse, ph_sub, ph_lfo;
    float lp1, bp1, lp2, bp2; // two Chamberlin state-variable filters in series
    float f_coef, q_coef;
    float env, fenv;          // amp and filter envelopes
    // piano
    float pph[PARTIALS], pinc[PARTIALS], pamp[PARTIALS], pdec[PARTIALS];
    float damp;               // damper after release
    // APHX: this key's drift
    float cut_mult;           // filter cutoff multiplier
    uint32_t hold;            // sample-rate crush: hold each output for this many samples (1 = off)
    float levels;             // bit crush: quantisation levels (0 = off)
    float held;               // last held output
} Voice;

static Voice s_voices[MAX_VOICES];

static float note_freq(int note) { return 440.0f * s_host->powf(2.0f, (float)(note - 69) / 12.0f); }

static Voice* alloc_voice(void) {
    Voice* best = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!s_voices[i].active) return &s_voices[i];
        // Steal the quietest released voice, or failing that the oldest one.
        if (!best || (s_voices[i].released && s_voices[i].env < best->env) || (!best->released && s_voices[i].age > best->age))
            best = &s_voices[i];
    }
    return best;
}

// A fixed value in -1..1 for this key and seed: APHX drift is the same every time a key plays.
static float key_drift(int note, uint32_t which) {
    return noise_at((uint32_t)note * 131u + which, s_seed * 0x2545F491u);
}

static void voice_start(Voice* v, const Note* n, uint32_t salt) {
    const float sr = (float)s_host->sample_rate;
    const float a = s_aphx, a2 = s_aphx * s_aphx;
    v->active = 1;
    v->released = 0;
    v->age = 0;
    v->gate = n->gate;
    // APHX tuning: up to +-3 semitones at full, a few cents at 25 (it grows with the square).
    v->freq = note_freq(n->note) * (a > 0.0f ? s_host->powf(2.0f, key_drift(n->note, 1) * a2 * 3.0f / 12.0f) : 1.0f);
    v->cut_mult = a > 0.0f ? s_host->powf(2.0f, key_drift(n->note, 2) * a * 2.0f) : 1.0f;
    {
        const float h = 0.5f + 0.5f * key_drift(n->note, 3);
        v->hold = 1u + (uint32_t)(a2 * h * 24.0f);
        const float bits = 16.0f - a2 * (0.5f + 0.5f * key_drift(n->note, 4)) * 12.0f;
        v->levels = a > 0.0f ? s_host->powf(2.0f, bits - 1.0f) : 0.0f;
    }
    v->held = 0.0f;
    v->vel = n->vel;
    v->salt = salt;
    v->ph_saw = 0.0f;
    v->ph_pulse = 0.25f;
    v->ph_sub = 0.0f;
    v->ph_lfo = (float)(salt & 1023) / 1024.0f;
    v->lp1 = v->bp1 = v->lp2 = v->bp2 = 0.0f;
    v->f_coef = 0.1f;
    v->q_coef = 1.0f;
    v->env = 0.0f;
    v->fenv = 1.0f;
    v->damp = 1.0f;
    // Piano partials: stretched (inharmonic) and dying away faster the higher they are.
    const float bright = 1.2f - s_tone * 0.6f - v->vel * 0.2f;
    const float t0 = clampf(3.0f - (float)(n->note - 48) * 0.04f, 0.6f, 4.0f);
    for (int k = 0; k < PARTIALS; k++) {
        const float kk = (float)(k + 1);
        const float inharm = 0.0004f * (1.0f + a2 * 40.0f * (0.5f + 0.5f * key_drift(n->note, 5)));
        const float stretch = s_host->sqrtf(1.0f + inharm * kk * kk);
        const float f = v->freq * kk * stretch;
        v->pph[k] = 0.0f;
        v->pinc[k] = f < sr * 0.45f ? f / sr : 0.0f; // drop partials above Nyquist
        v->pamp[k] = s_host->powf(1.0f / kk, bright);
        v->pdec[k] = s_host->expf(-1.0f / ((t0 / (1.0f + 0.6f * (kk - 1.0f))) * sr));
    }
}

// Advance one voice one sample and return its output. Also used to run tails forward silently.
static float voice_tick(Voice* v) {
    const float sr = (float)s_host->sample_rate;
    const StyleParams* p = &s_sp;
    if (!v->released && v->age >= v->gate) v->released = 1;

    float out = 0.0f;
    const float sub_w = 1.0f - p->piano;

    if (sub_w > 0.0f) {
        // Amp envelope: linear attack, exponential decay to sustain, exponential release.
        if (!v->released) {
            const float a = p->a * sr;
            if ((float)v->age < a) v->env = (float)v->age / a;
            else v->env = p->s + (v->env - p->s) * (1.0f - 1.0f / (p->d * sr));
        } else {
            v->env *= 1.0f - 1.0f / (p->r * sr);
        }
        v->fenv *= 1.0f - 1.0f / (p->fdecay * sr);

        if ((v->age % CTRL_RATE) == 0) {
            float fc = (p->cutoff + p->env_mod * v->fenv * (0.5f + 0.5f * v->vel)) * v->cut_mult;
            fc = clampf(fc, 30.0f, sr * 0.16f);
            v->f_coef = 2.0f * sin01(0.5f * fc / sr); // 2 sin(pi fc / sr)
            v->q_coef = 2.0f * (1.0f - p->res * 0.97f);
        }

        const float inc = v->freq / sr;
        v->ph_saw += inc;
        if (v->ph_saw >= 1.0f) v->ph_saw -= 1.0f;
        v->ph_pulse += inc * 1.003f; // a hair of detune between the two oscillators
        if (v->ph_pulse >= 1.0f) v->ph_pulse -= 1.0f;
        v->ph_sub += inc * 0.5f;
        if (v->ph_sub >= 1.0f) v->ph_sub -= 1.0f;
        v->ph_lfo += 0.6f / sr;
        if (v->ph_lfo >= 1.0f) v->ph_lfo -= 1.0f;

        const float pw = p->pw + p->pwm * sin01(v->ph_lfo);
        const float osc = p->saw * (2.0f * v->ph_saw - 1.0f) + p->pulse * (v->ph_pulse < pw ? 1.0f : -1.0f) +
                          p->sub * (v->ph_sub < 0.5f ? 1.0f : -1.0f);

        // Two 12 dB state-variable stages make the 24 dB low-pass both machines are known for.
        float hp = osc - v->lp1 - v->q_coef * v->bp1;
        v->bp1 += v->f_coef * hp;
        v->lp1 += v->f_coef * v->bp1;
        hp = v->lp1 - v->lp2 - v->q_coef * v->bp2;
        v->bp2 += v->f_coef * hp;
        v->lp2 += v->f_coef * v->bp2;

        out += v->lp2 * v->env * sub_w * 0.35f;
    }

    if (p->piano > 0.0f) {
        if (v->released) v->damp *= 1.0f - 1.0f / (0.12f * sr);
        float sum = 0.0f;
        for (int k = 0; k < PARTIALS; k++) {
            if (v->pinc[k] == 0.0f) continue;
            v->pph[k] += v->pinc[k];
            if (v->pph[k] >= 1.0f) v->pph[k] -= 1.0f;
            v->pamp[k] *= v->pdec[k];
            sum += sin01(v->pph[k]) * v->pamp[k];
        }
        // Hammer: a 6 ms thump of noise at the start.
        if (v->age < (uint32_t)(0.006f * sr)) sum += noise_at(v->age, v->salt) * 0.3f * (1.0f - (float)v->age / (0.006f * sr));
        out += sum * v->damp * p->piano * 0.3f;
    }

    v->age++;
    out *= v->vel;

    // APHX crush: hold the output for a few samples (lower sample rate) and drop bits.
    if (v->hold > 1u) {
        if ((v->age % v->hold) == 1u) v->held = out;
        out = v->held;
    }
    if (v->levels > 0.0f) out = (float)(int32_t)(out * v->levels) / v->levels;

    // A voice is finished once both parts are inaudible.
    const int sub_done = sub_w <= 0.0f || (v->released && v->env < 0.0002f);
    const int piano_done = p->piano <= 0.0f || v->damp < 0.0002f || v->pamp[0] < 0.0002f;
    if (sub_done && piano_done) v->active = 0;
    return out;
}

// ---- Render ---------------------------------------------------------------------------------

static uint32_t s_pos = 0;
static int s_next_note = 0;
static int s_dirty = 1;
static uint32_t s_chorus_w = 0;
static float s_chorus_rate = 0.0f; // chorus sweeps per sample: a whole number of sweeps per loop

// One output sample at loop position n, before the soft clip. Starts any notes due at n.
static float render_sample(uint32_t n) {
    const float sr = (float)s_host->sample_rate;
    while (s_next_note < s_note_count && s_notes[s_next_note].start <= n) {
        voice_start(alloc_voice(), &s_notes[s_next_note], 0xA5A5u + (uint32_t)s_next_note * 977u);
        s_next_note++;
    }
    float mix = 0.0f;
    for (int k = 0; k < MAX_VOICES; k++)
        if (s_voices[k].active) mix += voice_tick(&s_voices[k]);

    // Mono chorus: a slowly swept 5-9 ms delay mixed back in, the Juno's signature width.
    if (s_chorus && s_sp.chorus > 0.0f) {
        s_chorus[s_chorus_w] = mix;
        // The sweep's phase comes from the loop position and fits the loop a whole number of
        // times, so the end of one pass and the start of the next line up.
        const float lfo = (float)n * s_chorus_rate;
        const float delay = (0.007f + 0.002f * sin01(lfo - (float)(int)lfo)) * sr;
        float rp = (float)s_chorus_w - delay;
        if (rp < 0.0f) rp += (float)CHORUS_LEN;
        const uint32_t i0 = (uint32_t)rp;
        const float fr = rp - (float)i0;
        const float wet = s_chorus[i0 % CHORUS_LEN] * (1.0f - fr) + s_chorus[(i0 + 1) % CHORUS_LEN] * fr;
        s_chorus_w = (s_chorus_w + 1) % CHORUS_LEN;
        mix = mix * (1.0f - 0.5f * s_sp.chorus) + wet * s_sp.chorus;
    }
    return mix;
}

static void start_render(void) {
    build_pattern();
    compute_style();
    for (int i = 0; i < MAX_VOICES; i++) s_voices[i].active = 0;
    if (s_chorus) for (int i = 0; i < CHORUS_LEN; i++) s_chorus[i] = 0.0f;
    s_chorus_w = 0;
    {
        // About 0.5 Hz, rounded to a whole number of sweeps per loop.
        const float cycles = (float)(int)((float)s_loop_len * 0.5f / (float)s_host->sample_rate + 0.5f);
        s_chorus_rate = (cycles < 1.0f ? 1.0f : cycles) / (float)s_loop_len;
    }

    // Carry the end of the loop into its start, so the loop repeats seamlessly. What continuous
    // playback would be hearing at the loop point is rebuilt exactly:
    //  1. every note from earlier passes is run forward silently to the last CHORUS_LEN samples
    //     of the previous pass. Long releases (Sustain, Drone) can outlast a whole loop, so
    //     copies from up to TAIL_PASSES passes back are kept while they're still audible;
    //  2. those last samples are rendered in full (new notes, voices and chorus) without output.
    // Everything still sounding, and the chorus delay, then carries on into the real pass.
    const uint32_t tail_from = s_loop_len > CHORUS_LEN ? s_loop_len - CHORUS_LEN : 0;
    for (uint32_t back = 1; back <= TAIL_PASSES; back++) {
        for (int i = 0; i < s_note_count; i++) {
            // How long this copy of the note has been playing when the previous pass reaches
            // tail_from. Copies that haven't started by then are the ones step 2 plays.
            const uint32_t start = s_notes[i].start;
            if (back == 1 && start >= tail_from) continue;
            const uint32_t run = tail_from + (back - 1u) * s_loop_len - start;
            Voice* v = alloc_voice();
            voice_start(v, &s_notes[i], 0xA5A5u + (uint32_t)i * 977u);
            for (uint32_t k = 0; k < run && v->active; k++) voice_tick(v);
        }
    }
    s_next_note = 0;
    while (s_next_note < s_note_count && s_notes[s_next_note].start < tail_from) s_next_note++;
    for (uint32_t n = tail_from; n < s_loop_len; n++) (void)render_sample(n);

    s_next_note = 0;
    s_dirty = 0;
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    s_host = host;
    s_chorus = (scratch_mem && scratch_size >= CHORUS_LEN * sizeof(float)) ? (float*)scratch_mem : 0;
    s_pos = 0;
    s_dirty = 1;
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0: s_mode = (int)clampf(value + 0.5f, 0.0f, 3.0f); break;
        case 1: s_style = clampf(value, 0.0f, 100.0f) * 0.01f; break;
        case 2: s_steps = (int)clampf(value + 0.5f, 16.0f, (float)MAX_STEPS); break;
        case 3: s_bpm = (int)clampf(value + 0.5f, 60.0f, 200.0f); break;
        case 4: s_seed = (uint32_t)clampf(value + 0.5f, 1.0f, 9999.0f); break;
        case 5: s_root = (int)clampf(value + 0.5f, 0.0f, 11.0f); break;
        case 6: s_scale = (int)clampf(value + 0.5f, 0.0f, 9.0f); break;
        case 7: s_chords = (int)clampf(value + 0.5f, 0.0f, 5.0f); break;
        case 8: s_aphx = clampf(value, 0.0f, 100.0f) * 0.01f; break;
        case 9: s_density = clampf(value, 0.0f, 1.0f); break;
        case 10: s_length = clampf(value, 0.0f, 1.0f); break;
        case 11: s_tone = clampf(value, 0.0f, 1.0f); break;
        default: return;
    }
    s_dirty = 1;
    if (s_host) {
        const uint32_t len = (uint32_t)(step_seconds() * (float)s_host->sample_rate * (float)s_steps + 0.5f);
        s_host->set_extra_samples(0, len);
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    (void)in;
    if (!s_host) return 0;
    if (s_dirty) start_render();
    if (s_pos >= s_loop_len) return 0;

    uint32_t todo = samples;
    if (todo > s_loop_len - s_pos) todo = s_loop_len - s_pos;

    for (uint32_t i = 0; i < todo; i++) {
        const float mix = render_sample(s_pos + i);
        const float y = s_host->tanhf(mix * 1.2f);
        const int16_t pcm = (int16_t)SATURATE_16BIT((int32_t)(y * 30000.0f));
        for (uint8_t ch = 0; ch < channels; ch++) out[ch][i] = pcm;
    }
    s_pos += todo;
    return (int32_t)todo;
}

#ifdef PTFX_HOST_TEST
// Test hook: after a full render, keep going into a second pass without resetting anything, the
// way continuous playback would. If the carried-over tail is right, this matches the first pass.
uint32_t synth_loop_gen_second_pass(int16_t* out) {
    s_pos = 0;
    s_next_note = 0;
    for (uint32_t n = 0; n < s_loop_len; n++) {
        const float y = s_host->tanhf(render_sample(n) * 1.2f);
        out[n] = (int16_t)SATURATE_16BIT((int32_t)(y * 30000.0f));
    }
    return s_loop_len;
}

// Test hook: the notes the current parameters generate, so the host test can check keys and modes.
int synth_loop_gen_notes(int* out_notes, int max) {
    build_pattern();
    int n = s_note_count < max ? s_note_count : max;
    for (int i = 0; i < n; i++) out_notes[i] = s_notes[i].note;
    return s_note_count;
}
#endif
