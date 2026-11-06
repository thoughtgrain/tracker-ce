#pragma once

// Minimal check harness for the audio host tests: each CHECK prints what failed, main() returns non-zero if
// anything did, and golden hashes are FNV-1a over the rendered samples.

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace audio_test
{
inline int failures = 0;
inline int checks = 0;

constexpr uint64_t kFnvSeed = 1469598103934665603ull;
inline uint64_t fnv1a(uint64_t h, const void* data, size_t len)
{
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; i++)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

// `--print-goldens` prints computed hashes instead of comparing, for regenerating after an intended change.
inline bool printGoldens = false;
inline void parseArgs(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
        printGoldens = printGoldens || std::strcmp(argv[i], "--print-goldens") == 0;
}

inline int finish(const char* suite)
{
    std::printf("%s: %d checks, %d failed\n", suite, checks, failures);
    return failures ? 1 : 0;
}
} // namespace audio_test

#define CHECK(cond, ...)                                                                                       \
    do                                                                                                         \
    {                                                                                                          \
        ::audio_test::checks++;                                                                                \
        if (!(cond))                                                                                           \
        {                                                                                                      \
            ::audio_test::failures++;                                                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                   \
            std::printf(__VA_ARGS__);                                                                          \
            std::printf("\n");                                                                                 \
        }                                                                                                      \
    } while (0)

// Compare a hash against its golden value, or print it in --print-goldens mode.
#define CHECK_GOLDEN(name, hash, golden)                                                                       \
    do                                                                                                         \
    {                                                                                                          \
        if (::audio_test::printGoldens)                                                                        \
            std::printf("    { \"%s\", 0x%016llxull },\n", name, (unsigned long long)(hash));               \
        else                                                                                                   \
            CHECK((hash) == (golden), "%s: hash 0x%016llx, golden 0x%016llx", name,                            \
                  (unsigned long long)(hash), (unsigned long long)(golden));                                   \
    } while (0)
