load("@fbsource//tools/build_defs:fb_native_wrapper.bzl", "fb_native")
load("@fbsource//tools/build_defs:bazel_shim.bzl", "track_out")

def write_file(name, out = None, content = []):
    fb_native.write_file(
        name = name,
        out = track_out(out, ":" + name),
        content = content,
    )
