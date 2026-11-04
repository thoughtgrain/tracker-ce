# core/libs/NewDisplay: FT812 display and controls

This adds to the root [AGENTS.md](../../../AGENTS.md); it doesn't replace it.

- **The frame budget drops controls silently, and the oldest go first.** `cDisplay::step3` (the full-screen pass `cDisplay::update` runs) appends each visible control's display list in reverse creation order and stops at the first one that doesn't fit the 8,172-byte budget. Everything created before that control isn't drawn, with no error. In a DEBUG build, `lastMemoryUsed` holds the last frame's total.
- **Control setters can be reached from the interrupt.** `cDisplay::setControlText` and friends are called from MIDI handlers today (see `core/source/AGENTS.md`). They only touch the control's state, but they race with `cDisplay::update` in the main loop. Don't add more interrupt-side callers; set a flag and refresh from the screen's `update()`.
- **Graphics memory (RAM_G) is laid out by hand.** Bitmaps sit below `controlsRamStartAddress`. Control slot `i` sits at `controlsRamStartAddress` + `i` × `controlsRamAddressStep`, for `controlsCount` slots (`displayStructs.h`). Decoded images reuse that space: the image viewer decodes at `ImageViewerJpegAddress`, inside slot 24 and up, and the project cover at `imgRamAddress`, inside slots 63–64. Work out the addresses by hand before adding a bitmap, raising the control count or decoding somewhere new.
- **Style bits are a shared bit field.** The `controlStyle*` values in `displayStructs.h` use bits 0–29, so only 30 and 31 are left. Never reuse a bit.
