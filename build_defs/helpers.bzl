def _split_package_path() -> (str, str):
    """
    Split a package path into a tuple where the first component is everything up to and including
    the final llvm-project directory, and the second component is everything after that (excluding
    the leading slash).
    """
    prefix, llvm_project_dir, suffix = native.package_name().rpartition("/llvm-project")
    return prefix + llvm_project_dir, suffix[1:]  # remove leading slash from suffix

def llvm_relative_package_path() -> str:
    """
    Return the current package path relative to its llvm-project directory.
    """
    return _split_package_path()[1]

def llvm_target(target: str) -> str:
    """
    Convert a target path relative to the llvm-project directory into an absolute target path.
    """
    base_path = _split_package_path()[0]
    return "{}//{}/{}".format(native.get_cell_name(), base_path, target)
