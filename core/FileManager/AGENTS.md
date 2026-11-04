# core/FileManager: projects, patterns, instruments on SD

This adds to the root [AGENTS.md](../../AGENTS.md); it doesn't replace it.

- **Functions ending in `*Now()` do their SD work before they return.** `cFileManager::loadWorkspacePatternNow` and `cFileManager::saveWorkspacePatternNow` are synchronous reads and writes. They belong in the main loop only. Everything else here is a state machine driven by `cFileManager::update` from the main loop. If interrupt code needs a file operation, it sets a flag and the main loop does the work.
- **Interrupt code already reaches into this directory.** The sequencer calls `cFileManager::loadWorkspacePattern` from the interrupt, and that path can finish an autosave (see `core/source/AGENTS.md`). Before changing anything here, ask `interrupt_context` about it.
- **Old files must keep loading.** `cFileManager::checkVersionAndTranslate` upgrades patterns from every earlier `PATTERN_FILE_VERSION`. `mtFileManagerOldVersionRecovery.cpp` does the same for older projects and instruments. Add a step for a new version; never drop an old one.
- **Buffers handed to FatFs should be 4-byte aligned.** On IMX, reads of 512 bytes or more skip FatFs's own buffer and DMA straight into yours. The [developer guide §5](../../docs/developer_guide.md#5-sample-editor--ptfx-plugin-constraints) has the details.
