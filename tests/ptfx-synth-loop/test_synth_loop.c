// Host test for editor-fx-sdk/effects/synth_loop_gen.c. Drives the plugin the way
// mtEffectModular does (init with scratch, set all 12 parameters, process 8,192-sample blocks).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tracker_plugin.h"

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host);
void plugin_set_param(uint8_t index, float value);
int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels);
int synth_loop_gen_notes(int* out_notes, int max);
uint32_t synth_loop_gen_second_pass(int16_t* out);

enum { P_MODE, P_STYLE, P_STEPS, P_BPM, P_SEED, P_ROOT, P_SCALE, P_CHORDS, P_APHX, P_DENS, P_LEN, P_TONE };

static uint32_t g_append = 0;
static void set_extra(uint32_t pre, uint32_t app) { (void)pre; g_append = app; }
static int fake_rand(void) { return 12345; }
static PtfxHostApi g_host;
static float g_scratch[2048];

static void setup_host(void) {
    memset(&g_host, 0, sizeof g_host);
    g_host.sample_rate = 44100;
    g_host.sinf = sinf; g_host.cosf = cosf; g_host.tanf = tanf; g_host.tanhf = tanhf;
    g_host.powf = powf; g_host.expf = expf; g_host.sqrtf = sqrtf; g_host.logf = logf;
    g_host.log10f = log10f; g_host.fminf = fminf; g_host.fmaxf = fmaxf; g_host.fabsf = fabsf;
    g_host.memcpy = memcpy; g_host.memset = memset; g_host.rand = fake_rand;
    g_host.set_extra_samples = set_extra;
}

typedef struct { float v[12]; } Params;
static Params defaults(void) {
    Params p = { { 0, 50, 16, 120, 1, 0, 1, 0, 0, 0.6f, 0.4f, 0.5f } };
    return p;
}

static void apply(Params p) {
    g_append = 0;
    plugin_init(g_scratch, sizeof g_scratch, &g_host);
    for (uint8_t i = 0; i < 12; i++) plugin_set_param(i, p.v[i]);
}

static int16_t* render(Params p, uint32_t* out_len) {
    apply(p);
    uint32_t len = g_append ? g_append : 88200;
    int16_t* buf = calloc(len, sizeof(int16_t));
    uint32_t done = 0;
    while (done < len) {
        uint32_t block = len - done > 8192 ? 8192 : len - done;
        int16_t* outs[1] = { buf + done };
        int32_t got = plugin_process_block(NULL, outs, block, 1);
        if (got <= 0) break;
        done += (uint32_t)got;
    }
    *out_len = done;
    return buf;
}

static uint32_t fnv(const int16_t* b, uint32_t n) {
    uint32_t h = 2166136261u;
    const uint8_t* p = (const uint8_t*)b;
    for (uint32_t i = 0; i < n * 2; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static void write_wav(const char* path, const int16_t* b, uint32_t n) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = n * 2, riff = 36 + data, sr = 44100, br = sr * 2, fmt = 16;
    uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(b, 2, n, f); fclose(f);
}

// Envelope difference over 10 ms windows, relative to a's level (ignores harmless phase drift).
static double env_diff(const int16_t* a, const int16_t* b, uint32_t n) {
    double diff = 0, level = 0;
    for (uint32_t w = 0; w + 441 <= n; w += 441) {
        double ea = 0, eb = 0;
        for (uint32_t i = w; i < w + 441; i++) { ea += fabs((double)a[i]); eb += fabs((double)b[i]); }
        diff += fabs(ea - eb); level += ea;
    }
    return level > 0 ? diff / level : 0;
}

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static const uint8_t SCALES[10][12] = {
    { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
    { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 },
    { 0, 3, 5, 6, 7, 10 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
};
static const int SCALE_LEN[10] = { 7, 7, 7, 7, 7, 7, 5, 5, 6, 12 };
static const char* MODES[4] = { "seq", "arp", "sustain", "drone" };

int main(int argc, char** argv) {
    int print_goldens = argc > 1 && strcmp(argv[1], "--print-goldens") == 0;
    setup_host();

    // 1. Length is exactly Steps sixteenths at BPM.
    const int steps_list[] = { 16, 24 }, bpm_list[] = { 60, 200 };
    for (int s = 0; s < 2; s++)
        for (int b = 0; b < 2; b++) {
            Params p = defaults(); p.v[P_STEPS] = steps_list[s]; p.v[P_BPM] = bpm_list[b];
            uint32_t n; int16_t* buf = render(p, &n);
            uint32_t expect = (uint32_t)(60.0f / bpm_list[b] / 4.0f * 44100.0f * steps_list[s] + 0.5f);
            CHECK(n == expect, "length %u != %u", n, expect);
            free(buf);
        }

    // 2. Every note is in the chosen key and scale, in every mode, for every chord type.
    for (int mode = 0; mode < 4; mode++)
        for (int scale = 0; scale < 10; scale++)
            for (int root = 0; root < 12; root += 5)
                for (int chords = 0; chords < 6; chords++) {
                    Params p = defaults();
                    p.v[P_MODE] = mode; p.v[P_SCALE] = scale; p.v[P_ROOT] = root; p.v[P_CHORDS] = chords; p.v[P_SEED] = 3 + scale;
                    apply(p);
                    int notes[256];
                    int count = synth_loop_gen_notes(notes, 256);
                    CHECK(count > 0, "%s scale %d root %d chords %d made no notes", MODES[mode], scale, root, chords);
                    for (int i = 0; i < count && i < 256; i++) {
                        int pc = ((notes[i] - root) % 12 + 12) % 12, ok = 0;
                        for (int k = 0; k < SCALE_LEN[scale]; k++) if (SCALES[scale][k] == pc) ok = 1;
                        CHECK(ok, "%s scale %d root %d chords %d: note %d is out of key", MODES[mode], scale, root, chords, notes[i]);
                    }
                }

    // 3. Preview == Apply, and Seed regenerates, in every mode.
    for (int mode = 0; mode < 4; mode++) {
        Params p = defaults(); p.v[P_MODE] = mode; p.v[P_CHORDS] = mode == 0 ? 0 : 1;
        uint32_t n1, n2, n3; int16_t* a = render(p, &n1); int16_t* b = render(p, &n2);
        CHECK(n1 == n2 && memcmp(a, b, n1 * 2) == 0, "%s: same params rendered different audio", MODES[mode]);
        p.v[P_SEED] = 2; int16_t* c = render(p, &n3);
        if (mode != 3) CHECK(memcmp(a, c, n1 * 2) != 0, "%s: seed 1 and 2 match", MODES[mode]);
        free(a); free(b); free(c);
    }

    // 4. Chords change the sound; every mode is audible and doesn't clip much.
    for (int mode = 0; mode < 4; mode++) {
        Params p = defaults(); p.v[P_MODE] = mode; p.v[P_SEED] = 5;
        uint32_t n1, n2; int16_t* off = render(p, &n1);
        p.v[P_CHORDS] = 1; int16_t* tri = render(p, &n2);
        CHECK(memcmp(off, tri, n1 * 2) != 0, "%s: chords on and off sound the same", MODES[mode]);
        int peak = 0; uint32_t clipped = 0;
        for (uint32_t i = 0; i < n2; i++) { int v = abs(tri[i]); if (v > peak) peak = v; if (v > 29500) clipped++; }
        CHECK(peak > 4000, "%s is too quiet (peak %d)", MODES[mode], peak);
        CHECK(clipped < n2 / 50, "%s clips on %u of %u samples", MODES[mode], clipped, n2);
        char path[64]; snprintf(path, sizeof path, "build/mode-%s.wav", MODES[mode]);
        write_wav(path, tri, n2);
        free(off); free(tri);
    }

    // 5. Style blends continuously from SH-101 (0) through Juno (50) to piano (100).
    {
        static const int values[5] = { 0, 25, 50, 75, 100 };
        static const char* names[5] = { "sh101", "sh101-juno", "juno", "juno-piano", "piano" };
        int16_t* prev = NULL;
        for (int s = 0; s < 5; s++) {
            Params p = defaults(); p.v[P_STYLE] = values[s]; p.v[P_SEED] = 9;
            uint32_t n; int16_t* a = render(p, &n);
            int peak = 0; for (uint32_t i = 0; i < n; i++) if (abs(a[i]) > peak) peak = abs(a[i]);
            CHECK(peak > 3000, "style %d is too quiet (peak %d)", values[s], peak);
            if (prev) CHECK(memcmp(a, prev, n * 2) != 0, "style %d sounds the same as the one before", values[s]);
            char path[64]; snprintf(path, sizeof path, "build/style-%s.wav", names[s]);
            write_wav(path, a, n);
            free(prev); prev = a;
        }
        free(prev);
        Params p = defaults(); p.v[P_SEED] = 9;
        uint32_t n; p.v[P_STYLE] = 0; int16_t* lo = render(p, &n); p.v[P_STYLE] = 100; int16_t* hi = render(p, &n);
        p.v[P_STYLE] = 50; int16_t* a = render(p, &n); p.v[P_STYLE] = 51; int16_t* b = render(p, &n);
        const double step = env_diff(a, b, n), range = env_diff(lo, hi, n);
        CHECK(step < range * 0.15, "style 50 -> 51 is not a small step (%.3f of %.3f)", step, range);
        free(lo); free(hi); free(a); free(b);
    }

    // 5b. APHX: off changes nothing (the goldens below were made before APHX existed), 25 is a
    //     subtle change and 100 a strong one, and it's repeatable.
    {
        Params p = defaults(); p.v[P_SEED] = 9; p.v[P_STYLE] = 30;
        uint32_t n; int16_t* clean = render(p, &n);
        p.v[P_APHX] = 25; int16_t* light = render(p, &n);
        p.v[P_APHX] = 100; int16_t* heavy = render(p, &n);
        int16_t* again = render(p, &n);
        const double d_light = env_diff(clean, light, n), d_heavy = env_diff(clean, heavy, n);
        CHECK(memcmp(clean, light, n * 2) != 0, "APHX 25 changes nothing");
        CHECK(d_heavy > d_light, "APHX 100 (%.3f) isn't stronger than 25 (%.3f)", d_heavy, d_light);
        CHECK(memcmp(heavy, again, n * 2) == 0, "APHX render isn't repeatable");
        write_wav("build/aphx-25.wav", light, n);
        write_wav("build/aphx-100.wav", heavy, n);
        free(clean); free(light); free(heavy); free(again);
    }

    // 5c. Seamless loop: carrying straight on into a second pass (as continuous playback would)
    //     gives the same audio as the single render, so the loop's start already holds the right
    //     tail of the end. Voices can sum in a different order, so allow 8 LSB (about -72 dB) of rounding.
    for (int mode = 0; mode < 4; mode++)
        for (int style = 0; style <= 100; style += 25) {
            Params p = defaults(); p.v[P_MODE] = mode; p.v[P_STYLE] = style; p.v[P_CHORDS] = mode == 0 ? 0 : 1;
            p.v[P_STEPS] = 24; p.v[P_BPM] = 80; p.v[P_SEED] = 4; p.v[P_LEN] = mode >= 2 ? 0.9f : 0.4f;
            uint32_t n; int16_t* first = render(p, &n);
            int16_t* second = calloc(n, sizeof(int16_t));
            uint32_t n2 = synth_loop_gen_second_pass(second);
            int worst = 0; uint32_t at = 0;
            for (uint32_t i = 0; i < n && i < n2; i++) { int d = abs(first[i] - second[i]); if (d > worst) { worst = d; at = i; } }
            CHECK(n2 == n && worst <= 8, "%s style %d: second pass differs by %d at sample %u", MODES[mode], style, worst, at);
            free(first); free(second);
        }

    // 6. Render cost, for a rough comparison between styles (host only, not the device).
    {
        Params p = defaults(); p.v[P_MODE] = 2; p.v[P_CHORDS] = 2; p.v[P_STEPS] = 24; p.v[P_BPM] = 60;
        for (int s = 0; s <= 100; s += 50) {
            p.v[P_STYLE] = s;
            clock_t t0 = clock(); uint32_t n; int16_t* a = render(p, &n); clock_t t1 = clock();
            if (!print_goldens) printf("  worst case (24 steps, 60 BPM, 7th chords, style %d): %.2f s on this host\n", s, (double)(t1 - t0) / CLOCKS_PER_SEC);
            free(a);
        }
    }

    // 7. Goldens: one per mode.
    static const uint32_t goldens[4] = { GOLDEN0, GOLDEN1, GOLDEN2, GOLDEN3 };
    for (int mode = 0; mode < 4; mode++) {
        Params p = defaults(); p.v[P_MODE] = mode; p.v[P_CHORDS] = mode == 0 ? 0 : 1; p.v[P_SEED] = 7;
        uint32_t n; int16_t* a = render(p, &n);
        uint32_t h = fnv(a, n);
        if (print_goldens) printf("GOLDEN%d=0x%08x\n", mode, h);
        else CHECK(h == goldens[mode], "%s hash 0x%08x != golden 0x%08x", MODES[mode], h, goldens[mode]);
        free(a);
    }

    if (print_goldens) return 0;
    printf(failures ? "%d check(s) failed\n" : "synth_loop_gen: all checks passed\n", failures);
    return failures ? 1 : 0;
}
