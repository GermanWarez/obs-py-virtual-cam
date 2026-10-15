# obs-py-virtual-cam — Python client

A pure-stdlib UNIX-domain-socket client that streams raw BGRA frames to the
**obs-py-virtual-cam** OBS source plugin. OBS then presents the frames through
its built-in OBS Virtual Camera.

## Install

The frame samples use [NumPy](https://numpy.org/) to build the BGRA buffers:

```sh
pip install numpy
```

The client itself is a single pure-Python package, `pyvirtualcam_obs/`, with no
build step and no stdlib-external dependency. To use it, Python just has to find
it. The simplest way: **copy the `pyvirtualcam_obs/` folder next to your own
script**, so they sit side by side:

```text
my_project/
├── my_script.py
└── pyvirtualcam_obs/      ← copied from this repo's python/ directory
```

Then `import pyvirtualcam_obs` works from `my_script.py`.

Running the bundled samples needs no copying — they already live beside the
package in this `python/` directory, so run them from here:

```sh
cd python
python3 samples/sample_720p.py
```

## Pick the sample that matches your OBS resolution

The wire protocol has **no handshake and no resolution negotiation**: the frame
size must match the OBS canvas by construction. Each sample hardcodes one
resolution — copy the one matching the canvas/output resolution you set in OBS.
A mismatch produces a visibly wrong image (shear/garbage), not an error.

| OBS canvas | Sample |
|------------|--------|
| 854 x 480  | `samples/sample_480p.py` |
| 1280 x 720 | `samples/sample_720p.py` |
| 1920 x 1080 | `samples/sample_1080p.py` |

There is also `samples/sample_latency.py` (720p by default), which cycles solid
colors and prints a timestamp on each change — useful for measuring end-to-end
latency (see its docstring for options).

## Run

1. Install OBS and the obs-py-virtual-cam plugin.
2. In OBS, add the **obs-py-virtual-cam** source to a scene and set the
   canvas/output resolution to match your chosen sample.
3. Start **Virtual Camera** in OBS.
4. Run the matching sample:

   ```sh
   python3 samples/sample_720p.py
   ```

5. Select **OBS Virtual Camera** in Zoom / Teams / Meet / FaceTime.

Until the first Python frame arrives, the plugin shows a default video: the
source text of the python sample over a dark background with a blinking
cursor. Once frames stream in, the live frames replaces it.

## Using the client in your own code

```python
import numpy as np
from pyvirtualcam_obs import Camera

with Camera(width=1280, height=720, fps=30) as cam:
    frame = np.zeros((720, 1280, 4), dtype=np.uint8)
    frame[:, :, 3] = 255  # opaque alpha
    cam.send(frame)       # frame must be (height, width, 4) uint8 BGRA
```

`Camera` derives the socket path from `$TMPDIR/py-virtual-cam/feed.sock`
(falling back to `/tmp` when `TMPDIR` is unset). Pass `socket_path=...` to
override it. `send()` validates the frame shape and dtype and writes exactly
`width * height * 4` bytes.
