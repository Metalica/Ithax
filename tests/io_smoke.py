"""Validate the installed Carbon IO Python extensions."""

import importlib
import os
import sys
from types import ModuleType


EXPECTED_PAYLOAD = b"carbon-io-smoke"
IMPORT_PATH_PRIORITY = 0
LISTEN_BACKLOG = 1
LOOPBACK_ADDRESS = "127.0.0.1"
SELECT_TIMEOUT_SECONDS = 0.0
SOCKET_TIMEOUT_SECONDS = 2.0


def configure_import_paths() -> None:
    """Add Carbon IO's extension and Python module directories."""
    prefix = os.environ["CARBON_IO_PREFIX"]
    sys.path.insert(IMPORT_PATH_PRIORITY, os.path.join(prefix, "bin"))
    sys.path.insert(
        IMPORT_PATH_PRIORITY, os.path.join(prefix, "bin", "python")
    )
    sys.path.insert(IMPORT_PATH_PRIORITY, os.path.join(prefix, "lib"))


def import_extension(module_name: str) -> ModuleType:
    """Import a Carbon extension for the active build flavor."""
    flavor = os.environ.get("BUILDFLAVOR", "")
    flavored_name = f"{module_name}_{flavor}" if flavor else module_name
    return importlib.import_module(flavored_name)


def configure_scheduler() -> None:
    """Load Scheduler before Carbon IO initializes its scheduler bridge."""
    sys.modules["_scheduler"] = import_extension("_scheduler")
    importlib.import_module("scheduler")


def patch_standard_modules() -> None:
    """Expose Carbon extensions under the standard Python module names."""
    socket_extension = import_extension("_carbonsocket")
    sys.modules["_carbonsocket"] = socket_extension
    sys.modules["_socket"] = socket_extension
    sys.modules["_ssl"] = import_extension("_carbonssl")
    sys.modules["select"] = import_extension("carbonselect")


def exercise_socket() -> None:
    """Exchange a payload through a local TCP connection."""
    import socket

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.settimeout(SOCKET_TIMEOUT_SECONDS)
        server.bind((LOOPBACK_ADDRESS, 0))
        server.listen(LISTEN_BACKLOG)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client:
            client.settimeout(SOCKET_TIMEOUT_SECONDS)
            client.connect(server.getsockname())
            accepted_peer, _ = server.accept()
            with accepted_peer as peer:
                peer.settimeout(SOCKET_TIMEOUT_SECONDS)
                client.sendall(EXPECTED_PAYLOAD)
                if peer.recv(len(EXPECTED_PAYLOAD)) != EXPECTED_PAYLOAD:
                    raise RuntimeError("Carbon IO TCP payload did not round-trip")


def exercise_ssl() -> None:
    """Create an SSL context through Carbon IO's SSL extension."""
    import ssl

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    if not context.check_hostname:
        raise RuntimeError("Carbon IO SSL context has unexpected defaults")


def exercise_select() -> None:
    """Verify Carbon IO's select extension returns standard empty sets."""
    import select
    import socket

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        result = select.select([probe], [], [], SELECT_TIMEOUT_SECONDS)
    if result != ([], [], []):
        raise RuntimeError("Carbon IO select returned an unexpected result")


def run_tasklet_smoke() -> None:
    """Run blocking checks through the Carbon Scheduler event pump."""
    import scheduler
    import socket

    errors: list[Exception] = []
    is_complete = False

    def run_checks() -> None:
        nonlocal is_complete
        try:
            exercise_socket()
            exercise_ssl()
            exercise_select()
        except Exception as error:
            errors.append(error)
        finally:
            is_complete = True

    scheduler.tasklet(run_checks)()
    while not is_complete:
        scheduler.run()
        socket.dispatch()

    if errors:
        raise RuntimeError("Carbon IO tasklet smoke test failed") from errors[0]


def run_smoke() -> None:
    """Patch standard modules and exercise Carbon IO functionality."""
    if "socket" in sys.modules:
        raise RuntimeError("socket must not be imported before Carbon IO setup")
    configure_import_paths()
    configure_scheduler()
    patch_standard_modules()
    run_tasklet_smoke()


def main() -> int:
    """Run the Carbon IO smoke test."""
    run_smoke()
    print('{"event":"io_smoke","status":"pass"}')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
