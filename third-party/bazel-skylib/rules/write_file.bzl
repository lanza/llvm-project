def _normalize_out(out, default):
    candidate = out if out else default
    if candidate and candidate.startswith(":"):
        return candidate[1:]
    return candidate

def _write_file_impl(ctx):
    out_name = ctx.attrs.out or ctx.label.name
    output = ctx.actions.declare_output(out_name)
    ctx.actions.write(
        output.as_output(),
        "\n".join(ctx.attrs.content),
    )
    return [DefaultInfo(default_output = output)]

_write_file_rule = rule(
    impl = _write_file_impl,
    attrs = {
        "content": attrs.list(attrs.string(), default = []),
        "out": attrs.option(attrs.string(), default = None),
    },
)

def write_file(name, out = None, content = []):
    _write_file_rule(
        name = name,
        out = _normalize_out(out, name),
        content = content,
    )
