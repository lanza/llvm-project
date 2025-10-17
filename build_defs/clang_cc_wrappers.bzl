load("@bazel_skylib//lib:paths.bzl", "paths")
load("@//build_defs:defs.bzl", "llvm_binary", "llvm_library")

def _normalize_copts(copts):
    if copts == None:
        return []
    if not isinstance(copts, list):
        return copts
    result = []
    for flag in copts:
        if "STACK_FRAME_UNLIMITED" in flag:
            continue
        result.append(flag)
    return result

_HEADER_EXTS = (".h", ".hh", ".hpp", ".hxx")
_TEXTUAL_EXTS = (".inc", ".def")

def _normalize_srcs(srcs):
    if srcs == None:
        return [], []
    if not isinstance(srcs, list):
        return srcs, []
    result = []
    headers = []
    for src in srcs:
        if isinstance(src, str):
            if src.endswith(_HEADER_EXTS) or src.endswith(_TEXTUAL_EXTS):
                headers.append(src)
                continue
        result.append(src)
    return result, headers

def _convert_defines(defines):
    return ["-D{}".format(define) for define in defines or []]

def _filter_headers(headers):
    if not headers:
        return []
    filtered = []
    for header in headers:
        if isinstance(header, str):
            matches = glob([header])
            if not matches:
                continue
        filtered.append(header)
    return filtered

def _build_headers_map(strip_prefix, headers):
    if not strip_prefix:
        return {}
    normalized = strip_prefix.rstrip("/")
    mapping = {}
    for header in headers:
        if header.startswith(normalized + "/"):
            mapping[header[len(normalized) + 1:]] = header
        else:
            mapping[paths.basename(header)] = header
    return mapping

def cc_library(
        name,
        srcs = None,
        hdrs = None,
        textual_hdrs = None,
        deps = None,
        exported_deps = None,
        includes = None,
        strip_include_prefix = None,
        include_prefix = None,
        copts = None,
        defines = None,
        linkopts = None,
        visibility = None,
        testonly = False,
        data = None,
        toolchains = None,
        alwayslink = False,
        features = None,
        tags = None,
        link_whole = False,
        **kwargs):
    normalized_srcs, header_srcs = _normalize_srcs(srcs)
    header_inputs = []
    header_inputs.extend(hdrs or [])
    header_inputs.extend(header_srcs)
    header_inputs.extend(textual_hdrs or [])
    headers = _filter_headers(header_inputs)
    exported_headers = _build_headers_map(strip_include_prefix, headers)
    library_kwargs = {}
    if exported_headers:
        library_kwargs["exported_headers"] = exported_headers
    elif headers:
        library_kwargs["raw_headers"] = headers
    if includes:
        library_kwargs["public_include_directories"] = includes
    compiler_flags = _normalize_copts(copts)
    if compiler_flags:
        library_kwargs["compiler_flags"] = compiler_flags
    preprocessor_flags = _convert_defines(defines)
    if preprocessor_flags:
        library_kwargs["exported_preprocessor_flags"] = preprocessor_flags
    if linkopts:
        library_kwargs["exported_linker_flags"] = linkopts
    if data:
        library_kwargs["resources"] = data
    if visibility:
        library_kwargs["visibility"] = visibility
    header_namespace = kwargs.pop("header_namespace", "")
    if include_prefix:
        header_namespace = include_prefix
    if exported_headers or headers or include_prefix:
        library_kwargs["header_namespace"] = header_namespace
    if alwayslink or link_whole:
        library_kwargs["link_whole"] = True
    # Drop Bazel-only attrs we don't support.
    for unwanted in ["features", "tags", "testonly", "toolchains"]:
        kwargs.pop(unwanted, None)
    library_kwargs.update(kwargs)
    dep_attr = deps or []
    export_attr = exported_deps or []
    dep_is_list = isinstance(dep_attr, list)
    export_is_list = isinstance(export_attr, list)
    dep_list = list(dep_attr) if dep_is_list else dep_attr
    if dep_is_list and export_is_list:
        exported_dep_list = list(export_attr)
        for dep in dep_list:
            if dep not in exported_dep_list:
                exported_dep_list.append(dep)
    else:
        exported_dep_list = export_attr
    llvm_library(
        name = name,
        srcs = normalized_srcs,
        deps = dep_list,
        exported_deps = exported_dep_list,
        **library_kwargs
    )

def cc_binary(
        name,
        srcs = None,
        deps = None,
        copts = None,
        defines = None,
        linkopts = None,
        visibility = None,
        testonly = False,
        stamp = None,
        data = None,
        toolchains = None,
        features = None,
        tags = None,
        **kwargs):
    binary_kwargs = {}
    compiler_flags = _normalize_copts(copts)
    if compiler_flags:
        binary_kwargs["compiler_flags"] = compiler_flags
    preprocessor_flags = _convert_defines(defines)
    if preprocessor_flags:
        binary_kwargs["preprocessor_flags"] = preprocessor_flags
    if linkopts:
        binary_kwargs["linker_flags"] = linkopts
    if visibility:
        binary_kwargs["visibility"] = visibility
    if data:
        binary_kwargs["resources"] = data
    for unwanted in ["features", "tags", "testonly", "toolchains"]:
        kwargs.pop(unwanted, None)
    binary_kwargs.update(kwargs)
    normalized_srcs, _ = _normalize_srcs(srcs)
    llvm_binary(
        name = name,
        srcs = normalized_srcs,
        deps = deps or [],
        **binary_kwargs
    )

def py_binary(**kwargs):
    native.python_binary(**kwargs)

def binary_alias(name, binary, visibility = None):
    alias_kwargs = {}
    if visibility:
        alias_kwargs["visibility"] = visibility
    native.alias(
        name = name,
        actual = binary,
        **alias_kwargs
    )

def cc_plugin_library(
        name,
        srcs,
        hdrs = None,
        strip_include_prefix = None,
        copts = None,
        defines = None,
        deps = None,
        visibility = None,
        include_prefix = None,
        features = None,
        tags = None,
        **kwargs):
    headers = list(hdrs or [])
    exported_headers = _build_headers_map(strip_include_prefix, headers)
    library_kwargs = {
        "preferred_linkage": "shared",
    }
    if exported_headers:
        library_kwargs["exported_headers"] = exported_headers
    elif headers:
        library_kwargs["raw_headers"] = headers
    compiler_flags = _normalize_copts(copts)
    if compiler_flags:
        library_kwargs["compiler_flags"] = compiler_flags
    preprocessor_flags = _convert_defines(defines)
    if preprocessor_flags:
        library_kwargs["exported_preprocessor_flags"] = preprocessor_flags
    if visibility:
        library_kwargs["visibility"] = visibility
    header_namespace = kwargs.pop("header_namespace", "")
    if include_prefix:
        header_namespace = include_prefix
    if exported_headers or headers or include_prefix:
        library_kwargs["header_namespace"] = header_namespace
    for unwanted in ["features", "tags"]:
        kwargs.pop(unwanted, None)
    library_kwargs.update(kwargs)
    llvm_library(
        name = name,
        srcs = list(srcs or []) + headers,
        deps = deps or [],
        **library_kwargs
    )

def objc_library(
        name,
        srcs = None,
        hdrs = None,
        deps = None,
        copts = None,
        defines = None,
        non_arc_srcs = None,
        **kwargs):
    combined_srcs = list(srcs or [])
    if non_arc_srcs:
        combined_srcs += list(non_arc_srcs)
    cc_library(
        name = name,
        srcs = combined_srcs,
        hdrs = hdrs,
        deps = deps,
        copts = copts,
        defines = defines,
        **kwargs
    )
