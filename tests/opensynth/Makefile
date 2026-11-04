# Host tests for the open synth engines (core/synth/open). Plain g++, no ARM toolchain.
#
#   make -C tests/opensynth                  build and run under AddressSanitizer + UBSan, -Werror
#   make -C tests/opensynth syntax           compile the engine sources with the firmware's include path
#                                            and -fno-builtin, through config.h (rung 1)
#   make -C tests/opensynth goldens OT_ROOT=<OpenTracker checkout>
#                                            regenerate goldens.inc from OpenTracker's own engine
#
# Parity flags: both builds use the same flags, and -ffp-contract=off keeps the host from fusing a*b+c
# into one FMA, so float results are plain IEEE single steps on both sides.

ROOT     := ../..
OPEN     := $(ROOT)/core/synth/open
BUILD    := build
CXX      ?= g++
FLAGS    := -std=gnu++20 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined -DOPENSYNTH_HOST
CE_SRCS  := $(OPEN)/engines/Acid.cpp $(OPEN)/OpenSynthRegistry.cpp $(OPEN)/OpenSynthParams.cpp
DEPS     := $(CE_SRCS) $(wildcard $(OPEN)/*.h $(OPEN)/engines/*.h) scenarios.h goldens.inc

.PHONY: all test syntax goldens clean
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

goldens:
ifndef OT_ROOT
	$(error set OT_ROOT to an OpenTracker checkout)
endif
	@mkdir -p $(BUILD)
	$(CXX) $(FLAGS) -I. -I$(OT_ROOT)/src/core -I$(ROOT)/core/synth gen_goldens.cpp \
	  $(OT_ROOT)/src/core/audio/engines/acd.cpp $(OPEN)/OpenSynthParams.cpp \
	  -o $(BUILD)/gen_goldens
	./$(BUILD)/gen_goldens > goldens.inc
	@echo "goldens.inc written from OpenTracker at $$(git -C $(OT_ROOT) rev-parse --short HEAD)"

clean:
	rm -rf $(BUILD)
