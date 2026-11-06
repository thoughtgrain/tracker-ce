# Host tests for the TORO synth engine. Plain g++, no ARM toolchain needed.
#
#   make -C tests/toro          build and run both suites under AddressSanitizer + UBSan
#   make -C tests/toro clean
#
# The voice suite compiles the real core/synth/engines/ToroSynthVoice.cpp against the real PolySynth SDK
# headers, with FEATURE_SYNTH defined the way the IMX build defines it.

ROOT     := ../..
BUILD    := build
CXX      ?= g++
CXXFLAGS := -std=gnu++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined
INCLUDES := -I$(ROOT)/core/synth/engines -I$(ROOT)/core/config -I$(ROOT)/synth/include \
            -I$(ROOT)/util/include -I$(ROOT)/util/3rdparty

.PHONY: all test clean
all: test

test: $(BUILD)/core_test $(BUILD)/voice_test
	./$(BUILD)/core_test
	./$(BUILD)/voice_test

$(BUILD)/core_test: core_test.cpp harness.h $(ROOT)/core/synth/engines/ToroCore.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ core_test.cpp

$(BUILD)/voice_test: voice_test.cpp harness.h $(ROOT)/core/synth/engines/ToroCore.h \
                     $(ROOT)/core/synth/engines/ToroSynthVoice.cpp $(ROOT)/core/synth/engines/ToroSynthVoice.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -DFEATURE_SYNTH -DFMT_HEADER_ONLY $(INCLUDES) -o $@ voice_test.cpp $(ROOT)/core/synth/engines/ToroSynthVoice.cpp

clean:
	rm -rf $(BUILD)
