"""Pure-stdlib UNIX-domain-socket client for the obs-py-virtual-cam OBS plugin.

A small, ``pyvirtualcam``-style client. Open a :class:`Camera` as a context
manager, then call :meth:`Camera.send` once per frame with a NumPy BGRA array.
Frames travel to the OBS plugin as a continuous stream of raw, fixed-size BGRA
frames: no header, no handshake.

NumPy is used only to construct/validate the frame buffer. The transport itself
is pure standard-library ``socket`` so the client has no non-numpy dependency.

Example
-------
    import numpy as np
    from pyvirtualcam_obs import Camera

    with Camera(width=1280, height=720, fps=30) as cam:
        frame = np.zeros((720, 1280, 4), dtype=np.uint8)
        frame[:, :, 3] = 255  # opaque alpha
        cam.send(frame)
"""

import os
import socket

__all__ = ["Camera", "default_socket_path"]


def default_socket_path():
    """Return the well-known socket path the OBS plugin listens on.

    Derived without configuration so the client and plugin agree by
    construction: ``$TMPDIR/py-virtual-cam/feed.sock``, falling back to
    ``/tmp`` when ``TMPDIR`` is unset.
    """
    base = os.environ.get("TMPDIR") or "/tmp"
    return os.path.join(base, "py-virtual-cam", "feed.sock")


class Camera:
    """A context-managed BGRA frame sender over a UNIX domain socket.

    Parameters
    ----------
    width, height : int
        Frame geometry. Every frame passed to :meth:`send` must match exactly;
        there is no negotiation or resize — a mismatch is rejected locally.
    fps : int, optional
        Advisory target framerate. The transport does not pace; OBS owns output
        timing. Stored on the instance so callers can drive their own loop.
    socket_path : str, optional
        Override the derived socket path. Defaults to
        :func:`default_socket_path`.
    """

    def __init__(self, width, height, fps=30, socket_path=None):
        self.width = int(width)
        self.height = int(height)
        self.fps = int(fps)
        self.socket_path = socket_path
        self._frame_bytes = self.width * self.height * 4
        self._sock = None

    def __enter__(self):
        path = self.socket_path if self.socket_path is not None else default_socket_path()
        self.socket_path = path
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(path)
        self._sock = sock
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()
        return False

    def close(self):
        """Close the socket if open. Safe to call more than once."""
        if self._sock is not None:
            self._sock.close()
            self._sock = None

    def send(self, frame):
        """Send one BGRA frame.

        ``frame`` must be a NumPy ``ndarray`` of shape ``(height, width, 4)``
        and dtype ``uint8``. The exact ``width * height * 4`` bytes are written
        to the socket with ``sendall`` (raw BGRA, headerless).
        """
        if self._sock is None:
            raise RuntimeError("Camera is not connected; use it as a context manager")

        # Import numpy lazily so merely importing this module (e.g. for the
        # ast-parse check) does not require numpy to be installed.
        import numpy as np

        if not isinstance(frame, np.ndarray):
            raise TypeError("frame must be a numpy ndarray")
        if frame.dtype != np.uint8:
            raise ValueError("frame dtype must be uint8, got %s" % frame.dtype)
        if frame.shape != (self.height, self.width, 4):
            raise ValueError(
                "frame shape must be (%d, %d, 4), got %s"
                % (self.height, self.width, tuple(frame.shape))
            )

        # tobytes() yields a C-contiguous copy, so a non-contiguous view (e.g. a
        # slice) still serialises to exactly width*height*4 bytes.
        data = frame.tobytes()
        if len(data) != self._frame_bytes:
            raise ValueError(
                "serialised frame is %d bytes, expected %d" % (len(data), self._frame_bytes)
            )
        self._sock.sendall(data)
