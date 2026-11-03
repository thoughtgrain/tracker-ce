#pragma once

#include "PolySynth/sdk/InstrumentType.h"
#include <memory>

namespace polysynth
{
// TORO: two oscillators, a wavefolder, a resonant low-pass and APHX key-tracked chaos.
// Port of toro from thoughtgrain/labyrinth (OP-1 / OP-XY). DSP lives in ToroCore.h.
std::unique_ptr<InstrumentType> createToroSynth();
} // namespace polysynth
