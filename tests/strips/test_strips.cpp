// Host tests for the sample strips' model. Build and run with `make -C tests/strips`.
#include "sampleStrip.h"

#include <cstdio>
#include <cstring>

using namespace sampleStrip;

static int s_failures = 0;
#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            s_failures++;                                                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                                       \
            std::printf(__VA_ARGS__);                                                              \
            std::printf("\n");                                                                     \
        }                                                                                          \
    } while (0)

// A peak cache whose level rises with position, so a level says where in the sample we are.
static WaveformPeakPoint s_ramp[kWaveformPeakPoints];
// A peak cache at full scale everywhere.
static WaveformPeakPoint s_full[kWaveformPeakPoints];

static void makePeaks()
{
    for (uint32_t i = 0; i < kWaveformPeakPoints; i++)
    {
        int16_t v = (int16_t)((i * 109) / (kWaveformPeakPoints - 1));
        s_ramp[i] = { (int16_t)-v, v };
        s_full[i] = { -109, 109 };
    }
}

static Playback oneShot(uint32_t start,
                        uint32_t end,
                        float fps,
                        const WaveformPeakPoint* peaks,
                        uint32_t total)
{
    Playback p;
    p.mode = Mode::kOneShot;
    p.start = start;
    p.end = end;
    p.framesPerSecond = fps;
    p.peaks = peaks;
    p.totalFrames = total;
    return p;
}

static Playback looped(Mode mode,
                       uint32_t start,
                       uint32_t loop1,
                       uint32_t loop2,
                       uint32_t end,
                       float fps,
                       uint32_t total)
{
    Playback p;
    p.mode = mode;
    p.start = start;
    p.loop1 = loop1;
    p.loop2 = loop2;
    p.end = end;
    p.framesPerSecond = fps;
    p.peaks = s_full;
    p.totalFrames = total;
    return p;
}

static void testPositions()
{
    // One-shot: 1000 frames at 1000 frames/s plays for one second, then nothing.
    Playback os = oneShot(0, 1000, 1000.0f, s_full, 1000);
    CHECK(positionAt(os, 0.5f).audible && positionAt(os, 0.5f).frame == 500, "one-shot midway");
    CHECK(!positionAt(os, 1.0f).audible, "one-shot ends at its end point");
    CHECK(!positionAt(os, -0.1f).audible, "before the note: silent");

    // Forward loop: start 0, loop 500..1000. The first 1000 frames run straight to the loop's top
    // (1 s); at 1.25 s it has wrapped once to 500 and gone 250 further: frame 750, lap 1. At 1.75 s
    // it has wrapped again: frame 750 + 500 - 500 = 750 on lap 2.
    Playback fw = looped(Mode::kLoopForward, 0, 500, 1000, 1000, 1000.0f, 1000);
    Position p = positionAt(fw, 1.25f);
    CHECK(
      p.audible && p.frame == 750 && p.lap == 1, "forward loop: frame %u lap %u", p.frame, p.lap);
    p = positionAt(fw, 1.75f);
    CHECK(p.audible && p.frame == 750 && p.lap == 2,
          "forward loop, second lap: frame %u lap %u",
          p.frame,
          p.lap);
    CHECK(positionAt(fw, 0.25f).lap == 0, "forward loop: no wrap before the loop");

    // Backward loop: after reaching 1000 it runs 1000 -> 500 backwards.
    Playback bw = looped(Mode::kLoopBackward, 0, 500, 1000, 1000, 1000.0f, 1000);
    p = positionAt(bw, 1.1f);
    CHECK(p.audible && p.frame == 900, "backward loop: frame %u", p.frame);

    // Ping-pong: back from 1000 to 500, then forward again.
    Playback pp = looped(Mode::kPingPong, 0, 500, 1000, 1000, 1000.0f, 1000);
    CHECK(positionAt(pp, 1.1f).frame == 900, "ping-pong going back");
    CHECK(positionAt(pp, 1.6f).frame == 600,
          "ping-pong coming forward: %u",
          positionAt(pp, 1.6f).frame);
}

static void testLevels()
{
    Playback ramp = oneShot(0, 6000, 1000.0f, s_ramp, 6000);
    CHECK(levelAt(ramp, 0) == 0, "silent at the ramp's start");
    CHECK(levelAt(ramp, 5999) == 255, "full scale at the ramp's end: %u", levelAt(ramp, 5999));
    CHECK(levelAt(ramp, 3000) > 100 && levelAt(ramp, 3000) < 160,
          "about half way: %u",
          levelAt(ramp, 3000));
    Playback none = oneShot(0, 6000, 1000.0f, nullptr, 6000);
    CHECK(levelAt(none, 10) == 0, "no peak cache: nothing drawn");
}

static void testRows()
{
    // 16 steps, 0.125 s each. A 0.5 s one-shot on step 0 sounds for 4 steps.
    Step steps[16];
    steps[0].kind = Step::Kind::kNote;
    steps[0].playback = oneShot(0, 500, 1000.0f, s_full, 500);
    Options opt;
    opt.stepSeconds = 0.125f;
    Row rows[16];
    computeRows(steps, 16, 0, 16, opt, rows);
    CHECK(rows[0].noteStart && !rows[1].noteStart, "note start marked on its row only");
    CHECK(rows[0].level[0] > 0 && rows[3].level[3] > 0, "sounding for four steps");
    CHECK(rows[4].level[0] == 0 && rows[10].level[2] == 0, "silent once the one-shot ends");

    // An off on step 2 stops it.
    steps[2].kind = Step::Kind::kOff;
    computeRows(steps, 16, 0, 16, opt, rows);
    CHECK(rows[1].level[3] > 0 && rows[2].level[0] == 0, "note off stops the strip");
    steps[2].kind = Step::Kind::kEmpty;

    // A long loop on step 14 carries over the pattern's end into the rows before step 0.
    Step loop[16];
    loop[14].kind = Step::Kind::kNote;
    loop[14].playback = looped(Mode::kLoopForward, 0, 0, 250, 250, 1000.0f, 250);
    computeRows(loop, 16, -2, 6, opt, rows); // shows steps 14, 15, 0, 1, 2, 3
    CHECK(rows[0].noteStart, "the loop starts on the first row shown");
    CHECK(rows[2].level[1] > 0 && rows[5].level[3] > 0, "the loop keeps going across step 0");
    bool sawWrap = false;
    for (int r = 0; r < 6; r++)
        for (int q = 0; q < kQuartersPerRow; q++)
            sawWrap = sawWrap || rows[r].loopWrap[q];
    CHECK(sawWrap, "a 0.25 s loop wraps within six 0.125 s rows");

    // "Long samples": a hit that sounds for under four steps draws nothing.
    Step hits[16];
    for (int s = 0; s < 16; s += 2)
    {
        hits[s].kind = Step::Kind::kNote;
        hits[s].playback = oneShot(0, 100, 1000.0f, s_full, 100);
    }
    Options longOnly = opt;
    longOnly.minSteps = 4;
    computeRows(hits, 16, 0, 16, longOnly, rows);
    CHECK(rows[0].level[0] == 0, "short hits hidden with Long samples");
    computeRows(hits, 16, 0, 16, opt, rows);
    CHECK(rows[0].level[0] > 0 && rows[0].level[3] == 0, "short hits shown and dying away");
}

// FNV-1a over a scenario's full output: a loop, a one-shot, an off and a wrap.
static uint32_t scenarioHash()
{
    Step steps[32];
    steps[0].kind = Step::Kind::kNote;
    steps[0].playback = looped(Mode::kPingPong, 100, 300, 2900, 3000, 9000.0f, 3000);
    steps[0].playback.peaks = s_ramp;
    steps[12].kind = Step::Kind::kNote;
    steps[12].playback = oneShot(500, 2500, 12000.0f, s_ramp, 3000);
    steps[20].kind = Step::Kind::kOff;
    steps[28].kind = Step::Kind::kNote;
    steps[28].playback = looped(Mode::kLoopBackward, 0, 1000, 2000, 3000, 7000.0f, 3000);
    steps[28].playback.peaks = s_ramp;
    Options opt;
    opt.stepSeconds = 60.0f / (128.0f * 4.0f);
    Row rows[40];
    computeRows(steps, 32, -4, 40, opt, rows);
    uint32_t h = 2166136261u;
    for (const Row& r : rows)
    {
        for (int q = 0; q < kQuartersPerRow; q++)
        {
            h = (h ^ r.level[q]) * 16777619u;
            h = (h ^ (uint8_t)r.loopWrap[q]) * 16777619u;
        }
        h = (h ^ (uint8_t)r.noteStart) * 16777619u;
    }
    return h;
}

static constexpr uint32_t kScenarioGolden = 0x4f5cae82u;

int main(int argc, char** argv)
{
    makePeaks();
    if (argc > 1 && std::strcmp(argv[1], "--print-goldens") == 0)
    {
        std::printf("scenario 0x%08xu\n", scenarioHash());
        return 0;
    }
    testPositions();
    testLevels();
    testRows();
    uint32_t h = scenarioHash();
    CHECK(h == kScenarioGolden, "scenario golden 0x%08x (expected 0x%08x)", h, kScenarioGolden);
    if (s_failures)
    {
        std::printf("%d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("all sample strip tests passed\n");
    return 0;
}
