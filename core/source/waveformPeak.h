#ifndef SOURCE_WAVEFORMPEAK_H_
#define SOURCE_WAVEFORMPEAK_H_

// The per-instrument waveform peak cache's point type, kept apart from mtStructs.h so code that
// only reads peaks (the sample strips' model, and its host tests) doesn't need the whole project
// struct.

#include <stdint.h>

static constexpr uint32_t kWaveformPeakPoints = 600;

struct WaveformPeakPoint
{
    int16_t minVal;
    int16_t maxVal;
};
static_assert(sizeof(WaveformPeakPoint) == 4, "WaveformPeakPoint layout is shared with the cache");

#endif /* SOURCE_WAVEFORMPEAK_H_ */
