# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Configuration for the llvm-driver tool."""

load("@bazel_skylib//rules:expand_template.bzl", "expand_template")
load("@//build_defs:defs.bzl", "llvm_binary")
load("@//build_defs:fat_binary.bzl", "fat_binary_alias")
load("@//exports_files.bzl", "exported_file_label")
load("@prelude//utils:buckconfig.bzl", "read_list")

# Mapping from every tool to the cc_library that implements the tool's entrypoint.
_TOOLS = {
    "clang-scan-deps": "clang:clang-scan-deps-lib",
    "clang": "clang:clang-driver",
    "dsymutil": "llvm:dsymutil-lib",
    "lld": "lld:lld-lib",
    "llvm-ar": "llvm:llvm-ar-lib",
    "llvm-cgdata": "llvm:llvm-cgdata-lib",
    "llvm-cxxfilt": "llvm:llvm-cxxfilt-lib",
    "llvm-debuginfod-find": "llvm:llvm-debuginfod-find-lib",
    "llvm-dwp": "llvm:llvm-dwp-lib",
    "llvm-gsymutil": "llvm:llvm-gsymutil-lib",
    "llvm-ifs": "llvm:llvm-ifs-lib",
    "llvm-libtool-darwin": "llvm:llvm-libtool-darwin-lib",
    "llvm-lipo": "llvm:llvm-lipo-lib",
    "llvm-ml": "llvm:llvm-ml-lib",
    "llvm-mt": "llvm:llvm-mt-lib",
    "llvm-nm": "llvm:llvm-nm-lib",
    "llvm-objcopy": "llvm:llvm-objcopy-lib",
    "llvm-objdump": "llvm:llvm-objdump-lib",
    "llvm-profdata": "llvm:llvm-profdata-lib",
    "llvm-rc": "llvm:llvm-rc-lib",
    "llvm-readobj": "llvm:llvm-readobj-lib",
    "llvm-size": "llvm:llvm-size-lib",
    "llvm-symbolizer": "llvm:llvm-symbolizer-lib",
    "sancov": "llvm:sancov-lib",
}

# Tools automatically get their own name as an alias, but there may be additional
# aliases for a given tool.
_EXTRA_ALIASES = {
    "clang": ["clang++", "clang-cl", "clang-cpp"],
    "lld": ["ld", "lld-link", "ld.lld", "ld64.lld", "wasm-ld"],
    "llvm-ar": ["ranlib", "lib", "dlltool"],
    "llvm-cxxfilt": ["c++filt"],
    "llvm-objcopy": ["bitcode-strip", "install-name-tool", "strip"],
    "llvm-objdump": ["otool"],
    "llvm-rc": ["windres"],
    "llvm-readobj": ["readelf"],
    "llvm-symbolizer": ["addr2line"],
}

_DEFAULT_LLVM_ROOT = ""

_DRIVER_CONFIG_SECTION = "llvm"

def _normalize_driver_flag(flag):
    normalized = flag.lstrip(":")
    return normalized.replace("-", "_")

def _configured_driver_tool_names(flag):
    config_key = _normalize_driver_flag(flag) or "driver_tools"
    configured = read_list(
        _DRIVER_CONFIG_SECTION,
        config_key,
        default = list(_TOOLS.keys()),
    )
    invalid = [tool for tool in configured if tool not in _TOOLS]
    if invalid:
        fail("Unknown driver tool(s) {}. Expected one of: {}".format(
            invalid,
            sorted(_TOOLS.keys()),
        ))
    return configured

def _resolve_label(target, llvm_root):
    package, name = target.split(":", 1)
    package = package.strip("/")
    root = llvm_root or ""
    cell = ""
    path = ""
    if root.startswith("@"):
        if "//" not in root:
            fail("Invalid llvm_root '{}'. Expected '@cell//path' format.".format(root))
        cell, remainder = root.split("//", 1)
        path = remainder.strip("/")
    elif root.startswith("//"):
        path = root[2:].strip("/")
    else:
        path = root.strip("/")

    pkg_components = [component for component in [path, package] if component]
    pkg_path = "/".join(pkg_components)
    prefix = "{}//".format(cell) if cell else "//"
    label = "{}{}:{}".format(prefix, pkg_path, name)
    if "/" in name:
        return exported_file_label(label)
    return label

def select_driver_tools(flag, llvm_root = _DEFAULT_LLVM_ROOT):
    enabled = _configured_driver_tool_names(flag)
    return [
        _resolve_label(_TOOLS[tool], llvm_root)
        for tool in enabled
    ]

def _generate_driver_tools_def_impl(ctx):
    # Depending on how the LLVM build files are included,
    # it may or may not have the @llvm-project repo prefix.
    # Compare just on the name. We could also include the package,
    # but the name itself is unique in practice.
    label_to_name = {v.split(":")[-1]: k for k, v in _TOOLS.items()}

    # Reverse sort by the *main* tool name, but keep aliases together.
    # This is consistent with how tools/llvm-driver/CMakeLists.txt does it,
    # and this makes sure that more specific tools are checked first.
    # For example, "clang-scan-deps" should not match "clang".
    tools = [label_to_name[tool.name] for tool in ctx.attrs.driver_tools]
    tool_alias_pairs = []
    for tool_name in reversed(tools):
        tool_alias_pairs.append((tool_name, tool_name))
        for extra_alias in _EXTRA_ALIASES.get(tool_name, []):
            tool_alias_pairs.append((tool_name, extra_alias))

    lines = [
        'LLVM_DRIVER_TOOL("{alias}", {tool})'.format(
            tool = tool_name.replace("-", "_"),
            alias = alias.removeprefix("llvm-"),
        )
        for (tool_name, alias) in tool_alias_pairs
    ]
    lines.append("#undef LLVM_DRIVER_TOOL")

    output = ctx.actions.declare_output(ctx.attrs.out)
    ctx.actions.write(
        output.as_output(),
        "\n".join(lines),
    )

    return [DefaultInfo(default_output = output)]

generate_driver_tools_def = rule(
    impl = _generate_driver_tools_def_impl,
    doc = """Generate a list of LLVM_DRIVER_TOOL macros.
See tools/llvm-driver/CMakeLists.txt for the reference implementation.""",
    attrs = {
        "driver_tools": attrs.list(
            attrs.label(),
            doc = "List of tools to include in the generated header. Use select_driver_tools() to provide this.",
            default = [],
        ),
        "out": attrs.string(
            doc = "Name of the generated .def output file.",
        ),
    },
)

def _exe_symlink_impl(ctx: AnalysisContext) -> list[Provider]:
    symlink_name = ctx.attrs.symlink_name or ctx.attrs.name
    symlink = ctx.actions.symlink_file(symlink_name, ctx.attrs.exe[DefaultInfo].default_outputs[0])
    return [
        DefaultInfo(
            default_output = symlink,
            other_outputs = ctx.attrs.exe[DefaultInfo].other_outputs,
        ),
        RunInfo(
            args = cmd_args(
                symlink,
                hidden = ctx.attrs.exe[RunInfo],
            ),
        ),
    ]

exe_symlink = rule(
    impl = _exe_symlink_impl,
    attrs = {
        "exe": attrs.dep(providers = [RunInfo]),
        "symlink_name": attrs.option(attrs.string(), default = None),
    },
)

def llvm_driver_cc_binary(
        name,
        aliases = [],
        deps = [],
        *,
        llvm_root = _DEFAULT_LLVM_ROOT,
        **kwargs):
    """cc_binary wrapper for binaries using the llvm-driver template."""
    expand_template(
        name = "_gen_" + name,
        out = name + "-driver.cpp",
        substitutions = {"@TOOL_NAME@": name.replace("-", "_")},
        template = _resolve_label("llvm:cmake/modules/llvm-driver-template.cpp.in", llvm_root),
    )

    llvm_binary(
        name = name + "-bin",
        executable_name = name,
        srcs = [":_gen_" + name],
        deps = deps + [_resolve_label("llvm:Support", llvm_root)],
        **kwargs
    )
    for alias in aliases:
        exe_symlink(
            name = alias + "-bin",
            exe = ":{}-bin".format(name),
            symlink_name = alias,
        )

    targets = [name] + aliases
    for target in targets:
        fat_binary_alias(
            name = target,
            bin = ":{}-bin".format(target),
            visibility = ["PUBLIC"],
        )
