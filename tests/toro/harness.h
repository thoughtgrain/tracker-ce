#pragma once

// Tiny test harness shared by the TORO host tests. No framework: each check prints what failed and counts it,
// and main() returns non-zero if anything failed, so `make` and CI can gate on it.

#include <cstdint>
#include <cstdio>

namespace toro_test
{
inline int failures = 0;
inline int checks = 0;

inline uint64_t fnv1a(uint64_t h, int32_t x)
{
    for (int b = 0; b < 4; b++)
    {
        h ^= static_cast<uint64_t>((x >> (8 * b)) & 0xFF);
        h *= 1099511628211ull;
    }
    return h;
}
constexpr uint64_t kFnvSeed = 1469598103934665603ull;

inline int finish(const char* suite)
{
    std::printf("%s: %d checks, %d failed\n", suite, checks, failures);
    return failures ? 1 : 0;
}
} // namespace toro_test

#define CHECK(cond, ...)                                                                                       \
    do                                                                                                         \
    {                                                                                                          \
        ::toro_test::checks++;                                                                                 \
        if (!(cond))                                                                                           \
        {                                                                                                      \
            ::toro_test::failures++;                                                                           \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                   \
            std::printf(__VA_ARGS__);                                                                          \
            std::printf("\n");                                                                                 \
        }                                                                                                      \
    } while (0)
