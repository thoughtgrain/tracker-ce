# Recipe: add a UI screen (module)

How I add a new screen: a module the hardware buttons (and the Tracker Mini's menu) can open.

## Before you start

- Read `core/libs/NewDisplay/AGENTS.md` (frame budget, graphics memory) and the developer guide's [§6](../developer_guide.md#6-ui-ft812-display-config--bootloader-gotchas) on module slots.
- **Discuss first.** A new screen changes how people move around the instrument, so it needs a GitHub Discussion or Backstage topic before code (root `AGENTS.md`, section 7).
- The closest small example is the master screen: `core/interface/masterParams/` with `masterParams.h`, `gMasterParams.cpp` (drawing) and `fMasterParams.cpp` (input).

## Steps

1. **Class.** In `core/interface/<name>/`, declare `class c<Name> : public ModuleBase` and an `extern c<Name> <instance>;` in its header. Implement the pure virtuals from `core/interface/core/modulesBase.h`: `update`, `start`, `stop`, `initDisplayControls` and `destroyDisplayControls`.
2. **Files by job.** Put control creation and drawing in `g<Name>.cpp`, and button, encoder and pad handling in `f<Name>.cpp`. Everything else in `core/interface` follows that split, so people know where to look.
3. **Slot.** Add the instance to `Interface::modules` in `core/interface/core/interface.cpp`, **at the end**, and bump `Interface::modulesCount`. Navigation code uses hard-coded slot numbers, which is why slot 11 stays `nullptr` instead of being reused.
4. **Button (Original and Tracker+).** Add a row to `Interface::modulesButtons` (button, slot, mode) and bump `Interface::modulesButtonsCount`.
5. **Tracker Mini menu.** In `core/interface/core/interfaceMenuPopup.cpp`, add the name to `kModuleNamesList` and a row to `InterfaceMenuPopup::modules`, then bump `kMenuModulesCount`. The Mini has no dedicated buttons, so a screen missing from this menu is unreachable there.

## Pitfalls

- **Count your display-list bytes.** Every visible control shares the 8,172-byte frame budget, and past it the oldest controls silently disappear. Create controls in `initDisplayControls` and free them all in `destroyDisplayControls`; the control table is fixed-size (`controlsCount`).
- **Don't draw from the interrupt.** If MIDI or the sequencer needs to change what the screen shows, set a flag and refresh in `update()`.
- **Mini and desktop layouts differ.** Guard Mini-only layout with `TRACKER_MINI`.

## Verify

- Rung 1: syntax check against the real headers.
- Rung 3: the ARM build for all three devices.
- Rung 4 and 6: open the screen from the button and from the Mini menu, with the busiest content you can make. Check nothing at the edges disappears, then write the steps into the PR.
