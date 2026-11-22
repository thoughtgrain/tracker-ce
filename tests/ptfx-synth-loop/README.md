# Synth Loop Gen host test

`make -C tests/ptfx-synth-loop` compiles `editor-fx-sdk/effects/synth_loop_gen.c` with the host's
compiler (AddressSanitizer, UBSan, `-Werror`), drives it the way `mtEffectModular` does, and checks:

- the output is exactly Steps sixteenths long at the chosen BPM;
- every generated note is in the chosen key and scale, for all 4 modes, 10 scales, several roots
  and all 6 chord types (through a test-only hook, `PTFX_HOST_TEST`);
- the same parameters always render the same audio, so Apply matches the last Preview, and Seed
  regenerates in every mode;
- chords change the sound, and every mode is audible without much clipping;
- Style blends continuously from SH-101 (0) through Juno (50) to piano (100), with one step of
  the control a small change next to the full range;
- APHX off changes nothing, 25 is a subtle change, 100 a strong one, and it's repeatable;
- one golden hash per mode (`make print-goldens`, then update `goldens.mk`).

It also prints a rough render time for the heaviest case and writes `build/style-*.wav` and
`build/mode-*.wav` and `build/aphx-25.wav` / `aphx-100.wav` to listen to. It doesn't run the ARM build or the Sample Editor, and the
timings are this computer's, not a Tracker's.
