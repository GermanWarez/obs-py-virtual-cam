#!/usr/bin/env python3
"""480p (854x480) test pattern for the obs-py-virtual-cam plugin.

Streams a scrolling BGRA gradient at ~30 fps with a horizontal alpha gradient
(left = transparent, right = opaque) so transparency is exercised too. Pick
this sample when the OBS canvas is 854x480.

Transparent regions composite through to whatever sits behind this source in
the OBS scene; OBS Virtual Camera itself flattens to opaque.

Run:
    python3 sample_480p.py
"""

import time

import numpy as np

from pyvirtualcam_obs import Camera

WIDTH = 854
HEIGHT = 480
FPS = 30

# Horizontal alpha ramp: 0 (transparent) on the left -> 255 (opaque) on the right.
ALPHA_ROW = np.linspace(0, 255, WIDTH, dtype=np.uint8)


def main():
    xs = np.arange(WIDTH, dtype=np.uint16)
    ys = np.arange(HEIGHT, dtype=np.uint16)[:, None]

    with Camera(WIDTH, HEIGHT, fps=FPS) as cam:
        counter = 0
        while True:
            phase = counter * 3
            frame = np.empty((HEIGHT, WIDTH, 4), dtype=np.uint8)
            frame[:, :, 0] = ((xs + phase) % 256).astype(np.uint8)        # blue
            frame[:, :, 1] = ((ys + phase) % 256).astype(np.uint8)        # green
            frame[:, :, 2] = ((xs + ys + phase) % 256).astype(np.uint8)   # red
            frame[:, :, 3] = ALPHA_ROW                                    # alpha ramp

            cam.send(frame)
            counter += 1
            time.sleep(1.0 / FPS)


if __name__ == "__main__":
    main()
