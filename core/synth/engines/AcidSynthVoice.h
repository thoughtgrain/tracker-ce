#pragma once

#include "PolySynth/sdk/InstrumentType.h"
#include <memory>

namespace polysynth
{
// ACID: an acid bass and lead voice (saw / pulse / sub, three filters, glide). The open engine ported from
// OpenTracker (core/synth/open/engines/Acid.cpp), hosted through the SDK on Tracker+ and Tracker Mini.
std::unique_ptr<InstrumentType> createAcidSynth();
} // namespace polysynth
