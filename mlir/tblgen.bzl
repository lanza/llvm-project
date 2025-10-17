# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Buck2 helpers for MLIR/LLVM tablegen invocations."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@//build_defs:defs.bzl", "llvm_library")

TdInfo = provider(fields = ["transitive_sources", "transitive_includes"])

def _get_dep_transitive_srcs(dep):
    info = dep.get(TdInfo)
    if info:
        return info.transitive_sources
    return []

def _get_dep_transitive_includes(dep):
    info = dep.get(TdInfo)
    if info:
        return info.transitive_includes
    return []

def _resolve_includes(ctx, includes):
    resolved_includes = []
    for include in includes:
        if include == "/":
            path = "."
        elif include.startswith("//"):
            path = include[2:]
        elif include.startswith("/"):
            path = include.lstrip("/")
        elif include == ".":
            path = "."
        else:
            path = include
        resolved_includes.append(path)
    return resolved_includes

def _artifact_dir(artifact):
    short_path = artifact.short_path
    if "/" in short_path:
        return short_path.rsplit("/", 1)[0]
    return ""

def _td_library_impl(ctx: AnalysisContext) -> list[Provider]:
    trans_srcs = list(ctx.attrs.srcs)
    for gen in ctx.attrs.generated_srcs:
        default = gen.get(DefaultInfo)
        if default == None:
            fail("generated_srcs entry {} must provide DefaultInfo".format(gen))
        outputs = default.default_outputs
        if len(outputs) != 1:
            fail("generated_srcs entry {} must produce exactly one output".format(gen))
        trans_srcs.append(outputs[0])
    for dep in ctx.attrs.deps:
        trans_srcs.extend(_get_dep_transitive_srcs(dep))
    resolved_includes = _resolve_includes(ctx, ctx.attrs.includes)
    for dep in ctx.attrs.deps:
        resolved_includes.extend(_get_dep_transitive_includes(dep))
    return [
        TdInfo(
            transitive_sources = trans_srcs,
            transitive_includes = resolved_includes,
        ),
        DefaultInfo(),
    ]

_td_library_rule = rule(
    impl = _td_library_impl,
    attrs = {
        "srcs": attrs.list(attrs.source(), default = []),
        "generated_srcs": attrs.list(attrs.dep(), default = []),
        "includes": attrs.list(attrs.string(), default = []),
        "deps": attrs.list(attrs.dep(), default = []),
    },
)

def td_library(**kwargs):
    kwargs.setdefault("visibility", ["PUBLIC"])
    _td_library_rule(**kwargs)

def _gentbl_rule_impl(ctx: AnalysisContext) -> list[Provider]:
    td_file = ctx.attrs.td_file

    trans_srcs = list(ctx.attrs.td_srcs) + [td_file]
    for dep in ctx.attrs.deps:
        trans_srcs.extend(_get_dep_transitive_srcs(dep))

    include_dirs = _resolve_includes(ctx, ctx.attrs.includes + ["."])
    td_dir = _artifact_dir(td_file)
    if td_dir:
        include_dirs.append(td_dir)
    for dep in ctx.attrs.deps:
        include_dirs.extend(_get_dep_transitive_includes(dep))

    include_tree_entries = {}
    for src in trans_srcs:
        short_path = src.short_path
        existing = include_tree_entries.get(short_path)
        if existing and existing != src:
            fail("Conflicting td source '{}' encountered from {} and {}".format(
                short_path,
                existing,
                src,
            ))
        include_tree_entries[short_path] = src
    include_tree = ctx.actions.symlinked_dir(
        "td_includes",
        include_tree_entries,
    )

    def _include_dir_artifact(path):
        normalized = path.rstrip("/")
        if normalized in ("", "."):
            return include_tree
        return include_tree.project(normalized)

    output = ctx.actions.declare_output(ctx.attrs.out)

    args = cmd_args(
        ctx.attrs.tblgen[RunInfo],
        hidden = trans_srcs,
    )
    for opt in ctx.attrs.opts:
        args.add(opt)
    args.add(td_file)
    for include in include_dirs:
        args.add("-I", _include_dir_artifact(include))
    args.add("-o", output.as_output())

    ctx.actions.run(
        args,
        category = "gentbl",
        identifier = ctx.attrs.name,
    )

    return [DefaultInfo(default_output = output)]

gentbl_rule = rule(
    impl = _gentbl_rule_impl,
    attrs = {
        "tblgen": attrs.exec_dep(providers = [RunInfo]),
        "td_file": attrs.source(),
        "td_srcs": attrs.list(attrs.source(), default = []),
        "deps": attrs.list(attrs.dep(), default = []),
        "out": attrs.string(),
        "opts": attrs.list(attrs.string(), default = []),
        "includes": attrs.list(attrs.string(), default = []),
    },
)

def _normalize_tbl_outs(tbl_outs):
    if isinstance(tbl_outs, dict):
        return [(opts, out) for out, opts in tbl_outs.items()]
    return tbl_outs

def _gentbl_rule_name(base_name, opts, index):
    first_opt = opts[0] if opts else "gen"
    suffix = "{}_{}".format(
        first_opt.replace("-", "_").replace("=", "_"),
        index,
    )
    return "{}__{}".format(base_name, suffix)

def _create_gentbl_rules(
        base_name,
        tblgen,
        td_file,
        tbl_outs,
        td_srcs,
        includes,
        deps):
    normalized_tbl_outs = _normalize_tbl_outs(tbl_outs)
    generated_targets = []

    for idx, (opts, out) in enumerate(normalized_tbl_outs):
        rule_name = _gentbl_rule_name(base_name, opts, idx)
        gentbl_rule(
            name = rule_name,
            tblgen = tblgen,
            td_file = td_file,
            td_srcs = td_srcs,
            deps = deps,
            includes = includes,
            opts = opts,
            out = out,
        )
        generated_targets.append(":" + rule_name)

    return generated_targets

def gentbl_cc_library(
        name,
        tblgen,
        td_file,
        tbl_outs,
        td_srcs = [],
        includes = [],
        deps = [],
        strip_include_prefix = None,
        test = False,
        copts = None,
        **kwargs):
    if test:
        fail("gentbl_cc_library(test = True) is not supported in the Buck2 build.")

    normalized_tbl_outs = _normalize_tbl_outs(tbl_outs)
    generated_targets = _create_gentbl_rules(
        name,
        tblgen,
        td_file,
        tbl_outs,
        td_srcs,
        includes,
        deps,
    )

    filegroup_name = "{}__gentbl_files".format(name)
    filegroup_kwargs = {}
    if "visibility" in kwargs:
        filegroup_kwargs["visibility"] = kwargs["visibility"]
    if "within_view" in kwargs:
        filegroup_kwargs["within_view"] = kwargs["within_view"]
    native.filegroup(
        name = filegroup_name,
        srcs = generated_targets,
        **filegroup_kwargs
    )

    hdrs = [":" + filegroup_name] if strip_include_prefix else []
    textual_hdrs = [] if strip_include_prefix else [":" + filegroup_name]

    extra_kwargs = dict(kwargs)
    compiler_flags = copts or []
    deps_attr = extra_kwargs.pop("deps", [])
    include_dirs = includes or []
    if include_dirs:
        extra_kwargs["public_include_directories"] = include_dirs
    exported_headers = {}
    for idx, (opts, out) in enumerate(normalized_tbl_outs):
        header_key = out
        if strip_include_prefix:
            prefix = strip_include_prefix.rstrip("/")
            if prefix and out.startswith(prefix + "/"):
                header_key = out[len(prefix) + 1:]
        elif out.startswith("include/"):
            header_key = out[len("include/"):]
        exported_headers[header_key] = ":{}".format(_gentbl_rule_name(name, opts, idx))
    llvm_library(
        name = name,
        exported_headers = exported_headers,
        header_namespace = "",
        compiler_flags = compiler_flags,
        srcs = [],
        deps = deps_attr,
        **extra_kwargs
    )

def gentbl_filegroup(
        name,
        tblgen,
        td_file,
        tbl_outs,
        td_srcs = [],
        includes = [],
        deps = [],
        **kwargs):
    generated_targets = _create_gentbl_rules(
        name,
        tblgen,
        td_file,
        tbl_outs,
        td_srcs,
        includes,
        deps,
    )

    filegroup_kwargs = {}
    if "visibility" in kwargs:
        filegroup_kwargs["visibility"] = kwargs["visibility"]
    if "within_view" in kwargs:
        filegroup_kwargs["within_view"] = kwargs["within_view"]
    native.filegroup(
        name = name,
        srcs = generated_targets,
        **filegroup_kwargs
    )
