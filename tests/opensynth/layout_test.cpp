// Patch editor layout of the open engines (core/synth/open/OpenSynthLayout.*).
//
// What this proves:
//  - Every engine with a parameter table has a layout: at most kMaxSections sections, every parameter
//    in exactly one, and every section gets an icon in the patch editor.
//  - sectionIcon() follows the patch editor's own rules: this test reads SynthPatchEditor.cpp and checks
//    it tests the same words in the same order, so the two can't drift apart unnoticed.
//
//   make -C tests/opensynth                  (runs with the other suites)
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "open/OpenSynthEngine.h"
#include "open/OpenSynthLayout.h"
#include "open/OpenSynthParams.h"

namespace {

using opensynth::SectionIcon;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The editor's words, in the order it tests them, with the icon each gives.
struct Rule {
    const char* word;
    SectionIcon icon;
};
const Rule kRules[] = {
    {"Amp Envelope", SectionIcon::kEnvelope}, {"Amplifier", SectionIcon::kEnvelope},
    {"Envelope", SectionIcon::kEnvelope},     {"Filter", SectionIcon::kFilter},
    {"Voice", SectionIcon::kVoice},           {"LFO", SectionIcon::kLfo},
    {"Modulation", SectionIcon::kModulation}, {"Oscillator", SectionIcon::kOscillator},
    {"Shaper", SectionIcon::kShaper},         {"Kick", SectionIcon::kKick},
    {"Tom", SectionIcon::kTom},               {"Snare", SectionIcon::kSnare},
    {"Hihat", SectionIcon::kHihat},           {"Cymbal", SectionIcon::kCymbal},
    {"Percussion", SectionIcon::kPercussion}, {"Mixer", SectionIcon::kMixer},
    {"FX", SectionIcon::kFX},
};

void testRulesMatchTheEditor() {
    std::ifstream f(OPENSYNTH_ROOT "/core/interface/instrumentEditor/SynthPatchEditor.cpp");
    check(f.good(), "SynthPatchEditor.cpp is readable");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();
    // Every word the editor tests, in source order: sectionName.find("<word>").
    std::vector<std::string> words;
    const std::string key = "sectionName.find(\"";
    for (size_t at = src.find(key); at != std::string::npos; at = src.find(key, at + 1)) {
        const size_t start = at + key.size(), end = src.find('"', start);
        words.push_back(src.substr(start, end - start));
    }
    const size_t n = sizeof(kRules) / sizeof(kRules[0]);
    check(words.size() == n, "the editor tests as many words as sectionIcon()");
    for (size_t i = 0; i < n && i < words.size(); ++i) {
        if (words[i] != kRules[i].word) {
            std::printf("FAIL: editor rule %zu is \"%s\", sectionIcon() has \"%s\"\n", i, words[i].c_str(),
                        kRules[i].word);
            ++failures;
        }
    }
    for (const auto& r : kRules) {
        char what[96];
        std::snprintf(what, sizeof what, "\"%s\" gets its icon", r.word);
        check(opensynth::sectionIcon(r.word) == r.icon, what);
    }
}

void testFirstMatchWins() {
    check(opensynth::sectionIcon("Filter Envelope") == SectionIcon::kEnvelope, "Filter Envelope: envelope first");
    check(opensynth::sectionIcon("Mod Envelope") == SectionIcon::kEnvelope, "Mod Envelope: envelope");
    check(opensynth::sectionIcon("LFO Modulation") == SectionIcon::kLfo, "LFO before Modulation");
    check(opensynth::sectionIcon("Delay") == SectionIcon::kNone, "an unknown name has no icon");
    check(opensynth::sectionIcon("") == SectionIcon::kNone, "an empty name has no icon");
    check(opensynth::sectionIcon(nullptr) == SectionIcon::kNone, "no name has no icon");
}

void testEveryEngineLayout() {
    int engines = 0;
    for (int32_t id = 0; id < 64; ++id) {
        int nParams = 0;
        if (!opensynth::paramTable(id, &nParams)) continue;
        ++engines;
        const char* name = opensynth::openSynthEngine(id) ? opensynth::openSynthEngine(id)->name() : "?";
        char what[128];
        int nSections = 0;
        const opensynth::Section* s = opensynth::sectionTable(id, &nSections);
        std::snprintf(what, sizeof what, "%s has a layout", name);
        check(s && nSections > 0, what);
        std::snprintf(what, sizeof what, "%s fits the grid (%d sections)", name, nSections);
        check(nSections <= opensynth::kMaxSections, what);
        std::vector<int> seen(nParams, 0);
        for (int k = 0; s && k < nSections; ++k) {
            std::snprintf(what, sizeof what, "%s: section \"%s\" has an icon", name, s[k].name);
            check(opensynth::sectionIcon(s[k].name) != SectionIcon::kNone, what);
            std::snprintf(what, sizeof what, "%s: section \"%s\" isn't empty", name, s[k].name);
            check(s[k].count > 0, what);
            for (int j = 0; j < s[k].count; ++j) {
                std::snprintf(what, sizeof what, "%s: section \"%s\" names a real parameter", name, s[k].name);
                check(s[k].params[j] < nParams, what);
                if (s[k].params[j] < nParams) ++seen[s[k].params[j]];
            }
        }
        for (int p = 0; p < nParams; ++p) {
            std::snprintf(what, sizeof what, "%s: parameter %d is in exactly one section (%d)", name, p, seen[p]);
            check(seen[p] == 1, what);
        }
    }
    check(engines >= 2, "ACID and TORO both have parameter tables");
    int none = 1;
    check(opensynth::sectionTable(999, &none) == nullptr && none == 0, "no layout for an unknown id");
    std::printf("layouts: %d engines\n", engines);
}

}  // namespace

int main() {
    testRulesMatchTheEditor();
    testFirstMatchWins();
    testEveryEngineLayout();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
