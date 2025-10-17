# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Configuration for the llvm-driver tool."""

load("@bazel_skylib//rules:expand_template.bzl", "expand_template")
load("@fbsource//tools/build_defs:default_platform_defs.bzl", "get_host_target_platform")
load("@fbsource//tools/build_defs:fb_native_wrapper.bzl", "fb_native")
load("@fbsource//third-party/llvm-project/build_defs:defs.bzl", "llvm_binary")
load("@fbsource//third-party/llvm-project/build_defs:fat_binary.bzl", "fat_binary_alias")

# Mapping from every tool to the cc_library that implements the tool's entrypoint.
_TOOLS = {
    "clang-scan-deps": "{}/clang:clang-scan-deps-lib",
    "clang": "{}/clang:clang-driver",
    "dsymutil": "{}/llvm:dsymutil-lib",
    "lld": "{}/lld:lld-lib",
    "llvm-ar": "{}/llvm:llvm-ar-lib",
    "llvm-cgdata": "{}/llvm:llvm-cgdata-lib",
    "llvm-cxxfilt": "{}/llvm:llvm-cxxfilt-lib",
    "llvm-debuginfod-find": "{}/llvm:llvm-debuginfod-find-lib",
    "llvm-dwp": "{}/llvm:llvm-dwp-lib",
    "llvm-gsymutil": "{}/llvm:llvm-gsymutil-lib",
    "llvm-ifs": "{}/llvm:llvm-ifs-lib",
    "llvm-libtool-darwin": "{}/llvm:llvm-libtool-darwin-lib",
    "llvm-lipo": "{}/llvm:llvm-lipo-lib",
    "llvm-ml": "{}/llvm:llvm-ml-lib",
    "llvm-mt": "{}/llvm:llvm-mt-lib",
    "llvm-nm": "{}/llvm:llvm-nm-lib",
    "llvm-objcopy": "{}/llvm:llvm-objcopy-lib",
    "llvm-objdump": "{}/llvm:llvm-objdump-lib",
    "llvm-profdata": "{}/llvm:llvm-profdata-lib",
    "llvm-rc": "{}/llvm:llvm-rc-lib",
    "llvm-readobj": "{}/llvm:llvm-readobj-lib",
    "llvm-size": "{}/llvm:llvm-size-lib",
    "llvm-symbolizer": "{}/llvm:llvm-symbolizer-lib",
    "sancov": "{}/llvm:sancov-lib",
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

_DEFAULT_LLVM_ROOT = "//third-party/llvm-project/gator/20/llvm-project"

def generate_driver_selects(name):
    flag_base = name.replace("-", "_")
    for tool in _TOOLS.keys():
        flag_name = flag_base + "." + tool.replace("-", "_")
        fb_native.config_setting(
            name = "{}-include-{}".format(name, tool),
            values = {flag_name: "true"},
        )

def select_driver_tools(flag, llvm_root = _DEFAULT_LLVM_ROOT):
    flag_base = flag.replace("-", "_")
    tools = []
    for tool, target in _TOOLS.items():
        tools += select({
            "{}-include-{}".format(flag, tool): [target.format(llvm_root)],
            "DEFAULT": [],
        })
    return tools

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
        template = llvm_root + "/llvm:cmake/modules/llvm-driver-template.cpp.in",
    )

    llvm_binary(
        name = name + "-bin",
        executable_name = name,
        srcs = [":_gen_" + name],
        deps = deps + [llvm_root + "/llvm:Support"],
        **kwargs
    )
    for alias in aliases:
        exe_symlink(
            name = alias + "-bin",
            exe = ":{}-bin".format(name),
            symlink_name = alias,
            default_target_platform = get_host_target_platform(),
        )

    targets = [name] + aliases
    for target in targets:
        fat_binary_alias(
            name = target,
            bin = ":{}-bin".format(target),
            visibility = ["PUBLIC"],
        )
