#!/usr/bin/env python3
"""End-to-end UDS roundtrip test for the obs-py-virtual-cam client.

Binds a dummy ``AF_UNIX`` listener at a private temp socket path, runs the real
:class:`pyvirtualcam_obs.Camera` client against it (with ``socket_path``
override) for a few frames per resolution, and asserts that every received frame
chunk is exactly ``width * height * 4`` bytes. This validates the headerless,
fixed-size BGRA framing contract against the actual client, with no header or
handshake.

Run (no OBS, no plugin):
    python3 tests/test_uds_roundtrip.py

Requires NumPy (used by the client to build/validate the BGRA buffer).
"""

import os
import socket
import sys
import tempfile
import threading

import numpy as np

# Make the sibling python/ client importable regardless of CWD.
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", "python"))

from pyvirtualcam_obs import Camera  # noqa: E402

# (width, height) per shipped sample resolution.
RESOLUTIONS = [
    (854, 480),    # 480p
    (1280, 720),   # 720p
    (1920, 1080),  # 1080p
]

FRAMES_PER_RES = 3

failures = 0


def check(cond, msg):
    global failures
    if not cond:
        sys.stderr.write("FAIL: %s\n" % msg)
        failures += 1


def recv_exactly(conn, nbytes):
    """Read exactly ``nbytes`` from ``conn`` (loop over partial reads)."""
    buf = bytearray()
    while len(buf) < nbytes:
        chunk = conn.recv(nbytes - len(buf))
        if not chunk:
            break  # EOF before the full frame arrived
        buf.extend(chunk)
    return bytes(buf)


def run_resolution(width, height):
    """Stream FRAMES_PER_RES frames through a dummy listener; check byte counts."""
    expected = width * height * 4

    tmpdir = tempfile.mkdtemp(prefix="uds_roundtrip_")
    sock_path = os.path.join(tmpdir, "feed.sock")

    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(sock_path)
    listener.listen(1)

    received = []
    receiver_error = []

    def receiver():
        try:
            conn, _ = listener.accept()
            with conn:
                for _ in range(FRAMES_PER_RES):
                    frame = recv_exactly(conn, expected)
                    received.append(len(frame))
        except Exception as exc:  # pragma: no cover - surfaced via assertion
            receiver_error.append(repr(exc))

    thread = threading.Thread(target=receiver)
    thread.start()

    try:
        with Camera(width, height, fps=30, socket_path=sock_path) as cam:
            check(cam.socket_path == sock_path, "%dx%d: socket_path override honoured" % (width, height))
            for i in range(FRAMES_PER_RES):
                frame = np.zeros((height, width, 4), dtype=np.uint8)
                frame[:, :, 3] = 255  # opaque alpha
                frame[:, :, 0] = i    # vary a channel so frames are distinct
                cam.send(frame)
    finally:
        thread.join(timeout=10)
        listener.close()
        try:
            os.remove(sock_path)
        except OSError:
            pass
        try:
            os.rmdir(tmpdir)
        except OSError:
            pass

    check(not receiver_error, "%dx%d: receiver raised %s" % (width, height, receiver_error))
    check(
        len(received) == FRAMES_PER_RES,
        "%dx%d: received %d frames, expected %d" % (width, height, len(received), FRAMES_PER_RES),
    )
    for n in received:
        check(
            n == expected,
            "%dx%d: frame is %d bytes, expected %d (width*height*4)" % (width, height, n, expected),
        )


def main():
    for width, height in RESOLUTIONS:
        run_resolution(width, height)

    if failures == 0:
        print("ok: all uds roundtrip assertions passed")
        return 0
    sys.stderr.write("%d assertion(s) failed\n" % failures)
    return 1


if __name__ == "__main__":
    sys.exit(main())
