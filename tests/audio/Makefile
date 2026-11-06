# Host tests for firmware audio DSP classes (limiter, state-variable filter). Plain g++, no ARM toolchain.
#
#   make -C tests/audio                 build and run under AddressSanitizer + UBSan
#   make -C tests/audio print-goldens   print current hashes (only after an INTENDED behaviour change)
#   make -C tests/audio clean
#
# The REAL firmware sources are compiled; shim/host_prelude.h is force-included to replace the two headers
# that can't build on a desktop (ARM asm intrinsics, the 64-bit-hostile project struct), and stream_stub.cpp
# provides AudioStream's block plumbing. See README.md for what these tests do and don't prove.
#
# Flags that matter for exactness:
#   -ffp-contract=off   never fuse a*b+c into one FMA on the host, so float results are plain IEEE single
#                       steps (ARM GCC may fuse them; see README).
#   -fno-lifetime-dse   keep the zero-fill done before placement new (Fresh<T>), which models .bss.

ROOT     := ../..
AUDIO    := $(ROOT)/core/libs/Audio
BUILD    := build
CXX      ?= g++
CXXFLAGS := -std=gnu++20 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers \
            -ffp-contract=off -fno-lifetime-dse \
            -fsanitize=address,undefined -fno-sanitize-recover=undefined \
            -include shim/host_prelude.h
INCLUDES := -I. -I$(AUDIO) -I$(ROOT)/core/config -I$(ROOT)/core/include -I$(ROOT)/core/source \
            -I$(ROOT)/core/AudioEngine/ConstantValues

COMMON   := stream_stub.cpp
DEPS     := harness.h stream_stub.h shim/host_prelude.h $(COMMON)

.PHONY: all test print-goldens clean
all: test

test: $(BUILD)/limiter_test $(BUILD)/filter_test
	./$(BUILD)/limiter_test
	./$(BUILD)/filter_test

print-goldens: $(BUILD)/limiter_test $(BUILD)/filter_test
	@echo '// limiter:' && ./$(BUILD)/limiter_test --print-goldens
	@echo '// filter:'  && ./$(BUILD)/filter_test --print-goldens

$(BUILD)/limiter_test: limiter_test.cpp goldens/limiter.inc $(AUDIO)/effect_limiter.cpp $(AUDIO)/effect_limiter.h $(DEPS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ limiter_test.cpp $(AUDIO)/effect_limiter.cpp $(COMMON)

$(BUILD)/filter_test: filter_test.cpp goldens/filter.inc $(AUDIO)/filter_variable.cpp $(AUDIO)/filter_variable.h $(DEPS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ filter_test.cpp $(AUDIO)/filter_variable.cpp $(COMMON)

clean:
	rm -rf $(BUILD)
