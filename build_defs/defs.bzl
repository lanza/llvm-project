load("@prelude//utils:buckconfig.bzl", "read_bool", "read_list")

COMPILER_WARNING_FLAGS = [
    "-DLLVM_ENABLE_TELEMETRY=0",
    "-Wno-error=ambiguous-reversed-operator",
    "-Wno-error=deprecated-redundant-constexpr-static-def",
    "-Wno-error=header-hygiene",
    "-Wno-error=ignored-optimization-argument",
    "-Wno-error=implicit-fallthrough",
    "-Wno-error=unused-but-set-variable",
    "-Wno-error=unused-variable",
    # The following flags are introduced in LLVM upstream main
    "-Wno-unknown-warning-option",
    "-Wno-preferred-type-bitfield-enum-conversion",
    "-Wno-nontrivial-memcall",
    # The following flags are needed to build llvm opt on macos:
    "-Wno-error=deprecated-this-capture",
    "-Wno-error=deprecated-anon-enum-enum-conversion",
    # The following flags are needed to build with clang for Linux:
    "-Wno-shadow",
    "-Wno-unreachable-code-loop-increment",
    "-Wno-inconsistent-missing-destructor-override",
    "-Wno-unreachable-code",
    "-Wno-undef",
    "-Wno-ambiguous-reversed-operator",
    "-Wno-tautological-type-limit-compare",
    "-Wno-tautological-unsigned-enum-zero-compare",
    "-Wno-tautological-unsigned-zero-compare",
    "-Wno-nested-anon-types",
]

LLVM_DEFAULT_FLAGS = [
    # By default, LLVM doesn't build with exceptions and RTTI.
    "-fno-exceptions",
    "-fno-rtti",
]

LLVM_CXX_FLAGS = ["-std=c++17"]

def llvm_library(**kwargs):
    """Wrapper for cxx_library with LLVM defaults."""
    if "exported_headers" in kwargs:
        kwargs["header_namespace"] = kwargs.pop("header_namespace", "")

    kwargs = _set_team_specific_flags(**kwargs)

    kwargs["compiler_flags"] = LLVM_DEFAULT_FLAGS + kwargs.get("compiler_flags", []) + COMPILER_WARNING_FLAGS + read_list("llvm", "cflags", default = [])
    lang_flags = dict(kwargs.get("lang_compiler_flags", {}))
    lang_flags["cxx"] = LLVM_CXX_FLAGS + lang_flags.get("cxx", [])
    kwargs["lang_compiler_flags"] = lang_flags
    kwargs.setdefault("preferred_linkage", "static")
    kwargs.setdefault("visibility", ["PUBLIC"])

    native.cxx_library(**kwargs)

def llvm_binary(**kwargs):
    """Wrapper for cxx_binary with LLVM defaults."""
    kwargs = _set_team_specific_flags(**kwargs)

    kwargs["compiler_flags"] = LLVM_DEFAULT_FLAGS + kwargs.get("compiler_flags", []) + COMPILER_WARNING_FLAGS + read_list("llvm", "cflags", default = [])
    lang_flags = dict(kwargs.get("lang_compiler_flags", {}))
    lang_flags["cxx"] = LLVM_CXX_FLAGS + lang_flags.get("cxx", [])
    kwargs["lang_compiler_flags"] = lang_flags
    kwargs.setdefault("visibility", ["PUBLIC"])

    native.cxx_binary(**kwargs)

def llvm_test(**kwargs):
    """Wrapper for cxx_test with LLVM defaults."""
    kwargs["compiler_flags"] = LLVM_DEFAULT_FLAGS + kwargs.get("compiler_flags", []) + COMPILER_WARNING_FLAGS + read_list("llvm", "cflags", default = [])
    lang_flags = dict(kwargs.get("lang_compiler_flags", {}))
    lang_flags["cxx"] = LLVM_CXX_FLAGS + lang_flags.get("cxx", [])
    kwargs["lang_compiler_flags"] = lang_flags
    kwargs.setdefault("link_ordering", "topological")
    kwargs.setdefault("visibility", ["PUBLIC"])

    native.cxx_test(**kwargs)

def llvm_filegroup(**kwargs):
    srcs = kwargs.get("srcs", [])
    kwargs["srcs"] = glob(srcs)
    native.filegroup(**kwargs)

def llvm_alias(**kwargs):
    kwargs.setdefault("visibility", ["PUBLIC"])

    native.alias(**kwargs)

def _set_team_specific_flags(**kwargs):
    """
    A macro wrapper to set team specific flags.
    """
    GATOR_KEYWORDS = ["gator", "toolchain-dev"]

    def _check_keywords(keywords, package_name):
        for k in keywords:
            if k in package_name:
                return True
        return False

    if _check_keywords(GATOR_KEYWORDS, native.package_name()):
        kwargs["preprocessor_flags"] = kwargs.get("preprocessor_flags", []) + ["-UNDEBUG"]

    return kwargs
