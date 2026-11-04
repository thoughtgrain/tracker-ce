"""Command-line macros that select code per target, used to evaluate #if branches when a parser needs the
view of one target. Kept small on purpose; tests/test_fx.py checks it against build_config's guard matrix
(what make actually passes), so it can't silently drift from the makefiles."""

TARGET_DEFINES: dict[str, dict[str, str]] = {
    "original": {"KINETISK": "1"},
    "plus": {"KINETISK": "1", "PLATFORM_IMX": "1", "FEATURE_SYNTH": "1"},
    "mini": {"KINETISK": "1", "PLATFORM_IMX": "1", "FEATURE_SYNTH": "1", "TRACKER_MINI": "1"},
}

# The guards whose presence this table must match exactly (checked against build_config).
TRACKED_GUARDS = ("PLATFORM_IMX", "FEATURE_SYNTH", "TRACKER_MINI", "KINETISK")

# Macros known to be undefined in the release builds `make all` produces (debug builds add -DDEBUG). A value
# test like `#if DEBUG` evaluates to 0 for these instead of raising. PRODUCT and WAVE are tested by the
# PolySynth config.h (`#if PRODUCT == WAVE`); neither is a -D macro in any build (PRODUCT is only a make
# variable), so as in C both are 0 and the test is true. Checked against build_config.
RELEASE_UNDEFINED: frozenset[str] = frozenset({"DEBUG", "PRODUCT", "WAVE"})
