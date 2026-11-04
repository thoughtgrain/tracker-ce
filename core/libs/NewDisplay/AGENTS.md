# core/libs/NewDisplay: FT812 display and controls

This adds to the root [AGENTS.md](../../../AGENTS.md); it doesn't replace it.

- **The frame budget drops controls silently, and the oldest go first.** `cDisplay::step3` (the full-screen pass `cDisplay::update` runs) appends each visible control's display list in reverse creation order and stops at the first one that doesn't fit the 8,172-byte budget. Everything created before that control isn't drawn, with no error. In a DEBUG build, `lastMemoryUsed` holds the last frame's total. `display_controls` lists the budgets, every control class and every create site.
- **Control setters can be reached from the interrupt.** `cDisplay::setControlText` and friends are called from MIDI handlers today (see `core/source/AGENTS.md`). They only touch the control's state, but they race with `cDisplay::update` in the main loop. Don't add more interrupt-side callers; set a flag and refresh from the screen's `update()`.
- **Graphics memory (RAM_G) is laid out by hand.** Bitmaps, control slots and decoded images share the FT812's graphics memory at fixed addresses (`displayStructs.h`). `display_controls` computes the regions and reports overlaps. Check it after adding a bitmap or a control.
- **Style bits are a shared bit field.** New styles go in the free bits `display_controls` reports, never on top of a used one.
