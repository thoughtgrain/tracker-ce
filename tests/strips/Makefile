# Host tests for the sample strips' model (core/source/sampleStrip.cpp). See README.md.
CXX      ?= g++
CXXFLAGS := -std=gnu++20 -Wall -Wextra -Werror -O1 -g -ffp-contract=off \
            -fsanitize=address,undefined -fno-sanitize-recover=all \
            -I../../core/source
SRC      := ../../core/source/sampleStrip.cpp test_strips.cpp

.PHONY: test print-goldens clean
test: test_strips
	./test_strips

print-goldens: test_strips
	./test_strips --print-goldens

test_strips: $(SRC) ../../core/source/sampleStrip.h ../../core/source/waveformPeak.h
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

clean:
	rm -f test_strips
