"""
Helpers to export individual source files with valid Buck2 target names.
"""

_EXPORT_PREFIX = "__exported__"

def _is_ascii_alnum(ch):
    return (
        ("a" <= ch and ch <= "z") or
        ("A" <= ch and ch <= "Z") or
        ("0" <= ch and ch <= "9")
    )

def _sanitize_export_name(path):
    parts = []
    # Buck2's Starlark currently does not support iterating over strings directly.
    for idx in range(len(path)):
        ch = path[idx:idx + 1]
        if _is_ascii_alnum(ch) or ch == "_":
            parts.append(ch)
        elif ch == "/":
            parts.append("__SLASH__")
        elif ch == ".":
            parts.append("__DOT__")
        elif ch == "-":
            parts.append("__DASH__")
        else:
            parts.append("_x{:02x}_".format(ord(ch)))
    return _EXPORT_PREFIX + "".join(parts)

def _build_label(package_prefix, target):
    sanitized = _sanitize_export_name(target)
    if package_prefix:
        return "{}:{}".format(package_prefix, sanitized)
    return ":" + sanitized

def exported_file_label(label):
    """
    Convert a logical label like "//llvm:include/llvm/IR/Intrinsics.td"
    into the sanitized label that exports_files() declared.
    """
    if label.startswith("@"):
        package, target = label.split(":", 1)
        return _build_label(package, target)
    if label.startswith("//"):
        package, target = label.split(":", 1)
        return _build_label(package, target)
    if label.startswith(":"):
        return _build_label("", label[1:])
    return _build_label("", label)

def exports_files(files, visibility = ["PUBLIC"], mode = "reference"):
    """Takes a list of files, and exports each of them."""
    for file in files:
        matches = glob([file])
        if not matches:
            continue
        native.export_file(
            name = _sanitize_export_name(file),
            src = file,
            mode = mode,
            visibility = visibility,
        )
