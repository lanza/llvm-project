# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Buck helpers for MLIR linalg generation."""

load("@//build_defs:clang_cc_wrappers.bzl", "cc_library")

def _linalggen_impl(ctx: AnalysisContext) -> list[Provider]:
    output = ctx.actions.declare_output(ctx.attrs.out)
    args = cmd_args(ctx.attrs.linalggen[RunInfo])
    for token in ctx.attrs.opts:
        if token == "$@":
            args.add(output.as_output())
            continue
        if "$@" in token:
            before, after = token.split("$@", 1)
            args.add(cmd_args(before, output.as_output(), after, format = "{}{}{}"))
        else:
            args.add(token)
    args.add(ctx.attrs.src)
    ctx.actions.run(
        args,
        category = "linalggen",
        identifier = ctx.attrs.name,
    )
    return [DefaultInfo(output)]

_linalggen_rule = rule(
    impl = _linalggen_impl,
    attrs = {
        "linalggen": attrs.exec_dep(),
        "opts": attrs.list(attrs.string(), default = []),
        "out": attrs.string(),
        "src": attrs.source(),
    },
)

def genlinalg(name, linalggen, src, linalg_outs):
    generated_entries = []
    for (opts, out) in linalg_outs:
        token_string = " ".join(opts)
        chars = [token_string[i:i + 1] for i in range(len(token_string))]
        safe = "".join([c if c.isalnum() else "_" for c in chars])
        rule_suffix = safe.strip("_") or "gen"
        target_name = "{}_{}".format(name, rule_suffix)
        _linalggen_rule(
            name = target_name,
            linalggen = linalggen,
            opts = opts,
            out = out,
            src = src,
        )
        generated_entries.append((out, ":" + target_name))

    exported_headers = {}
    for out, target_name in generated_entries:
        header_key = out
        if header_key.startswith("include/"):
            header_key = header_key[len("include/"):]
        exported_headers[header_key] = target_name
    cc_library(
        name = name,
        exported_headers = exported_headers,
        header_namespace = "",
        public_include_directories = ["include"],
    )
