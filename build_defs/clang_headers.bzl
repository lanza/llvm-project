def _builtin_headers_impl(ctx):
    output = ctx.actions.declare_output(ctx.attrs.out_dir, dir = True)
    prefix = ctx.attrs.strip_prefix.rstrip("/") + "/"
    entries = {}
    for src in ctx.attrs.srcs:
        short_path = src.short_path
        if not short_path.startswith(prefix):
            fail("Source {} does not start with prefix '{}'".format(short_path, prefix))
        rel = short_path[len(prefix):]
        entries[ctx.attrs.dest_dir.rstrip("/") + "/" + rel] = src
    ctx.actions.symlinked_dir(
        output.as_output(),
        entries,
    )
    return [DefaultInfo(default_output = output)]

clang_builtin_headers = rule(
    impl = _builtin_headers_impl,
    attrs = {
        "srcs": attrs.list(attrs.source(), default = []),
        "strip_prefix": attrs.string(default = ""),
        "dest_dir": attrs.string(default = "include"),
        "out_dir": attrs.string(default = "staging"),
    },
)
