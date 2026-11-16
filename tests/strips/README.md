# Sample strip model tests

`make -C tests/strips` builds the real `core/source/sampleStrip.cpp` on the host (g++, AddressSanitizer and
UBSan, `-Werror`) and checks it: where each play mode is after a given time, how a peak reads as a level,
and what a track's rows show (note starts, one-shots running out, note offs, loops wrapping, notes carried
across the pattern's end, the "Long samples" filter). One scenario's full output is also pinned by a golden
hash; `make print-goldens` prints the current value, and it should only change with an intended change to the
model.

What this doesn't prove: anything about drawing (that's the pattern editor and the display list), the ARM
compiler's float results, or that the picture matches what the audio engine actually plays. The model is
deliberately an approximation of `play_memory_*.cpp`, as `sampleStrip.h` says.
