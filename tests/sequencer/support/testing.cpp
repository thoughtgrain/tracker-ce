#include "testing.h"

#include <cstring>

namespace testing
{

namespace
{
struct Golden
{
    const char* name;
    uint64_t value;
};

const Golden kGoldens[] = {
#include "../goldens.inc"
};

bool g_printGoldens = false;
bool g_quietFailures = false;
} // namespace

std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

int& failuresInCurrentCase()
{
    static int failures = 0;
    return failures;
}

void reportFailure(const char* file, int line, const std::string& what)
{
    failuresInCurrentCase()++;
    // A known bug is expected to fail; show only its first failure, as a note.
    if (g_quietFailures && failuresInCurrentCase() > 1)
        return;
    std::printf("      %s:%d: %s\n", file, line, what.c_str());
}

bool printingGoldens()
{
    return g_printGoldens;
}

void checkGolden(const char* file, int line, const char* name, uint64_t actual)
{
    if (g_printGoldens)
    {
        std::printf("{ \"%s\", 0x%016llxULL },\n", name, static_cast<unsigned long long>(actual));
        return;
    }
    for (const Golden& golden : kGoldens)
    {
        if (std::strcmp(golden.name, name) != 0)
            continue;
        if (golden.value != actual)
        {
            char text[160];
            std::snprintf(text,
                          sizeof text,
                          "golden \"%s\": got 0x%016llx, expected 0x%016llx",
                          name,
                          static_cast<unsigned long long>(actual),
                          static_cast<unsigned long long>(golden.value));
            reportFailure(file, line, text);
        }
        return;
    }
    reportFailure(file, line, std::string("golden \"") + name + "\" is missing from goldens.inc");
}

int runAll(int argc, char** argv)
{
    const char* filter = nullptr;
    for (int arg = 1; arg < argc; arg++)
    {
        if (std::strcmp(argv[arg], "--print-goldens") == 0)
            g_printGoldens = true;
        else
            filter = argv[arg];
    }

    int passed = 0;
    int failed = 0;
    int knownBugs = 0;
    int nowPassing = 0;

    for (const Case& testCase : registry())
    {
        if (filter != nullptr && std::strstr(testCase.name, filter) == nullptr &&
            (testCase.bugId == nullptr || std::strstr(testCase.bugId, filter) == nullptr))
            continue;

        failuresInCurrentCase() = 0;
        g_quietFailures = testCase.bugId != nullptr;

        if (!g_printGoldens)
        {
            if (testCase.bugId != nullptr)
                std::printf("  [bug %s] %s\n", testCase.bugId, testCase.name);
            else
                std::printf("  %s\n", testCase.name);
        }

        testCase.run();

        const bool caseFailed = failuresInCurrentCase() > 0;
        if (testCase.bugId == nullptr)
        {
            caseFailed ? failed++ : passed++;
        }
        else if (caseFailed)
        {
            knownBugs++;
            if (!g_printGoldens)
                std::printf("      still present (expected until the fix lands)\n");
        }
        else
        {
            nowPassing++;
            std::printf("      NOW PASSES: bug %s looks fixed. Change KNOWN_BUG to TEST.\n",
                        testCase.bugId);
        }
    }

    if (g_printGoldens)
        return 0;

    std::printf("\n%d passed, %d failed, %d known bug(s) still present, %d known bug(s) now passing\n",
                passed,
                failed,
                knownBugs,
                nowPassing);
    return (failed == 0 && nowPassing == 0) ? 0 : 1;
}

} // namespace testing

int main(int argc, char** argv)
{
    return testing::runAll(argc, argv);
}
