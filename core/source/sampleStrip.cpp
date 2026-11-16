#include "sampleStrip.h"

#include <math.h>

// Main loop only (pattern editor refresh). Float maths with `f` literals throughout, so the same
// code is cheap on the MK66's single-precision FPU even though nothing here runs in the interrupt.

namespace sampleStrip
{
namespace
{
// The peak cache stores sample / 300 (VoiceStreamManager::buildWaveformPeaksFromRam and the SD
// build), so a full-scale sample reads 32767 / 300 = 109.
constexpr int kPeakFullScale = 32767 / 300;

int wrapStep(int step, int patternLength)
{
    int s = step % patternLength;
    return (s < 0) ? s + patternLength : s;
}

// The step at or before `step` (searching back up to a full pattern) that holds a note or an off,
// and how many steps back it is. Returns -1 when the track holds neither.
int findLastEvent(const Step* steps, int patternLength, int step, int& distance)
{
    for (int d = 0; d < patternLength; d++)
    {
        int s = wrapStep(step - d, patternLength);
        if (steps[s].kind != Step::Kind::kEmpty)
        {
            distance = d;
            return s;
        }
    }
    return -1;
}

// Steps from `step` to the next note or off after it (a full pattern if there is none).
int stepsToNextEvent(const Step* steps, int patternLength, int step)
{
    for (int d = 1; d < patternLength; d++)
    {
        if (steps[wrapStep(step + d, patternLength)].kind != Step::Kind::kEmpty)
        {
            return d;
        }
    }
    return patternLength;
}

// How many steps a note sounds: until the next event on the track, or until a one-shot runs out.
float soundingSteps(const Step* steps, int patternLength, int step, const Options& options)
{
    float untilNext = (float)stepsToNextEvent(steps, patternLength, step);
    const Playback& pb = steps[step].playback;
    if (pb.mode == Mode::kOneShot && pb.framesPerSecond > 0.0f && options.stepSeconds > 0.0f)
    {
        float frames = (pb.end > pb.start) ? (float)(pb.end - pb.start) : 0.0f;
        float steps = (frames / pb.framesPerSecond) / options.stepSeconds;
        return (steps < untilNext) ? steps : untilNext;
    }
    return untilNext;
}
} // namespace

Position positionAt(const Playback& pb, float seconds)
{
    Position out;
    if (pb.mode == Mode::kSilent || pb.framesPerSecond <= 0.0f || seconds < 0.0f)
    {
        return out;
    }

    float travelled = seconds * pb.framesPerSecond;

    uint32_t lo = (pb.loop1 < pb.loop2) ? pb.loop1 : pb.loop2;
    uint32_t hi = (pb.loop1 < pb.loop2) ? pb.loop2 : pb.loop1;
    bool looping = pb.mode != Mode::kOneShot && hi > lo;

    if (!looping)
    {
        if (pb.end <= pb.start)
        {
            return out;
        }
        float frame = (float)pb.start + travelled;
        if (frame >= (float)pb.end)
        {
            return out;
        }
        out.audible = true;
        out.frame = (uint32_t)frame;
        return out;
    }

    // Before the loop: straight from start to the loop's upper point.
    float lead = (pb.start < hi) ? (float)(hi - pb.start) : 0.0f;
    if (travelled < lead)
    {
        out.audible = true;
        out.frame = pb.start + (uint32_t)travelled;
        return out;
    }

    float length = (float)(hi - lo);
    float inLoop = travelled - lead;
    float laps = floorf(inLoop / length);
    float phase = inLoop - laps * length;

    out.audible = true;
    out.lap = (uint32_t)laps + 1; // reaching the loop is the first wrap

    switch (pb.mode)
    {
        case Mode::kLoopForward: out.frame = lo + (uint32_t)phase; break;
        case Mode::kLoopBackward: out.frame = hi - (uint32_t)phase; break;
        case Mode::kPingPong:
        {
            // Each lap is one direction: back from hi to lo, then forward from lo to hi.
            uint32_t lap = (uint32_t)laps;
            out.frame = (lap % 2 == 0) ? hi - (uint32_t)phase : lo + (uint32_t)phase;
            break;
        }
        default: out.audible = false; break;
    }

    if (out.frame < lo)
        out.frame = lo;
    if (out.frame > hi)
        out.frame = hi;
    return out;
}

uint8_t levelAt(const Playback& pb, uint32_t frame)
{
    if (pb.peaks == nullptr || pb.totalFrames == 0)
    {
        return 0;
    }
    uint32_t idx = (uint32_t)(((uint64_t)frame * kWaveformPeakPoints) / pb.totalFrames);
    if (idx >= kWaveformPeakPoints)
    {
        idx = kWaveformPeakPoints - 1;
    }
    int lo = pb.peaks[idx].minVal;
    int hi = pb.peaks[idx].maxVal;
    int magnitude = (hi > -lo) ? hi : -lo;
    if (magnitude <= 0)
    {
        return 0;
    }
    int level = (magnitude * 255) / kPeakFullScale;
    return (uint8_t)((level > 255) ? 255 : level);
}

void computeRows(const Step* steps,
                 int patternLength,
                 int firstStep,
                 int rowCount,
                 const Options& options,
                 Row* out)
{
    for (int i = 0; i < rowCount; i++)
    {
        out[i] = Row();
    }
    if (steps == nullptr || patternLength <= 0 || options.stepSeconds <= 0.0f)
    {
        return;
    }

    for (int i = 0; i < rowCount; i++)
    {
        int step = wrapStep(firstStep + i, patternLength);
        int distance = 0;
        int eventStep = findLastEvent(steps, patternLength, step, distance);
        if (eventStep < 0 || steps[eventStep].kind != Step::Kind::kNote)
        {
            continue;
        }

        if (options.minSteps > 0 &&
            soundingSteps(steps, patternLength, eventStep, options) < (float)options.minSteps)
        {
            continue;
        }

        const Playback& pb = steps[eventStep].playback;
        out[i].noteStart = (distance == 0);

        for (int q = 0; q < kQuartersPerRow; q++)
        {
            float quarter = 1.0f / (float)kQuartersPerRow;
            float t0 = ((float)distance + (float)q * quarter) * options.stepSeconds;
            float tMid = t0 + 0.5f * quarter * options.stepSeconds;
            float t1 = t0 + quarter * options.stepSeconds;

            Position mid = positionAt(pb, tMid);
            if (mid.audible)
            {
                out[i].level[q] = levelAt(pb, mid.frame);
            }

            Position a = positionAt(pb, t0);
            Position b = positionAt(pb, t1);
            out[i].loopWrap[q] = a.audible && b.audible && a.lap != b.lap;
        }
    }
}

} // namespace sampleStrip
