# Host tests for the TORO synth engine. Plain g++, no ARM toolchain needed.
#
#   make -C tests/toro          build and run all three suites under AddressSanitizer + UBSan
#   make -C tests/toro clean
#
# The voice suite compiles the real core/synth/engines/ToroSynthVoice.cpp against the real PolySynth SDK
# headers, with FEATURE_SYNTH defined the way the IMX build defines it. The open parity suite builds the SDK
# voice and the open engine (core/synth/open/engines/Toro.cpp) side by side; PLATFORM_IMX puts the open
# engine at 44.1 kHz, the rate the SDK voice is fixed to.

ROOT     := ../..
BUILD    := build
CXX      ?= g++
CXXFLAGS := -std=gnu++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined
INCLUDES := -I$(ROOT)/core/synth/engines -I$(ROOT)/core/synth -I$(ROOT)/core/config -I$(ROOT)/synth/include \
            -I$(ROOT)/util/include -I$(ROOT)/util/3rdparty

.PHONY: all test clean
all: test

OPEN     := $(ROOT)/core/synth/open
OPEN_SRC := $(OPEN)/engines/Toro.cpp $(OPEN)/engines/Acid.cpp $(OPEN)/OpenSynthRegistry.cpp $(OPEN)/OpenSynthParams.cpp

test: $(BUILD)/core_test $(BUILD)/voice_test $(BUILD)/open_parity_test
	./$(BUILD)/core_test
	./$(BUILD)/voice_test
	./$(BUILD)/open_parity_test

$(BUILD)/core_test: core_test.cpp harness.h $(ROOT)/core/synth/open/engines/ToroCore.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ core_test.cpp

$(BUILD)/voice_test: voice_test.cpp harness.h sdk_fixture.h $(ROOT)/core/synth/open/engines/ToroCore.h \
                     $(ROOT)/core/synth/engines/ToroSynthVoice.cpp $(ROOT)/core/synth/engines/ToroSynthVoice.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -DFEATURE_SYNTH -DFMT_HEADER_ONLY $(INCLUDES) -o $@ voice_test.cpp $(ROOT)/core/synth/engines/ToroSynthVoice.cpp

$(BUILD)/open_parity_test: open_parity_test.cpp harness.h sdk_fixture.h $(OPEN_SRC) $(wildcard $(OPEN)/*.h) \
                           $(wildcard $(OPEN)/engines/*.h) $(ROOT)/core/synth/engines/ToroSynthVoice.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -ffp-contract=off -DFEATURE_SYNTH -DFMT_HEADER_ONLY -DOPENSYNTH_HOST -DPLATFORM_IMX \
	  $(INCLUDES) -o $@ open_parity_test.cpp $(ROOT)/core/synth/engines/ToroSynthVoice.cpp $(OPEN_SRC)

clean:
	rm -rf $(BUILD)
