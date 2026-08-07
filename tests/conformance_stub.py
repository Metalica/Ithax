"""Loopback conformance fixture for the Stage 1 gate.

A test harness, not a server-side implementation or game implementation:
it implements no game protocol, no game logic, and no world state. It proves
a local process can start, bind the loopback
address, accept a TCP connection, and answer a probe.

Usage:
    conformance_stub.py            run the in-process self-test
    conformance_stub.py --serve    bind and answer probes until stopped
    conformance_stub.py --probe    send one probe to a running fixture
"""

import argparse
import json
import socket
import sys
import threading
import time


LOOPBACK_ADDRESS = "127.0.0.1"
DEFAULT_PORT = 26000
LISTEN_BACKLOG = 1
MAX_FRAME_SIZE = 1024
PROBE_PAYLOAD = b"probe"
PONG_PAYLOAD = b"pong"
SOCKET_TIMEOUT_SECONDS = 5.0
LENGTH_PREFIX_SIZE = 4
SERVE_POLL_SECONDS = 0.1


def encode_frame(payload: bytes) -> bytes:
    """Prefix a payload with its 4-byte big-endian length."""
    length = len(payload)
    if length > MAX_FRAME_SIZE:
        raise ValueError("frame payload exceeds the fixture bound")
    return length.to_bytes(LENGTH_PREFIX_SIZE, "big") + payload


def recv_exact(connection: socket.socket, count: int) -> bytes:
    """Read exactly count bytes, or raise on EOF or timeout."""
    chunks = bytearray()
    while len(chunks) < count:
        chunk = connection.recv(count - len(chunks))
        if not chunk:
            raise ConnectionError("peer closed the connection early")
        chunks.extend(chunk)
    return bytes(chunks)


def read_frame(connection: socket.socket) -> bytes:
    """Read one length-prefixed frame from a connection."""
    header = recv_exact(connection, LENGTH_PREFIX_SIZE)
    length = int.from_bytes(header, "big")
    if length > MAX_FRAME_SIZE:
        raise ConnectionError("frame length exceeds the fixture bound")
    return recv_exact(connection, length)


def handle_connection(connection: socket.socket) -> None:
    """Answer one probe frame with a pong frame."""
    connection.settimeout(SOCKET_TIMEOUT_SECONDS)
    payload = read_frame(connection)
    if payload != PROBE_PAYLOAD:
        raise ConnectionError("unexpected probe payload")
    connection.sendall(encode_frame(PONG_PAYLOAD))


def serve(listener: socket.socket) -> None:
    """Accept and answer probes until the listener is closed."""
    while True:
        try:
            connection, _address = listener.accept()
        except OSError:
            return
        try:
            handle_connection(connection)
        except (ConnectionError, OSError):
            print(json.dumps({"event": "probe_rejected"}), flush=True)
        finally:
            connection.close()


def start_fixture(port: int) -> tuple[socket.socket, int]:
    """Bind a loopback listener and return it with its actual port."""
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((LOOPBACK_ADDRESS, port))
        listener.listen(LISTEN_BACKLOG)
    except OSError:
        listener.close()
        raise
    actual_port = listener.getsockname()[1]
    return listener, actual_port


def verify_frame_size_bound() -> None:
    """Verify outbound frames enforce the fixture size limit."""
    oversized_payload = b"x" * (MAX_FRAME_SIZE + 1)
    rejected_oversized_payload = False
    try:
        encode_frame(oversized_payload)
    except ValueError:
        rejected_oversized_payload = True
    if not rejected_oversized_payload:
        raise RuntimeError("fixture accepted an oversized payload")


def verify_closed_peer_rejected() -> None:
    """Verify closed peers do not bypass connection handling."""
    server, client = socket.socketpair()
    client.close()
    rejected_closed_peer = False
    try:
        try:
            handle_connection(server)
        except ConnectionError:
            rejected_closed_peer = True
        if server.gettimeout() != SOCKET_TIMEOUT_SECONDS:
            raise RuntimeError("fixture did not set a peer timeout")
    finally:
        server.close()
    if not rejected_closed_peer:
        raise RuntimeError("fixture accepted a closed peer")


def run_self_test() -> None:
    """Start the fixture and verify one probe round-trip."""
    verify_frame_size_bound()
    verify_closed_peer_rejected()

    listener, actual_port = start_fixture(0)
    thread = threading.Thread(target=serve, args=(listener,), daemon=True)
    thread.start()
    try:
        with socket.create_connection(
            (LOOPBACK_ADDRESS, actual_port), SOCKET_TIMEOUT_SECONDS
        ) as client:
            client.settimeout(SOCKET_TIMEOUT_SECONDS)
            client.sendall(encode_frame(PROBE_PAYLOAD))
            response = read_frame(client)
        if response != PONG_PAYLOAD:
            raise RuntimeError("probe round-trip returned the wrong payload")
    finally:
        listener.close()
        thread.join(timeout=SOCKET_TIMEOUT_SECONDS)
    print('{"event":"conformance_stub_selftest","status":"pass"}')


def run_serve(port: int) -> None:
    """Serve probes on the loopback address until interrupted."""
    listener, actual_port = start_fixture(port)
    thread = threading.Thread(target=serve, args=(listener,), daemon=True)
    thread.start()
    print(
        json.dumps(
            {
                "event": "fixture_start",
                "address": LOOPBACK_ADDRESS,
                "port": actual_port,
            }
        ),
        flush=True,
    )
    try:
        while thread.is_alive():
            time.sleep(SERVE_POLL_SECONDS)
    except KeyboardInterrupt:
        listener.close()
    finally:
        listener.close()
        thread.join(timeout=SOCKET_TIMEOUT_SECONDS)
    print('{"event":"fixture_stop"}', flush=True)


def run_probe(port: int) -> int:
    """Probe a running fixture and report the response."""
    try:
        with socket.create_connection(
            (LOOPBACK_ADDRESS, port), SOCKET_TIMEOUT_SECONDS
        ) as client:
            client.settimeout(SOCKET_TIMEOUT_SECONDS)
            client.sendall(encode_frame(PROBE_PAYLOAD))
            response = read_frame(client)
        if response != PONG_PAYLOAD:
            raise RuntimeError("probe returned an unexpected payload")
    except (ConnectionError, OSError, RuntimeError) as error:
        print(
            json.dumps(
                {"event": "probe", "status": "fail", "error": str(error)}
            ),
            flush=True,
        )
        return 1
    print(
        json.dumps(
            {
                "event": "probe",
                "status": "pass",
                "response": response.decode("ascii"),
            }
        ),
        flush=True,
    )
    return 0


def parse_args() -> argparse.Namespace:
    """Parse the fixture command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Loopback conformance fixture for the Stage 1 gate"
    )
    parser.add_argument(
        "--port",
        type=int,
        default=DEFAULT_PORT,
        help="loopback port to bind or probe",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--serve", action="store_true", help="answer probes until stopped"
    )
    mode.add_argument(
        "--probe", action="store_true", help="send one probe to the fixture"
    )
    return parser.parse_args()


def main() -> int:
    """Dispatch to the selected fixture mode."""
    args = parse_args()
    if args.serve:
        run_serve(args.port)
        return 0
    if args.probe:
        return run_probe(args.port)
    run_self_test()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
