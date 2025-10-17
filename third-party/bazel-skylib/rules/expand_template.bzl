load("@bazel_skylib//lib:shell.bzl", "shell")

def track_out(out, default):
    """
    Buck1/Buck2 compatibility helper to mirror META's bazel_shim.track_out behavior.
    Allows callers to pass labels such as :name and normalizes them to plain paths.
    """
    candidate = out if out else default
    if candidate and candidate.startswith(":"):
        return candidate[1:]
    return candidate

def expand_template(
        name,
        out = None,
        substitutions = {},
        template = None,
        labels = []):
    native.genrule(
        name = name,
        cmd = "sed {} {} > \"$OUT\"".format(
            " ".join(["-e {}".format(shell.quote("s|{}|{}|g".format(k, v.replace("\n", "\\\n")))) for k, v in substitutions.items()]),
            template if ":" not in template else "$(location {})".format(template),
        ),
        srcs = [template],
        out = track_out(out, name),
        labels = labels,
    )
