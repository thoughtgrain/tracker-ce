"""The command-line macros that select code per device, so interrupt_context can say which devices compile
a call path. Hand-kept and small on purpose: these are the guards AGENTS.md section 4 tells people to use.
If a makefile adds or drops one of them, update this table (core/makefile and platforms/*/makefile)."""

TARGET_DEFINES: dict[str, dict[str, str]] = {
    "original": {"KINETISK": "1"},
    "plus": {"KINETISK": "1", "PLATFORM_IMX": "1", "FEATURE_SYNTH": "1"},
    "mini": {"KINETISK": "1", "PLATFORM_IMX": "1", "FEATURE_SYNTH": "1", "TRACKER_MINI": "1"},
}

# Macros undefined in release builds (debug builds add -DDEBUG). `#if DEBUG` evaluates to 0 for these
# instead of raising. PolySynth's config.h tests `#if PRODUCT == WAVE`; neither is a -D macro, so as in C
# both are 0.
RELEASE_UNDEFINED: frozenset[str] = frozenset({"DEBUG", "PRODUCT", "WAVE"})
