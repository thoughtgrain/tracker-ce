#pragma once

// A PolySynth Patch for host tests: answers parameter reads from a map and ignores everything else. Only
// value formatters read the patch. Shared by the SDK-hosted engine suites (tests/toro, tests/acid).
//
// Copyright (c) 2026 briannhinton. MIT.

#include "PolySynth/Patch.h"
#include "PolySynth/Sequencer.h"

#include <map>
#include <memory>

namespace polysynth
{

// A Patch that answers parameter reads from a map and ignores everything else. Only formatters read the patch.
class StubPatch final : public Patch
{
  public:
    std::map<int, paramvalue_t> values;
    Metadata metadata;

    optional_paramvalue_t getParamValue(int paramId) const override
    {
        auto it = values.find(paramId);
        return it == values.end() ? optional_paramvalue_t{} : optional_paramvalue_t{ it->second };
    }
    tiny_optional<float> getParamValueEditMapped(int) const override { return std::nullopt; }
    void setParamValueEditMapped(int, tiny_optional<float>, ParamChangeSource) override {}
    optional_paramvalue_t getParamFinalValue(int paramId) const override { return getParamValue(paramId); }
    void setParamValue(int paramId, optional_paramvalue_t v, ParamChangeSource) override
    {
        if (v)
            values[paramId] = *v;
    }
    void subscribeParamUpdates(std::weak_ptr<ParamChangeListener>) override {}
    void unsubscribeParamUpdates(const ParamChangeListener&) override {}
    void startBatchUpdate() override {}
    void finishBatchUpdate() override {}
    optional_paramvalue_t getMacroValue(int) const override { return std::nullopt; }
    void setMacroValue(int, optional_paramvalue_t, tiny_optional<int>) override {}
    void setMacroModifier(int, int, tiny_optional<int>) override {}
    void setMacroModifierAmount(int, int, float) override {}
    tiny_optional<float> getMacroModifierAmount(int, int) override { return std::nullopt; }
    void clearMacroModifiers() override {}
    void changeMacroModifierAmount(int, int, paramvalue_t) override {}
    void setMacroBipolar(int, bool) override {}
    void setMacroDisplayName(int, std::string) override {}
    const Sample* getSample(int) const override { return nullptr; }
    LoadStatus loadSample(int, const std::string&) override { return LoadStatus{}; }
    const Metadata& getMetadata() const override { return metadata; }
    const Sequencer& getSequencer() const override { return sequencer; }
    void setSequence(const Sequencer::Sequence&, int) override {}
    void setSequencerConfig(const Sequencer::Config&) override {}

  private:
    Sequencer sequencer;
};

} // namespace polysynth
