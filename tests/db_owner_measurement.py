"""Measure Carbon DB provider work through the installed Python binding."""

from __future__ import annotations

import datetime
import json
import os
import sys
import time
from typing import Protocol

import blue
import db
import scheduler

from io_owner_measurement import count_process_threads


CONCURRENT_REQUESTS = 2
MAX_REQUEST_SECONDS = 15.0
MAX_SCHEMA_ENTRIES = 1_000_000
SAMPLE_INTERVAL_SECONDS = 0.01
SESSION_SETTINGS = {
    "maxSessions": 4,
    "minFreeSessions": 2,
    "maxFreeSessions": 4,
}


class DbSession(Protocol):
    """Minimal Carbon DB session surface used by this measurement."""

    def GetSchema(self, refresh: bool) -> object:
        """Refresh and return the provider schema."""
        ...

    def SetSessionSettings(self, settings: dict[str, int]) -> None:
        """Apply bounded session-pool settings."""
        ...


def utc_timestamp() -> str:
    """Return a UTC timestamp for a machine-readable evidence record."""
    return (
        datetime.datetime.now(datetime.timezone.utc)
        .isoformat(timespec="milliseconds")
        .replace("+00:00", "Z")
    )


def print_sample(
    phase: str,
    observed: int,
    peak: int,
    configured_sessions: int,
) -> None:
    """Print one bounded provider observation without connection data."""
    print(
        json.dumps(
            {
                "event": "carbon_db_owner_sample",
                "phase": phase,
                "owner": "carbon-db-provider",
                "subsystem": "carbon-db-ole-db-libuv",
                "measurement_class": "real-provider",
                "reservation": "unknown",
                "configured_sessions": configured_sessions,
                "observed_threads": observed,
                "peak_observed_threads": peak,
                "thread_source": "win32_toolhelp",
                "timestamp_utc": utc_timestamp(),
            },
            separators=(",", ":"),
        )
    )


def provider_request(
    session: DbSession,
    result: dict[int, int],
    errors: dict[int, str],
    request_id: int,
) -> None:
    """Refresh a provider schema from one isolated Carbon DB session."""
    try:
        schema = session.GetSchema(True)
        procedures = (
            schema[0]
            if isinstance(schema, tuple)
            and len(schema) > 0
            and isinstance(schema[0], dict)
            else {}
        )
        if len(procedures) > MAX_SCHEMA_ENTRIES:
            raise RuntimeError("schema result exceeded its bound")
        result[request_id] = len(procedures)
    except Exception:
        errors[request_id] = "provider schema request failed"


def run_measurement() -> int:
    """Run bounded concurrent Carbon DB provider requests."""
    connection_string = os.environ.get(
        "ITHAX_CARBON_DB_CONNECTION_STRING", ""
    )
    if not connection_string:
        print(
            json.dumps(
                {
                    "event": "carbon_db_owner_summary",
                    "status": "not_configured",
                    "gate_status": "open",
                    "measurement_class": "real-provider",
                    "provider": "OLE DB via Carbon DB",
                    "reason": (
                        "ITHAX_CARBON_DB_CONNECTION_STRING is unset"
                    ),
                },
                separators=(",", ":"),
            )
        )
        return 0

    baseline = count_process_threads()
    peak = baseline
    print_sample("provider_before", baseline, peak, 0)
    sessions = [
        db.NSession(connection_string) for _ in range(CONCURRENT_REQUESTS)
    ]
    for session in sessions:
        session.SetSessionSettings(SESSION_SETTINGS)
    ready = count_process_threads()
    peak = max(peak, ready)
    print_sample(
        "provider_sessions_ready", ready, peak, CONCURRENT_REQUESTS
    )

    results: dict[int, int] = {}
    errors: dict[int, str] = {}
    tasks = [
        scheduler.tasklet(provider_request)(
            session, results, errors, request_id
        )
        for request_id, session in enumerate(sessions)
    ]
    deadline = time.monotonic() + MAX_REQUEST_SECONDS
    last_sample = 0.0
    while any(task.alive for task in tasks):
        blue.os.Pump()
        now = time.monotonic()
        if now - last_sample >= SAMPLE_INTERVAL_SECONDS:
            observed = count_process_threads()
            peak = max(peak, observed)
            print_sample(
                "provider_requests_active",
                observed,
                peak,
                len(sessions),
            )
            last_sample = now
        if now >= deadline:
            print(
                json.dumps(
                    {
                        "event": "carbon_db_owner_summary",
                        "status": "fail",
                        "gate_status": "open",
                        "measurement_class": "real-provider",
                        "provider": "OLE DB via Carbon DB",
                        "reason": "provider request deadline exceeded",
                    },
                    separators=(",", ":"),
                )
            )
            return 1

    drained = count_process_threads()
    print_sample("provider_requests_drained", drained, peak, len(sessions))
    successful = len(results) == CONCURRENT_REQUESTS and not errors
    status = "pass" if successful else "fail"
    print(
        json.dumps(
            {
                "event": "carbon_db_owner_summary",
                "status": status,
                "gate_status": "pass" if successful else "open",
                "measurement_class": "real-provider",
                "provider": "OLE DB via Carbon DB",
                "concurrent_requests": CONCURRENT_REQUESTS,
                "schema_entries": [results[index] for index in sorted(results)],
                "baseline_threads": baseline,
                "ready_threads": ready,
                "peak_threads": peak,
                "drained_threads": drained,
                "shutdown_mode": "process-exit",
            },
            separators=(",", ":"),
        )
    )
    return 0 if successful else 1


def main() -> int:
    """Run the provider measurement inside the Carbon scheduler."""
    if os.name != "nt":
        print(
            "Carbon DB provider measurement requires Windows", file=sys.stderr
        )
        return 1

    state: dict[str, int] = {"exit_code": 1}

    def run() -> None:
        try:
            state["exit_code"] = run_measurement()
        except Exception:
            print(
                json.dumps(
                    {
                        "event": "carbon_db_owner_summary",
                        "status": "fail",
                        "gate_status": "open",
                        "measurement_class": "real-provider",
                        "provider": "OLE DB via Carbon DB",
                        "reason": "provider measurement raised an error",
                    },
                    separators=(",", ":"),
                )
            )
            state["exit_code"] = 1

    task = scheduler.tasklet(run)()
    while task.alive:
        blue.os.Pump()
    return state["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
