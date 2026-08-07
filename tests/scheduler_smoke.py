"""Validate the installed Carbon Scheduler Python extension."""

import importlib
import os
import sys


EXPECTED_VALUE = "scheduler-smoke"
EXTENSION_NAME = "_scheduler_debug"


def configure_import_paths() -> None:
    """Add the vcpkg Python package and extension directories."""
    prefix = os.environ["SCHEDULER_PREFIX"]
    sys.path.insert(0, os.path.join(prefix, "bin"))
    sys.path.insert(0, os.path.join(prefix, "bin", "python"))


def run_smoke() -> None:
    """Import Scheduler and exercise a non-blocking queue channel."""
    extension = importlib.import_module(EXTENSION_NAME)
    sys.modules["_scheduler"] = extension

    scheduler = importlib.import_module("scheduler")
    channel = scheduler.QueueChannel()
    channel.send(EXPECTED_VALUE)

    if channel.receive() != EXPECTED_VALUE:
        raise RuntimeError("Scheduler QueueChannel returned the wrong value")


def main() -> int:
    """Run the Scheduler smoke test."""
    configure_import_paths()
    run_smoke()
    print('{"event":"scheduler_smoke","status":"pass"}')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
