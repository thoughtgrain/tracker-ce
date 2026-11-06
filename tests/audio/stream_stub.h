#pragma once

// Test-side replacement for AudioStream's block plumbing (AudioStream.cpp is not compiled: it drags in the
// whole audio engine). The real AudioStream.h class declaration is used unchanged; only its out-of-line
// methods are defined in stream_stub.cpp, so the code under test calls exactly the methods it calls on device.
//
// How a test drives an object:
//   feed(&obj, input, samples)  queue the block that obj's next receiveReadOnly/receiveWritable(input) returns
//   obj.update()                run one block
//   take(&obj, output)          the block obj transmitted on that output (empty if none)
//
// Blocks are reference counted like the firmware's: a block fed to an object and received writable is copied
// if anything else still holds it, so the code under test sees the same ownership rules.

#include "AudioStream.h"

#include <array>
#include <optional>

namespace audio_test
{
using Block = std::array<int16_t, AUDIO_BLOCK_SAMPLES>;

void feed(AudioStream* to, unsigned input, const Block& samples);
std::optional<Block> take(AudioStream* from, unsigned output);
void resetStreams();
int blocksInUse();
} // namespace audio_test

#include <cstring>
#include <memory>
#include <new>

namespace audio_test
{
// Build an audio object the way the firmware has it: in zero-filled static storage, so members the
// constructor doesn't set start at 0 exactly as in .bss. Call resetStreams() before each new object.
template<class T>
struct Fresh
{
    alignas(T) unsigned char storage[sizeof(T)];
    T* obj;
    Fresh()
    {
        std::memset(storage, 0, sizeof(storage));
        obj = new (storage) T();
    }
    ~Fresh() { obj->~T(); }
    Fresh(const Fresh&) = delete;
    Fresh& operator=(const Fresh&) = delete;
    T* operator->() { return obj; }
};

// Deterministic test signals
Block sine(int blockIdx, float hz, float amp);       // continuous across blocks
Block rampedSine(int blockIdx, int blocks, float hz); // 0 -> full scale -> 0 over `blocks`
Block square(int blockIdx, float hz, int16_t amp);
Block noise(uint32_t& state, int16_t amp);
Block constant(int16_t v);
} // namespace audio_test
