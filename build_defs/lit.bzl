load("@prelude//:paths.bzl", "paths")
load(":helpers.bzl", "llvm_relative_package_path", "llvm_target")

def lit_suite(
        name,
        srcs = None,
        site_config_template = "lit.site.cfg.py.in",
        site_config = None,
        test_dir = None,
        tools = None,
        **kwargs):
    """
    A macro wrapper around the _lit_suite rule that enables default values for source arguments.
    """
    if not srcs:
        srcs = native.glob(["**/*"])

    if not site_config:
        site_config = site_config_template.removesuffix(".in")
        if len(site_config) == len(site_config_template):
            fail("site_config_template doesn't end with '.in'`; site_config must be passed")

    if not test_dir:
        # This is conventionally relative to the LLVM subproject directory (e.g. llvm or clang).
        test_dir = llvm_relative_package_path().partition("/")[2]

    # Lit always requires these.
    tools = tools or []
    tools += [
        llvm_target("llvm:FileCheck"),
        llvm_target("llvm:count"),
        llvm_target("llvm:not"),
    ]

    _lit_suite(
        name = name,
        srcs = srcs,
        site_config_template = site_config_template,
        site_config = site_config,
        test_dir = test_dir,
        tools = tools,
        **kwargs
    )

def lit_tests(
        lit_suite,
        suffixes,
        excludes = ["Inputs"],
        test_suite = "",
        subdir = ""):
    """
    Create targets for running lit tests.
    - lit_suite: The lit_suite target for the tests.
    - suffixes: Corresponds to config.suffixes in lit.cfg or lit.local.cfg. It should include the
                leading "." for extensions (e.g. [".test"]).
    - excludes: Corresponds to config.excludes. Defaults to ["Inputs"], which is usually enough.
    - test_suite: Create a test_suite target to run all associated tests.
    - subdir: A particular subdirectory to look for tests in. A test_suite target is automatically
              created with the name of the subdirectory to run all associated tests. The test_suite
              target name can be overridden by the test_suite argument.
    """
    lit = llvm_target("llvm:lit")
    tests = native.glob(
        [paths.join(subdir, "**", "*{}".format(suffix)) for suffix in suffixes],
        exclude = [paths.join("**", exclude, "**") for exclude in excludes] + [paths.join("**", exclude) for exclude in excludes],
    )
    for test in tests:
        test_path = paths.join("$(location {})".format(lit_suite), test)
        sh_test(
            name = test,
            test = lit,
            args = ["-v", "--skip-test-time-recording", test_path],
            # From fbcode/buck2/platform/remote_test_execution_toolchain_utils.bzl.
            # We'll need similar logic as there to handle other platforms if needed.
            remote_execution = {
                "capabilities": {
                    "platform": "linux-remote-execution",
                },
                "local_enabled": True,
                "local_listing_enabled": True,
                "use_case": "re_tests",
            },
        )

    test_suite_name = test_suite or subdir
    if test_suite_name:
        test_suite(
            name = test_suite_name,
            tests = [":{}".format(test) for test in tests],
        )

def lit_runner(
        name,
        lit_suite,
        subdir = "") -> None:
    """
    Creates a lit runner wrapper to use with `buck run`, to avoid the lit startup overhead for every
    individual test.
    """
    command_alias(
        name = name,
        exe = llvm_target("llvm:lit"),
        args = ["-sv", paths.join("$(location {})".format(lit_suite), subdir)],
    )

def _perform_substitutions_impl(
        actions: AnalysisActions,
        template_file: ArtifactValue,
        substitutions_file: ArtifactValue,
        output_file: OutputArtifact) -> list[Provider]:
    template = template_file.read_string()
    substitutions = substitutions_file.read_json()

    user_substitutions = {}
    for placeholder, replacement in substitutions["user_substitutions"].items():
        # unpack the replacement if it's a list, which is the case for macro substitutions
        if isinstance(replacement, list):
            if len(replacement) > 1:
                fail("Placeholder '{}' has multiple replacements: {}. Expected exactly one replacement.".format(placeholder, replacement))
            replacement = replacement[0]
        user_substitutions[placeholder] = replacement

    builtin_substitutions = substitutions["builtin_substitutions"]
    for placeholder, replacement in user_substitutions.items():
        # apply builtin substitutions, e.g. @@BINARY_DIR@@
        replacement = builtin_substitutions.get(replacement, replacement)
        template = template.replace(placeholder, replacement)
    actions.write(output_file, template)
    return []

_perform_substitutions = dynamic_actions(
    impl = _perform_substitutions_impl,
    attrs = {
        "output_file": dynattrs.output(),
        "substitutions_file": dynattrs.artifact_value(),
        "template_file": dynattrs.artifact_value(),
    },
)

def _lit_suite_impl(ctx: AnalysisContext) -> list[Provider]:
    copy_or_symlink = ctx.actions.copied_dir if ctx.attrs.copy_srcs else ctx.actions.symlinked_dir
    source_dir = copy_or_symlink(
        "source",
        {paths.join(ctx.attrs.test_dir, src.short_path): src for src in ctx.attrs.srcs},
    )
    binary_dir = ctx.actions.declare_output("binary", dir = True)
    tools_dir = ctx.actions.symlinked_dir(
        "tools",
        {
            output.basename: output
            for tool in ctx.attrs.tools
            for output in tool[DefaultInfo].default_outputs
        },
    )

    substitutions = {
        "builtin_substitutions": {
            "@@BINARY_DIR@@": binary_dir,
            "@@SOURCE_DIR@@": source_dir,
            "@@TOOLS_DIR@@": tools_dir,
        },
        "user_substitutions": ctx.attrs.substitutions,
    }
    substitutions_file = ctx.actions.write_json("tmp/substitutions.json", substitutions)

    # We need a dynamic action to access artifact paths and the site config template contents.
    site_config = ctx.actions.declare_output("tmp", ctx.attrs.site_config)
    ctx.actions.dynamic_output_new(_perform_substitutions(
        template_file = ctx.attrs.site_config_template,
        substitutions_file = substitutions_file,
        output_file = site_config.as_output(),
    ))
    ctx.actions.copied_dir(
        binary_dir.as_output(),
        {paths.join(ctx.attrs.test_dir, ctx.attrs.site_config): site_config},
    )

    # Every test that uses this suite will depend on all these outputs. It's a bit inefficient to
    # make each test depend on all the sources, since a single test usually only needs a few of
    # them, but RE worker download times dwarf actual test execution time even if each test has a
    # minimal set of inputs, so just do the simple thing. See https://fburl.com/workplace/5y1c79c0
    # for a detailed discussion. One simple optimization if we want it in the future is to create
    # sub_targets for each top-level test subdirectory, since those are usually independent.
    return [DefaultInfo(
        default_output = binary_dir.project(ctx.attrs.test_dir),
        other_outputs = [
            source_dir,
            tools_dir,
        ] + [tool[RunInfo] for tool in ctx.attrs.tools],
    )]

_lit_suite = rule(
    doc = "Create a lit test suite",
    # @unsorted-dict-items
    attrs = {
        "srcs": attrs.list(attrs.source(), doc = """
            The test suite sources
        """),
        "site_config_template": attrs.source(doc = """
            The site config template for the suite (usually "lit.site.cfg.py.in")
        """),
        "site_config": attrs.string(doc = """
            The generated site config name (usually "lit.site.cfg.py")
        """),
        "substitutions": attrs.dict(key = attrs.string(), value = attrs.arg(), doc = """
            The substitutions to apply to the site config template. Some special placeholders are:
            - @@SOURCE_DIR@@ points to the suite source directory
            - @@BINARY_DIR@@ points to the suite binary directory, which is where the generated site
              config lives (similar to CMake's notion of a binary directory)
            - @@TOOLS_DIR@@ points to the tools directory
        """),
        "test_dir": attrs.string(doc = """
            The directory the sources (relative to @@SOURCE_DIR@@) and generated site config
            (relative to @@BINARY_DIR@@) will be placed under (usually "test")
        """),
        "tools": attrs.list(attrs.dep(providers = [RunInfo]), doc = """
            The tool targets required by the suite
        """),
        "copy_srcs": attrs.bool(default = False, doc = """
            Copy the test suite sources instead of symlinking them
        """),
    },
    impl = _lit_suite_impl,
)
