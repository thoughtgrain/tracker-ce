// See stream_stub.h. Definitions of AudioStream's out-of-line members for host tests, plus the one global the
// limiter reads from the project.

#include "stream_stub.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

HostProject mtProject;

// AudioStream statics declared in AudioStream.h
uint16_t AudioStream::memory_used = 0;
uint16_t AudioStream::memory_used_max = 0;
bool AudioStream::renderMode = false;
bool AudioStream::update_scheduled = false;
AudioStream* AudioStream::first_update = nullptr;
audio_block_t* AudioStream::memory_pool = nullptr;
uint16_t AudioStream::memory_pool_first_mask = 0;
#ifdef PLATFORM_IMX
UsbAudio* AudioStream::usbAudio = nullptr;
#endif

namespace
{
constexpr int kPoolSize = 64;
audio_block_t pool[kPoolSize];
bool used[kPoolSize];

std::map<std::pair<const AudioStream*, unsigned>, audio_block_t*> inputs;  // fed, not yet received
std::map<std::pair<const AudioStream*, unsigned>, audio_test::Block> outputs; // transmitted, not yet taken
int blocksAllocated = 0;

// One pool for both the code under test (AudioStream::allocate is protected) and the test's feed().
audio_block_t* poolAllocate()
{
    for (int i = 0; i < kPoolSize; i++)
    {
        if (!used[i])
        {
            used[i] = true;
            pool[i].ref_count = 1;
            pool[i].memory_pool_index = static_cast<uint16_t>(i);
            blocksAllocated++;
            return &pool[i];
        }
    }
    return nullptr;
}
} // namespace

audio_block_t* AudioStream::allocate(void) { return poolAllocate(); }

void AudioStream::release(audio_block_t* block)
{
    if (block == nullptr)
        return;
    assert(block->ref_count > 0);
    if (--block->ref_count == 0)
    {
        used[block->memory_pool_index] = false;
        blocksAllocated--;
    }
}

void AudioStream::transmit(audio_block_t* block, unsigned char index)
{
    if (block == nullptr)
        return;
    audio_test::Block copy;
    std::memcpy(copy.data(), block->data, sizeof(block->data));
    outputs[{ this, index }] = copy;
}

audio_block_t* AudioStream::receiveReadOnly(unsigned int index)
{
    auto it = inputs.find({ this, index });
    if (it == inputs.end())
        return nullptr;
    audio_block_t* b = it->second;
    inputs.erase(it);
    return b;
}

audio_block_t* AudioStream::receiveWritable(unsigned int index)
{
    audio_block_t* in = receiveReadOnly(index);
    if (in != nullptr && in->ref_count > 1)
    {
        audio_block_t* copy = allocate();
        if (copy != nullptr)
            std::memcpy(copy->data, in->data, sizeof(in->data));
        release(in);
        in = copy;
    }
    return in;
}

// AudioStream.h declares this as a friend; the tests use it to empty the update list between objects, so
// constructing a new object never walks pointers to ones that were already destroyed.
void artificial_isr(void) { AudioStream::first_update = nullptr; }

// Referenced by AudioStream.h / AudioConnection but never used by the tests.
void AudioStream::initialize_memory(audio_block_t*, unsigned int) {}
void AudioStream::update_all(void) {}
void AudioConnection::connect(void) {}
void AudioConnection::disconnect(void) {}

namespace audio_test
{
void feed(AudioStream* to, unsigned input, const Block& samples)
{
    audio_block_t* b = poolAllocate();
    assert(b != nullptr);
    std::memcpy(b->data, samples.data(), sizeof(b->data));
    inputs[{ to, input }] = b;
}

std::optional<Block> take(AudioStream* from, unsigned output)
{
    auto it = outputs.find({ from, output });
    if (it == outputs.end())
        return std::nullopt;
    Block b = it->second;
    outputs.erase(it);
    return b;
}

void resetStreams()
{
    artificial_isr();
    inputs.clear();
    outputs.clear();
    for (int i = 0; i < kPoolSize; i++)
        used[i] = false;
    blocksAllocated = 0;
}

int blocksInUse() { return blocksAllocated; }
} // namespace audio_test

namespace audio_test
{
Block sine(int blockIdx, float hz, float amp)
{
    Block b;
    for (int n = 0; n < AUDIO_BLOCK_SAMPLES; n++)
    {
        const double t = (blockIdx * AUDIO_BLOCK_SAMPLES + n) / 44100.0;
        b[n] = static_cast<int16_t>(std::lround(amp * 32767.0 * std::sin(2.0 * 3.14159265358979 * hz * t)));
    }
    return b;
}

Block rampedSine(int blockIdx, int blocks, float hz)
{
    const double pos = static_cast<double>(blockIdx) / blocks;
    const double amp = pos < 0.5 ? pos * 2.0 : (1.0 - pos) * 2.0;
    return sine(blockIdx, hz, static_cast<float>(amp));
}

Block square(int blockIdx, float hz, int16_t amp)
{
    Block b;
    const int period = std::max(2, static_cast<int>(44100.0f / hz));
    for (int n = 0; n < AUDIO_BLOCK_SAMPLES; n++)
        b[n] = (((blockIdx * AUDIO_BLOCK_SAMPLES + n) / (period / 2)) & 1) ? amp : static_cast<int16_t>(-amp);
    return b;
}

Block noise(uint32_t& state, int16_t amp)
{
    Block b;
    for (auto& s : b)
    {
        state = state * 1664525u + 1013904223u;
        s = static_cast<int16_t>((static_cast<int32_t>(state >> 16) - 32768) * amp / 32768);
    }
    return b;
}

Block constant(int16_t v)
{
    Block b;
    b.fill(v);
    return b;
}
} // namespace audio_test
