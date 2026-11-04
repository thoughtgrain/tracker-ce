# Host tests for ACID on Tracker+ and Tracker Mini (core/synth/engines/AcidSynthVoice.cpp on the open engine).
# Plain g++, no ARM toolchain.
#
#   make -C tests/acid          build and run under AddressSanitizer + UBSan
#   make -C tests/acid clean
#
# Built like the IMX firmware sees it: FEATURE_SYNTH and PLATFORM_IMX (the open engine at 44.1 kHz).

ROOT     := ../..
BUILD    := build
CXX      ?= g++
OPEN     := $(ROOT)/core/synth/open
FLAGS    := -std=gnu++20 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined -DFEATURE_SYNTH -DFMT_HEADER_ONLY -DOPENSYNTH_HOST -DPLATFORM_IMX
INCLUDES := -I$(ROOT)/core/synth/engines -I$(ROOT)/core/synth -I$(ROOT)/core/config -I$(ROOT)/synth/include \
            -I$(ROOT)/util/include -I$(ROOT)/util/3rdparty
SRCS     := $(ROOT)/core/synth/engines/AcidSynthVoice.cpp $(OPEN)/engines/Acid.cpp $(OPEN)/engines/Toro.cpp \
            $(OPEN)/OpenSynthRegistry.cpp $(OPEN)/OpenSynthParams.cpp $(OPEN)/OpenSynthLayout.cpp
DEPS     := $(SRCS) $(wildcard $(OPEN)/*.h $(OPEN)/engines/*.h $(ROOT)/core/synth/engines/*.h) \
            ../toro/harness.h ../toro/stub_patch.h

.PHONY: all test clean
all: test

test: $(BUILD)/acid_sdk_test
	./$(BUILD)/acid_sdk_test

$(BUILD)/acid_sdk_test: acid_sdk_test.cpp $(DEPS)
	@mkdir -p $(BUILD)
	$(CXX) $(FLAGS) $(INCLUDES) -o $@ acid_sdk_test.cpp $(SRCS)

clean:
	rm -rf $(BUILD)
