load("@bazel_skylib//lib:shell.bzl", "shell")
load("@fbsource//tools/build_defs:bazel_shim.bzl", "track_out")

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
        out = track_out(out, ":" + name),
        labels = labels,
    )
