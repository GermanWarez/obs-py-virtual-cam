#!/usr/bin/env python3
"""Latency-measurement sample for the obs-py-virtual-cam OBS plugin.

Fills the whole frame with a solid color that cycles through

    RED -> GREEN -> BLUE -> BLACK -> (repeat)

changing once per interval. At the exact moment each new color is sent, the
color name and a high-resolution timestamp are printed to the terminal.

How to measure latency
----------------------
Because the terminal line and the OBS frame flip at the same instant, you can
measure the end-to-end latency (Python -> socket -> OBS plugin -> OBS Virtual
Camera -> OBS preview) by screen-recording BOTH the terminal and the OBS
preview in one capture, then stepping through the recording frame by frame:
the gap between "the terminal line changes" and "the OBS preview changes color"
is the pipeline latency.

Tips for a clean measurement:
  * Record at a high frame rate (e.g. 60 fps) so each step is finely resolved.
  * Use a slow interval (the default 1.0 s) so the two events are easy to pair.
  * Put the terminal and the OBS preview side by side in the same recording.

Run (pick the resolution to match your OBS canvas; 720p is the default):
    python3 sample_latency.py
    python3 sample_latency.py --resolution 1080p --interval 0.5
"""

import argparse
import time

import numpy as np

from pyvirtualcam_obs import Camera

# Supported resolutions (match the OBS source's dropdown).
RESOLUTIONS = {
    "480p": (854, 480),
    "720p": (1280, 720),
    "1080p": (1920, 1080),
}

# Cycle order. Each entry is (name, (B, G, R)) — BGRA channel order, so the
# tuple is blue, green, red. Alpha is always 255 (opaque).
COLORS = [
    ("RED", (0, 0, 255)),
    ("GREEN", (0, 255, 0)),
    ("BLUE", (255, 0, 0)),
    ("BLACK", (0, 0, 0)),
]


def build_frame(width, height, bgr):
    """Return an (height, width, 4) uint8 BGRA frame of a single solid color."""
    frame = np.empty((height, width, 4), dtype=np.uint8)
    frame[:, :, 0] = bgr[0]  # blue
    frame[:, :, 1] = bgr[1]  # green
    frame[:, :, 2] = bgr[2]  # red
    frame[:, :, 3] = 255     # opaque alpha
    return frame


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--resolution", choices=sorted(RESOLUTIONS), default="720p",
        help="Frame resolution; must match the OBS canvas (default: 720p).")
    parser.add_argument(
        "--interval", type=float, default=1.0,
        help="Seconds between color changes (default: 1.0).")
    parser.add_argument(
        "--fps", type=int, default=30,
        help="Frames per second resent while a color is held (default: 30).")
    parser.add_argument(
        "--socket-path", default=None,
        help="Override the UDS path (defaults to the plugin's well-known path).")
    return parser.parse_args()


def main():
    args = parse_args()
    width, height = RESOLUTIONS[args.resolution]

    # Pre-build every solid-color frame once so sending is just a socket write
    # (no per-frame allocation to pollute the latency measurement).
    frames = [(name, build_frame(width, height, bgr)) for name, bgr in COLORS]

    frame_interval = 1.0 / args.fps if args.fps > 0 else 0.0

    print("obs-py-virtual-cam - latency test")
    print("resolution=%s (%dx%d)  interval=%.3fs  fps=%d"
          % (args.resolution, width, height, args.interval, args.fps))
    print("Cycling RED -> GREEN -> BLUE -> BLACK. Ctrl-C to stop.\n", flush=True)

    index = 0
    try:
        with Camera(width, height, fps=args.fps, socket_path=args.socket_path) as cam:
            while True:
                name, frame = frames[index % len(frames)]

                # Send the first frame of this color and timestamp it: this is
                # the instant the color changes on the wire. Print and send are
                # back to back so the terminal line and the OBS flip coincide.
                t = time.perf_counter()
                cam.send(frame)
                print("%12.4f  %s" % (t, name), flush=True)

                # Hold the color for `interval`, resending at `fps` so OBS keeps
                # receiving fresh frames (the extra sends do not change color).
                deadline = time.perf_counter() + args.interval
                while time.perf_counter() < deadline:
                    cam.send(frame)
                    if frame_interval:
                        time.sleep(frame_interval)

                index += 1
    except KeyboardInterrupt:
        print("\nstopped.")


if __name__ == "__main__":
    main()
