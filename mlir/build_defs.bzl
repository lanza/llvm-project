# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Buck helpers for MLIR rules."""

load("@//build_defs:clang_cc_wrappers.bzl", "cc_library")

def if_cuda_available(if_true, if_false = []):
    return if_false

def cc_headers_only(name, src, visibility = None):
    """Expose only headers from an existing cc_library target."""
    cc_library(
        name = name,
        exported_deps = [src],
        visibility = visibility,
    )

def mlir_c_api_cc_library(
        name,
        srcs = None,
        hdrs = None,
        deps = None,
        header_deps = None,
        capi_deps = None,
        **kwargs):
    """Mirror Bazel's mlir_c_api_cc_library helper."""
    capi_deps = list(capi_deps or [])
    header_deps = list(header_deps or [])
    deps = list(deps or [])
    srcs = list(srcs or [])
    hdrs = list(hdrs or [])

    capi_header_deps = ["{}Headers".format(dep) for dep in capi_deps]
    capi_object_deps = ["{}Objects".format(dep) for dep in capi_deps]

    cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        deps = deps + capi_deps + header_deps,
        **kwargs
    )

    cc_library(
        name = name + "Headers",
        hdrs = hdrs,
        deps = header_deps + capi_header_deps,
        **kwargs
    )

    cc_library(
        name = name + "Objects",
        srcs = srcs,
        hdrs = hdrs,
        deps = deps + capi_object_deps + capi_header_deps + header_deps,
        alwayslink = True,
        **kwargs
    )
