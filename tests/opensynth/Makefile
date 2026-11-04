# Host tests for the open synth engines (core/synth/open). Plain g++, no ARM toolchain.
#
#   make -C tests/opensynth                  build and run under AddressSanitizer + UBSan, -Werror
#   make -C tests/opensynth syntax           compile the engine sources with the firmware's include path
#                                            and -fno-builtin, through config.h (rung 1)
#   make -C tests/opensynth print-goldens    print goldens.inc for the current engines (only after an
#                                            INTENDED change to how they sound; say so in the PR)
#
# -ffp-contract=off keeps the host from fusing a*b+c into one FMA, so float results are plain IEEE single
# steps and the goldens don't depend on the host CPU.

ROOT     := ../..
OPEN     := $(ROOT)/core/synth/open
BUILD    := build
CXX      ?= g++
FLAGS    := -std=gnu++20 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined -DOPENSYNTH_HOST
CE_SRCS  := $(OPEN)/engines/Acid.cpp $(OPEN)/OpenSynthRegistry.cpp $(OPEN)/OpenSynthParams.cpp
DEPS     := $(CE_SRCS) $(wildcard $(OPEN)/*.h $(OPEN)/engines/*.h) scenarios.h goldens.inc

.PHONY: all test syntax print-goldens clean
all: test

test: $(BUILD)/acid_test
	./$(BUILD)/acid_test

$(BUILD)/acid_test: acid_test.cpp $(DEPS)
	@mkdir -p $(BUILD)
	$(CXX) $(FLAGS) -I. -I$(ROOT)/core/synth acid_test.cpp $(CE_SRCS) -o $@

# Rung 1: the sources as the firmware sees them (core/makefile's -I./config, real config.h, -fno-builtin),
# for both device families.
syntax:
	$(CXX) -std=gnu++20 -fsyntax-only -fno-builtin -Wall -Werror -I$(ROOT)/core/config -I$(ROOT)/core/synth \
	  $(CE_SRCS)
	$(CXX) -std=gnu++20 -fsyntax-only -fno-builtin -Wall -Werror -DPLATFORM_IMX -I$(ROOT)/core/config \
	  -I$(ROOT)/core/synth $(CE_SRCS)

print-goldens: acid_test.cpp $(filter-out goldens.inc,$(DEPS))
	@mkdir -p $(BUILD)
	@$(CXX) $(FLAGS) -Wno-unused-function -DOPENSYNTH_PRINT_GOLDENS -I. -I$(ROOT)/core/synth acid_test.cpp \
	  $(CE_SRCS) -o $(BUILD)/print_goldens
	@./$(BUILD)/print_goldens

clean:
	rm -rf $(BUILD)
