# Drum Loop Gen host test

`make -C tests/ptfx-drum-loop` compiles `editor-fx-sdk/effects/drum_loop_gen.c` with the host's
compiler (AddressSanitizer, UBSan, `-Werror`), drives it the way `mtEffectModular` does, and checks:

- the output is exactly Steps sixteenths long at the chosen BPM (16/20/24 steps at 60/120/200 BPM);
- the same parameters always render the same audio, so Apply matches the last Preview;
- changing Seed changes the pattern in every style;
- each style is audible, barely clips, and starts on a hit;
- Machine blends continuously (0 = CR-78, 50 = 808, 100 = 909): each point is audible and
  different, and one step of the control is a small change;
- Morph sweeps the machine across the loop, starting at Machine and ending nearer its target;
- one golden hash per style, so any change to the sound is deliberate (`make print-goldens`, then
  update `goldens.mk`).

It also writes `build/style0..3.wav` and `build/machine-*.wav` and `build/morph-cr78-to-909.wav` to listen to. It doesn't run the ARM build or the real
Sample Editor; that's a hardware check.
