# Host tests for the sequencer (core/source/mtSequencer.cpp and mtSequencer_patternEdit.cpp).
#
#   make            build and run, for every device configuration
#   make og|plus|mini   one configuration
#   make print-goldens  print fresh golden hashes (only after an INTENDED behaviour change)
#   make clean
#
# FW_ROOT is the firmware checkout. It's the only link between this suite and the firmware,
# so the suite keeps working when it lives in its own repository:
#   make FW_ROOT=/path/to/tracker-ce
FW_ROOT ?= ../..
CXX ?= g++

CORE := $(FW_ROOT)/core

# The same include path the firmware makefile uses (core/makefile, INCLUDES).
# -isystem rather than -I: the firmware headers aren't warning-clean under -Wall -Wextra,
# and that isn't this suite's to fix. The suite's own code is still held to -Werror.
INCLUDES := $(addprefix -isystem $(CORE)/, include config source synth libs libs/Audio \
              libs/ElapsedMillis libs/SerialFlash libs/SPI libs/Audio/utility libs/FT812 \
              libs/fonts libs/NewDisplay interface effector FileManager Exporter AudioEngine \
              libs/Audio/Polyverb AudioEngine/ConstantValues AudioEngine/MasterEnhancer .) \
            -isystem $(FW_ROOT)/util/include -isystem $(FW_ROOT)/util/3rdparty \
            -isystem $(FW_ROOT)/synth/include

# The firmware sources under test, compiled unmodified.
FW_SOURCES := $(CORE)/source/mtSequencer.cpp \
              $(CORE)/source/mtSequencer_patternEdit.cpp \
              $(CORE)/source/scales.cpp \
              $(CORE)/source/util_math.cpp

SUPPORT_SOURCES := support/firmware_stubs.cpp support/testing.cpp
CASE_SOURCES := $(sort $(wildcard cases/*.cpp))

# -DNDEBUG matches the release firmware (core/makefile), where assert() compiles out.
# -ffp-contract=off keeps float results the same across host compilers.
COMMON := -std=gnu++20 -O1 -g -DNDEBUG -ffp-contract=off -fno-rtti \
          -fsanitize=address,undefined -fno-sanitize-recover=undefined \
          -include shim/host_prelude.h -Isupport $(INCLUDES)

FW_FLAGS := $(COMMON) -w
# -fno-access-control lets a test read the sequencer's private player state. Tests only
# read it; they drive the sequencer through its public functions.
OWN_FLAGS := $(COMMON) -Wall -Wextra -Werror -fno-access-control

# Device configurations, from core/makefile: which -D flags each product builds with.
DEFS_og   :=
DEFS_plus := -DPLATFORM_IMX -DFEATURE_SYNTH
DEFS_mini := -DPLATFORM_IMX -DFEATURE_SYNTH -DTRACKER_MINI
CONFIGS := og plus mini

test: $(CONFIGS)

define CONFIG_RULES
FW_OBJECTS_$(1) := $$(patsubst $$(CORE)/source/%.cpp,build/$(1)/fw/%.o,$$(FW_SOURCES))
OWN_OBJECTS_$(1) := $$(patsubst %.cpp,build/$(1)/%.o,$$(SUPPORT_SOURCES) $$(CASE_SOURCES))

build/$(1)/fw/%.o: $$(CORE)/source/%.cpp shim/host_prelude.h
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(FW_FLAGS) $$(DEFS_$(1)) -MMD -MP -c $$< -o $$@

build/$(1)/%.o: %.cpp shim/host_prelude.h goldens.inc
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(OWN_FLAGS) $$(DEFS_$(1)) -MMD -MP -c $$< -o $$@

build/$(1)/test: $$(FW_OBJECTS_$(1)) $$(OWN_OBJECTS_$(1))
	$$(CXX) $$(COMMON) $$^ -o $$@

$(1): build/$(1)/test
	@echo "== sequencer host tests: $(1)"
	@./build/$(1)/test

-include $$(FW_OBJECTS_$(1):.o=.d) $$(OWN_OBJECTS_$(1):.o=.d)
endef

$(foreach config,$(CONFIGS),$(eval $(call CONFIG_RULES,$(config))))

# Goldens are taken from the Tracker+ configuration; the cases that use them say so.
print-goldens: build/plus/test
	@./build/plus/test --print-goldens

clean:
	rm -rf build

.PHONY: test print-goldens clean $(CONFIGS)
