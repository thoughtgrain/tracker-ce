# core/libs/Audio: audio nodes

This adds to the root [AGENTS.md](../../../AGENTS.md); it doesn't replace it.

- **Every `update()` here runs in the audio interrupt.** `AudioStream::update_all` walks the node list and calls each active node's `update()` once per block. Treat every line of an `update()` as interrupt code: no SD, no heap, no `double`, no UI.
- **Blocks come from a fixed pool.** `AudioStream::allocate` can return `NULL` when the pool runs out, so check it. Every block you `allocate` or `receiveReadOnly` must be released (`AudioStream::release`) or handed on with `transmit` before `update()` returns, on every early-return path. A leaked block never comes back, and the pool drains a little more each time that path runs.
- **Hot code is placed in ITCM with `AUDIO_CORE_RAM`.** It's fast but small (the CI build's memory report shows what's left), so add it to code that runs per block, not to setup code.
- **Export renders through the same nodes.** `artificial_isr` calls every node's `update()` when audio export runs, while `AudioStream::renderMode` makes `AudioStream::update_all` return early. A change to a node's output changes exported files too, which is one more reason audio changes need a bit-exact host test (rung 2 in the root file).
