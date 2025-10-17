"""
Function used to re-export files
"""

load("@fbsource//tools/build_defs:fb_native_wrapper.bzl", "fb_native")

def exports_files(files, visibility = ["PUBLIC"], mode = "reference"):
    """ Takes a list of files, and exports each of them """
    for file in files:
        """FIXME: Temporary workaround for the invalid file paths in the upstreaming Bazel"""
        if glob([file]):
            fb_native.export_file(
                name = file,
                mode = mode,
                visibility = visibility,
            )
