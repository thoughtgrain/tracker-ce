# Polyend Community Edition Synth Plugin SDK (.psyn) Makefile
# Builds standalone relocatable SD-card synth plugins (/Synths/*.psyn)

SDK_DIR       := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT     := $(abspath $(SDK_DIR)/..)

ifneq ($(wildcard $(REPO_ROOT)/agt/10.3/bin/arm-none-eabi-g++),)
  TOOLCHAIN   ?= $(REPO_ROOT)/agt/10.3/bin/arm-none-eabi-
else ifneq ($(wildcard $(REPO_ROOT)/../tracker-ce/agt/10.3/bin/arm-none-eabi-g++),)
  TOOLCHAIN   ?= $(REPO_ROOT)/../tracker-ce/agt/10.3/bin/arm-none-eabi-
else
  TOOLCHAIN   ?= arm-none-eabi-
endif

CXX           := $(TOOLCHAIN)g++
PYTHON        ?= python3
OBJDUMP       := $(TOOLCHAIN)objdump

# PORTABLE=1 builds for the Cortex-M4's FPv4 unit (the Original Tracker). That code also runs on the
# M7 (Tracker+, Mini). The default M7 build may use FPv5-only instructions (vrinta, vmaxnm, vsel,
# vcvta...), which fault on the M4. Measured on CHIP: 55 of them in the M7 build, none in the
# portable one, at the same size (docs/design/tape-mode-plan.md §3.1).
PORTABLE      ?= 0
ifeq ($(PORTABLE),1)
  CPU_FLAGS   := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
  BUILD_DIR   ?= $(SDK_DIR)/build-portable
else
  CPU_FLAGS   := -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
  BUILD_DIR   ?= $(SDK_DIR)/build
endif

CXXFLAGS      := -std=c++20 -includecstdint -felide-constructors -Wno-psabi \
                 -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics \
                 -fcheck-new -Wall -Wextra -fno-builtin -fno-common \
                 -ffunction-sections -fdata-sections -ffreestanding \
                 -fsingle-precision-constant -ffast-math -O3 -DNDEBUG \
                 $(CPU_FLAGS) \
                 -mfp16-format=ieee -mword-relocations \
                 -DPRODUCT=WAVE -DPRODUCT_WAVE \
                 -I$(SDK_DIR)/include \
                 -I$(REPO_ROOT)/synth/include \
                 -I$(REPO_ROOT)/synth/config \
                 -I$(REPO_ROOT)/util/include \
                 -I$(REPO_ROOT)/util/3rdparty \
                 -I$(REPO_ROOT)/thirdparty/boost/include \
                 -I$(REPO_ROOT)/thirdparty/enum_util/include

LDFLAGS       := -nostartfiles --specs=nano.specs \
                 -T$(SDK_DIR)/tools/psyn.ld \
                 -Wl,--gc-sections -Wl,--emit-relocs \
                 -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc \
                 -Wl,--wrap=_malloc_r -Wl,--wrap=_free_r -Wl,--wrap=_calloc_r

all: $(BUILD_DIR)/CHIP.psyn
ifeq ($(PORTABLE),1)
all: check-portable
endif

$(BUILD_DIR)/CHIP.elf: $(SDK_DIR)/engines/chip.cpp $(SDK_DIR)/include/psyn/psyn_plugin.h $(SDK_DIR)/include/psyn/psyn_cpp_bridge.h $(SDK_DIR)/tools/psyn.ld
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

HAS_SAMPLE_SLOTS ?= 0

$(BUILD_DIR)/CHIP.psyn: $(BUILD_DIR)/CHIP.elf $(SDK_DIR)/tools/psyn_builder.py
	$(PYTHON) $(SDK_DIR)/tools/psyn_builder.py \
		--elf $< \
		--out $@ \
		--code-name "CHIP" \
		--display-name "Chiptune" \
		--patch-folder "CHIP" \
		--type-id-wave 12 \
		--type-id-play-tracker 8 \
		--default-voices 4 \
		--max-voices 8 \
		--output-channels 1 \
		--renders-aux 0 \
		--has-sample-slots $(HAS_SAMPLE_SLOTS)

clean:
	rm -rf $(BUILD_DIR)

# Fails if the engine contains an instruction only the M7's FPv5 unit has, which would fault on the
# Original. Runs automatically with PORTABLE=1, and on its own against any build.
FPV5_ONLY     := '\bv(rint[anpmrxz]|sel(eq|ge|gt|vs)|maxnm|minnm|cvt[anpm])\.'

check-portable: $(BUILD_DIR)/CHIP.elf
	@n=$$($(OBJDUMP) -d $< | grep -cE $(FPV5_ONLY)); \
	if [ "$$n" != "0" ]; then \
		echo "$<: $$n FPv5-only instructions; it won't run on the Original (build with PORTABLE=1)"; \
		exit 1; \
	fi; \
	echo "$<: no FPv5-only instructions; runs on the Original, Tracker+ and Mini"

.PHONY: all clean check-portable
