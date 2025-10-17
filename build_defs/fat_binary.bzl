"""Lightweight alias helper maintained for compatibility."""

def enable_fat_binaries():
    # No-op placeholder kept for compatibility with downstream builds.
    pass

def fat_binary_alias(name, bin, **kwargs):
    """Create a thin alias for a binary target."""
    native.alias(
        name = name,
        actual = bin,
        **kwargs
    )
