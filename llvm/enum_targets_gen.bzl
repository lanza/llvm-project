# This file is licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""A rule to expand LLVM target enumerations.

Replaces in a text file a single variable of the style `@LLVM_ENUM_FOOS@` with a
list of macro invocations, one for each target on its own line:

```
LLVM_FOO(TARGET1)
LLVM_FOO(TARGET2)
// ...
```

Example:
load(":enum_targets_gen.bzl", "enum_targets_gen")

enum_targets_gen(
    name = "disassemblers_def_gen",
    src = "include/llvm/Config/Disassemblers.def.in",
    out = "include/llvm/Config/Disassemblers.def",
    macro_name = "DISASSEMBLER",
    targets = llvm_target_disassemblers,
)

This rule provides a slightly more semantic API than template_rule, but the main
reason it exists is to permit a list with selects to be passed for `targets` as
a select is not allowed to be passed to a rule within another data structure.
"""

load("@bazel_skylib//rules:expand_template.bzl", "expand_template")

def enum_targets_gen(name, out = None, src = None, targets = [], placeholder_name = None, macro_name = None):
    to_replace = placeholder_name
    if not to_replace:
        to_replace = "@LLVM_ENUM_{}S@".format(macro_name)
    replacement = "\n".join([
        "LLVM_{}({})\n".format(macro_name, t)
        for t in targets
    ])

    expand_template(
        name = name,
        template = src,
        out = out,
        substitutions = {to_replace: replacement},
    )
