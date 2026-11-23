// A very small test runner, so the suite has no dependency to install or vendor.
//
//   TEST("name") { ... CHECK(x); CHECK_EQ(a, b); }
//       Must pass.
//
//   KNOWN_BUG("16101", "name") { ... }
//       Describes the behaviour I want, for a bug that isn't fixed yet. It's expected to
//       fail, and a failure doesn't fail the run. When the fix lands the test starts
//       passing, the run fails with "now passes", and the fix's PR turns it into a TEST.
//       That keeps the bug's reproduction in the tree, runnable, from the day it's found.
#ifndef TESTS_SEQUENCER_SUPPORT_TESTING_H_
#define TESTS_SEQUENCER_SUPPORT_TESTING_H_

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace testing
{

struct Case
{
    const char* bugId; // nullptr for an ordinary test
    const char* name;
    void (*run)();
};

std::vector<Case>& registry();
int& failuresInCurrentCase();
void reportFailure(const char* file, int line, const std::string& what);

struct Registrar
{
    Registrar(const char* bugId, const char* name, void (*run)())
    {
        registry().push_back(Case{ bugId, name, run });
    }
};

// Golden hashes: named values kept in goldens.inc. `--print-goldens` prints fresh ones.
bool printingGoldens();
void checkGolden(const char* file, int line, const char* name, uint64_t actual);

int runAll(int argc, char** argv);

} // namespace testing

#define TESTING_CONCAT2(a, b) a##b
#define TESTING_CONCAT(a, b) TESTING_CONCAT2(a, b)

#define TESTING_DEFINE(bugId, name)                                                                \
    static void TESTING_CONCAT(testCase_, __LINE__)();                                             \
    static testing::Registrar TESTING_CONCAT(testRegistrar_, __LINE__)(                            \
      bugId, name, TESTING_CONCAT(testCase_, __LINE__));                                           \
    static void TESTING_CONCAT(testCase_, __LINE__)()

#define TEST(name) TESTING_DEFINE(nullptr, name)
#define KNOWN_BUG(bugId, name) TESTING_DEFINE(bugId, name)

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
            testing::reportFailure(__FILE__, __LINE__, "CHECK(" #condition ")");                   \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                 \
    do                                                                                             \
    {                                                                                              \
        const long long testingActual = static_cast<long long>(actual);                            \
        const long long testingExpected = static_cast<long long>(expected);                        \
        if (testingActual != testingExpected)                                                      \
            testing::reportFailure(__FILE__,                                                       \
                                   __LINE__,                                                       \
                                   std::string(#actual " == " #expected ": got ") +                \
                                     std::to_string(testingActual) + ", expected " +               \
                                     std::to_string(testingExpected));                             \
    } while (0)

#define CHECK_GOLDEN(name, value) testing::checkGolden(__FILE__, __LINE__, name, value)

#endif
