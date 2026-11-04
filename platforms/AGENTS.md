# platforms: MK66 (Original Tracker) and IMX (Tracker+, Tracker Mini)

This adds to the root [AGENTS.md](../AGENTS.md); it doesn't replace it.

- **The two trees are never linked together.** `platforms/mk66` builds the Original Tracker, and `platforms/imx` builds Tracker+ and Tracker Mini. Each links `core/` with its own drivers, so the same function name can exist in both (`dataTransferComplite` does). A change to shared behaviour usually needs both trees changed, and testing on a device from each.
- **The interrupt entry points live here.** The audio DMA callbacks (`updateBuffers` and `updateInputBuffers`) call into `core/` through `Tracker`. The MIDI clock timer (`TMR4_IRQHandler` on IMX, an IntervalTimer in `platforms/mk66/source/TimerImpl.cpp` on MK66) runs the callback `core/` hands to `scheduleMidiClockUs`. The full list of entry points is in section 2 of the root `AGENTS.md`.
- **Platform code and `core/` meet at the interfaces in `core/include/Platform.h`** (`Platform`, `Timer`, `FileSystem` and the rest). Nearly every method there is pure virtual, so a new one has to be implemented in both trees before either builds. That's the point: add to the interface rather than reaching into `core/` internals from a driver.
- **Third-party code is vendored: `platforms/mk66/3rdparty`, `platforms/imx/3rdparty`, `platforms/imx/sdmmc` and the NXP SDK folders next to them.** Keep changes there minimal and note them in the PR. Licences are listed in `NOTICE.md`.
