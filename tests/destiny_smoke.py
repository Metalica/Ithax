"""Validate the installed Carbon Destiny Python extension."""

import ctypes
import importlib
import os
import sys
from types import ModuleType


BLUE_EXTENSION_NAME = "blue_debug"
BLUE_LIBRARY_NAME = "blue_debug.pyd"
DESTINY_EXTENSION_NAME = "_destiny_debug"
IMPORT_PATH_PRIORITY = 0


def configure_import_paths() -> tuple[str, str]:
    """Add Destiny's Python and native module directories."""
    prefix = os.environ["DESTINY_PREFIX"]
    bin_directory = os.path.join(prefix, "bin")
    lib_directory = os.path.join(prefix, "lib")
    sys.path.insert(IMPORT_PATH_PRIORITY, bin_directory)
    sys.path.insert(IMPORT_PATH_PRIORITY, lib_directory)
    sys.path.insert(
        IMPORT_PATH_PRIORITY,
        os.path.join(bin_directory, "python"),
    )
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(bin_directory)
        os.add_dll_directory(lib_directory)
    return bin_directory, lib_directory


def start_blue(bin_directory: str) -> ctypes.CDLL:
    """Initialize Blue before importing the Destiny extension."""
    library_path = os.path.join(bin_directory, BLUE_LIBRARY_NAME)
    library = ctypes.CDLL(library_path)
    library.BlueModuleStartup.argtypes = []
    library.BlueModuleStartup.restype = None
    library.BlueModuleStartup()
    return library


def load_destiny() -> ModuleType:
    """Import the initialized Destiny extension."""
    blue_module = importlib.import_module(BLUE_EXTENSION_NAME)
    sys.modules["blue"] = blue_module
    return importlib.import_module(DESTINY_EXTENSION_NAME)


def run_smoke() -> None:
    """Start Blue and load Destiny through the supported Python path."""
    bin_directory, _lib_directory = configure_import_paths()
    _blue_library = start_blue(bin_directory)
    destiny_module = load_destiny()
    if destiny_module.__name__ != DESTINY_EXTENSION_NAME:
        raise RuntimeError("Carbon Destiny imported under an unexpected name")


def main() -> int:
    """Run the Carbon Destiny smoke test."""
    run_smoke()
    print('{"event":"destiny_smoke","status":"pass"}')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
