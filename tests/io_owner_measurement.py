"""Measure the installed Carbon IO workload and process thread activity."""

from __future__ import annotations

import ctypes
import datetime
import json
import os
import sys
from ctypes import wintypes

import io_smoke


MEASUREMENT_REPETITIONS = 8
THREAD_SNAPSHOT_FLAGS = 0x00000004
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
MAX_OBSERVED_THREADS = 4096
NO_MORE_FILES = 18


class ThreadEntry32(ctypes.Structure):
    """Win32 process-thread enumeration record."""

    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ThreadID", wintypes.DWORD),
        ("th32OwnerProcessID", wintypes.DWORD),
        ("tpBasePri", wintypes.LONG),
        ("tpDeltaPri", wintypes.LONG),
        ("dwFlags", wintypes.DWORD),
    ]


def utc_timestamp() -> str:
    """Return a UTC timestamp for the machine-readable evidence record."""
    return (
        datetime.datetime.now(datetime.timezone.utc)
        .isoformat(timespec="milliseconds")
        .replace("+00:00", "Z")
    )


def count_process_threads() -> int:
    """Count live threads belonging to this Windows process."""
    if os.name != "nt":
        raise RuntimeError("Carbon IO owner measurement requires Windows")

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateToolhelp32Snapshot.argtypes = [
        wintypes.DWORD,
        wintypes.DWORD,
    ]
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.GetCurrentProcessId.restype = wintypes.DWORD
    kernel32.Thread32First.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(ThreadEntry32),
    ]
    kernel32.Thread32First.restype = wintypes.BOOL
    kernel32.Thread32Next.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(ThreadEntry32),
    ]
    kernel32.Thread32Next.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    snapshot = kernel32.CreateToolhelp32Snapshot(
        THREAD_SNAPSHOT_FLAGS, 0
    )
    if snapshot == INVALID_HANDLE_VALUE:
        raise ctypes.WinError(ctypes.get_last_error())

    process_id = kernel32.GetCurrentProcessId()
    entry = ThreadEntry32()
    entry.dwSize = ctypes.sizeof(ThreadEntry32)
    count = 0
    try:
        if not kernel32.Thread32First(snapshot, ctypes.byref(entry)):
            error = ctypes.get_last_error()
            if error == NO_MORE_FILES:
                return 0
            raise ctypes.WinError(error)

        while True:
            if entry.th32OwnerProcessID == process_id:
                if count >= MAX_OBSERVED_THREADS:
                    raise RuntimeError("process thread bound was exceeded")
                count += 1
            if kernel32.Thread32Next(snapshot, ctypes.byref(entry)):
                continue
            error = ctypes.get_last_error()
            if error != NO_MORE_FILES:
                raise ctypes.WinError(error)
            return count
    finally:
        if not kernel32.CloseHandle(snapshot):
            raise ctypes.WinError(ctypes.get_last_error())


def print_sample(phase: str, observed: int, peak: int) -> None:
    """Print one bounded owner measurement record."""
    print(
        json.dumps(
            {
                "event": "carbon_owner_sample",
                "phase": phase,
                "owner": "carbon-io",
                "subsystem": "libuv-scheduler",
                "reservation": "unknown",
                "configured_threads": None,
                "configured_threads_known": False,
                "observed_threads": observed,
                "peak_observed_threads": peak,
                "thread_source": "win32_toolhelp",
                "timestamp_utc": utc_timestamp(),
            },
            separators=(",", ":"),
        )
    )


def run_measurement() -> None:
    """Exercise Carbon IO through its installed Python extensions."""
    baseline = count_process_threads()
    print_sample("carbon_io_before_import", baseline, baseline)

    io_smoke.configure_import_paths()
    io_smoke.configure_scheduler()
    io_smoke.patch_standard_modules()
    ready = count_process_threads()
    peak = max(baseline, ready)
    print_sample("carbon_io_extensions_ready", ready, peak)

    for repetition in range(1, MEASUREMENT_REPETITIONS + 1):
        io_smoke.run_tasklet_smoke()
        observed = count_process_threads()
        peak = max(peak, observed)
        print_sample(f"carbon_io_workload_{repetition}", observed, peak)

    print(
        json.dumps(
            {
                "event": "carbon_io_owner_summary",
                "status": "pass",
                "repetitions": MEASUREMENT_REPETITIONS,
                "baseline_threads": baseline,
                "ready_threads": ready,
                "peak_threads": peak,
                "workload": "carbon-io-loopback-scheduler",
                "thread_source": "win32_toolhelp",
            },
            separators=(",", ":"),
        )
    )


def main() -> int:
    """Run the Carbon IO owner measurement."""
    try:
        run_measurement()
    except Exception as error:
        print(f"carbon IO owner measurement failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
