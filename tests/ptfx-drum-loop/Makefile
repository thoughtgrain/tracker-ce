# Host test for the Drum Loop Gen .ptfx (editor-fx-sdk/effects/drum_loop_gen.c).
# `make` builds and runs it; `make print-goldens` prints new hashes after an intended change.
ROOT := ../..
CC ?= gcc
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined \
          -I$(ROOT)/editor-fx-sdk
include goldens.mk
GOLDENS := -DGOLDEN0=$(G0) -DGOLDEN1=$(G1) -DGOLDEN2=$(G2) -DGOLDEN3=$(G3)

test: build/test
	cd . && ./build/test

build/test: test_drum_loop.c $(ROOT)/editor-fx-sdk/effects/drum_loop_gen.c goldens.mk
	@mkdir -p build
	$(CC) $(CFLAGS) $(GOLDENS) test_drum_loop.c $(ROOT)/editor-fx-sdk/effects/drum_loop_gen.c -lm -o $@

print-goldens: build/test
	./build/test --print-goldens

clean:
	rm -rf build

.PHONY: test print-goldens clean
