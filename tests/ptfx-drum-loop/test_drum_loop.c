// Host test for editor-fx-sdk/effects/drum_loop_gen.c. Drives the plugin the way
// mtEffectModular does (init, set every parameter, then process blocks of up to 8,192 samples)
// and checks what the Preview/Apply workflow depends on.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tracker_plugin.h"

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host);
void plugin_set_param(uint8_t index, float value);
int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels);

static uint32_t g_append = 0;
static void set_extra(uint32_t pre, uint32_t app) { (void)pre; g_append = app; }
static int fake_rand(void) { return 12345; } // the plugin must not depend on it

static PtfxHostApi g_host;

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
    Params p = { { 16, 120, 1, 0, 0.5f, 0, 0.5f, 0.5f, 0.5f, 0.2f, 4, 0 } };
    return p;
}

// Renders like the host: init, set all 12 params, length from set_extra_samples, 8 KB blocks.
static int16_t* render(Params p, uint32_t* out_len) {
    g_append = 0;
    plugin_init(NULL, 0, &g_host);
    for (uint8_t i = 0; i < 12; i++) plugin_set_param(i, p.v[i]);
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

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char** argv) {
    int print_goldens = argc > 1 && strcmp(argv[1], "--print-goldens") == 0;
    setup_host();

    // 1. Length is exactly Steps sixteenths at BPM.
    const int steps_list[] = { 16, 20, 24 };
    const int bpm_list[] = { 60, 120, 200 };
    for (int s = 0; s < 3; s++)
        for (int b = 0; b < 3; b++) {
            Params p = defaults(); p.v[0] = steps_list[s]; p.v[1] = bpm_list[b];
            uint32_t n; int16_t* buf = render(p, &n);
            uint32_t expect = (uint32_t)(60.0f / bpm_list[b] / 4.0f * 44100.0f * steps_list[s] + 0.5f);
            CHECK(n == expect, "length %u != %u (steps %d, bpm %d)", n, expect, steps_list[s], bpm_list[b]);
            free(buf);
        }

    // 2. Preview and Apply render the same audio: two renders with the same params match.
    {
        Params p = defaults(); p.v[2] = 42; p.v[3] = 1; p.v[5] = 25;
        uint32_t n1, n2; int16_t* a = render(p, &n1); int16_t* b = render(p, &n2);
        CHECK(n1 == n2 && memcmp(a, b, n1 * 2) == 0, "same params rendered different audio");
        free(a); free(b);
    }

    // 3. Turning Seed changes the pattern (regenerate), for every style.
    for (int style = 0; style < 4; style++) {
        Params p = defaults(); p.v[3] = style;
        uint32_t n1, n2; int16_t* a = render(p, &n1);
        p.v[2] = 2; int16_t* b = render(p, &n2);
        CHECK(n1 == n2 && memcmp(a, b, n1 * 2) != 0, "seed 1 and 2 match for style %d", style);
        free(a); free(b);
    }

    // 4. Not silent, not wall-to-wall clipped, and the loop starts with a hit.
    Params golden_params[4];
    for (int style = 0; style < 4; style++) {
        Params p = defaults(); p.v[3] = style; p.v[2] = 7; p.v[0] = style == 1 ? 24 : 16;
        golden_params[style] = p;
        uint32_t n; int16_t* a = render(p, &n);
        int peak = 0; uint32_t clipped = 0;
        for (uint32_t i = 0; i < n; i++) {
            int v = abs(a[i]); if (v > peak) peak = v; if (v >= 32000) clipped++;
        }
        CHECK(peak > 8000, "style %d is too quiet (peak %d)", style, peak);
        CHECK(clipped < n / 100, "style %d clips on %u of %u samples", style, clipped, n);
        int head = 0; for (uint32_t i = 0; i < 2000; i++) if (abs(a[i]) > head) head = abs(a[i]);
        CHECK(head > 4000, "style %d has no downbeat (peak %d in first 2000 samples)", style, head);
        char path[64]; snprintf(path, sizeof path, "build/style%d.wav", style);
        write_wav(path, a, n);
        free(a);
    }

    // 5. Machine reshapes the sound: each of the five settings renders differently and audibly.
    {
        static const char* names[5] = { "cr78", "cr78-808", "808", "808-909", "909" };
        int16_t* prev = NULL; uint32_t prev_n = 0;
        for (int m = 0; m < 5; m++) {
            Params p = defaults(); p.v[2] = 7; p.v[10] = m;
            uint32_t n; int16_t* a = render(p, &n);
            int peak = 0; for (uint32_t i = 0; i < n; i++) if (abs(a[i]) > peak) peak = abs(a[i]);
            CHECK(peak > 6000, "machine %s is too quiet (peak %d)", names[m], peak);
            if (prev) CHECK(n == prev_n && memcmp(a, prev, n * 2) != 0, "machine %s sounds the same as the one before", names[m]);
            char path[64]; snprintf(path, sizeof path, "build/machine-%s.wav", names[m]);
            write_wav(path, a, n);
            free(prev); prev = a; prev_n = n;
        }
        free(prev);
    }

    // 6. Goldens: one hash per style, so any change to the sound is deliberate.
    static const uint32_t goldens[4] = { GOLDEN0, GOLDEN1, GOLDEN2, GOLDEN3 };
    for (int style = 0; style < 4; style++) {
        uint32_t n; int16_t* a = render(golden_params[style], &n);
        uint32_t h = fnv(a, n);
        if (print_goldens) printf("GOLDEN%d=0x%08x\n", style, h);
        else CHECK(h == goldens[style], "style %d hash 0x%08x != golden 0x%08x", style, h, goldens[style]);
        free(a);
    }

    if (print_goldens) return 0;
    printf(failures ? "%d check(s) failed\n" : "drum_loop_gen: all checks passed\n", failures);
    return failures ? 1 : 0;
}
