"""Validate the installed Carbon Blue Python extension."""

import ctypes
import importlib
import os
import sys
from types import ModuleType


BLUE_EXTENSION_NAME = "blue_debug"
BLUE_LIBRARY_NAME = "blue_debug.pyd"
IMPORT_PATH_PRIORITY = 0


def configure_import_paths() -> str:
    """Add Carbon Blue's extension and Python module directories."""
    prefix = os.environ["CARBON_BLUE_PREFIX"]
    bin_directory = os.path.join(prefix, "bin")
    sys.path.insert(IMPORT_PATH_PRIORITY, bin_directory)
    sys.path.insert(
        IMPORT_PATH_PRIORITY,
        os.path.join(bin_directory, "python"),
    )
    return bin_directory


def load_blue_extension(bin_directory: str) -> ModuleType:
    """Start Blue before importing its debug Python extension."""
    library_path = os.path.join(bin_directory, BLUE_LIBRARY_NAME)
    library = ctypes.CDLL(library_path)
    library.BlueModuleStartup.argtypes = []
    library.BlueModuleStartup.restype = None
    library.BlueModuleStartup()
    return importlib.import_module(BLUE_EXTENSION_NAME)


def run_smoke() -> None:
    """Start Blue and exercise its installed Python support package."""
    bin_directory = configure_import_paths()
    blue_module = load_blue_extension(bin_directory)
    sys.modules["blue"] = blue_module

    bluepycore = importlib.import_module("bluepycore")
    branch = blue_module.GetBranch()
    changelist = blue_module.GetChangelist()
    if not branch or not changelist:
        raise RuntimeError("Carbon Blue returned empty build metadata")
    if not hasattr(bluepycore, "TaskletExt"):
        raise RuntimeError("Carbon Blue Python support is incomplete")


def main() -> int:
    """Run the Carbon Blue smoke test."""
    run_smoke()
    print('{"event":"blue_smoke","status":"pass"}')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
