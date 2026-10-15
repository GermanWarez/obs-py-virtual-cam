# obs-py-virtual-cam

An OBS plugin that adds a new video source fed from Python. Because it is a
regular OBS source, you can mix, scale, filter, and composite it with anything
else in your scene. Send frames from Python with a small client that feels like
`pyvirtualcam`, and route the mixed result out through OBS's built-in **Virtual
Camera** to Zoom, Teams, and other apps.

Python sends raw BGRA frames over a UNIX domain socket; no custom CMIO system
extension is involved.

```text
Python writes a BGRA frame (your code)
          │
          ▼  Unix domain socket
          │
OBS plugin "obs-py-virtual-cam"
          │
          ▼
OBS scene (mix and arrange)
          │
          ▼
OBS Virtual Camera
```

See [`python/README.md`](./python/README.md) for client details.

## Prerequisites

- **OBS Studio 32.x** (developed and tested against 32.2.2).
- **CMake 3.30+**.
- **Xcode (current)** with the Command Line Tools — Apple clang.
- Apple Silicon (arm64) macOS. The build also targets x86_64 for a Universal
  binary; a running OBS is only needed for the manual load check, not the build.
- **Python 3** with **NumPy** for the client samples (`pip install numpy`).
- Using a prebuilt download? See
  [Prebuilt downloads and macOS Gatekeeper](#prebuilt-downloads-and-macos-gatekeeper).

## Build (macOS, Apple Silicon)

The project uses the standard `obs-plugintemplate` modern-CMake layout.
`buildspec.json` pins OBS 32.2.2 and the matching prebuilt `obs-deps`; CMake
fetches the libobs headers/libraries into the build tree, so you do **not** need
OBS source or dev headers installed.

From the repository root:

```sh
cmake --preset macos
cmake --build --preset macos
```

This produces the plugin bundle at:

```text
build_macos/RelWithDebInfo/py-virtual-cam-feed.plugin
```

## Install

OBS loads user plugins from:

```text
~/Library/Application Support/obs-studio/plugins/
```

That directory may not exist yet — create it, then copy the built bundle in:

```sh
mkdir -p ~/Library/Application\ Support/obs-studio/plugins
cp -R build_macos/RelWithDebInfo/py-virtual-cam-feed.plugin \
      ~/Library/Application\ Support/obs-studio/plugins/
```

Restart OBS. The source then appears in **Sources → Add** as
**obs-py-virtual-cam**.

### Prebuilt downloads and macOS Gatekeeper

A plugin you **build yourself** on your own Mac loads without any extra steps —
nothing below applies to the local build above.

A plugin you **download** (e.g. a prebuilt `.plugin` from a GitHub release) is a
different case. Prebuilt bundles here are only **ad-hoc signed** (no Apple
Developer ID, no notarization). macOS tags every downloaded file with a
`com.apple.quarantine` attribute, and Gatekeeper then distrusts an ad-hoc-signed
bundle — so OBS will **not** load the plugin (it silently skips a plugin it
cannot load). The bundle is not broken; it is only untrusted because it was
downloaded.

To use a downloaded build, remove the quarantine flag, then restart OBS:

```sh
xattr -dr com.apple.quarantine \
  ~/Library/Application\ Support/obs-studio/plugins/py-virtual-cam-feed.plugin
```

`-dr` deletes the attribute recursively through the whole bundle. After this the
plugin loads normally. Only run it on a bundle you trust — clearing quarantine
is the same decision as "open anyway" in the Gatekeeper dialog.

The alternative is to build from source, as described above.

## Usage (end to end)

1. **Install OBS** (32.x).
2. **Install the plugin** — drop `py-virtual-cam-feed.plugin` into
   `~/Library/Application Support/obs-studio/plugins/` (see above) and restart
   OBS.
3. **Add the source** — in OBS, add the **obs-py-virtual-cam** source to a
   scene, and set the canvas/output resolution (Settings → Video) to match the
   Python sample you will run (854×480, 1280×720, or 1920×1080). OBS otherwise
   letterboxes/scales per the scene settings.
4. **Start Virtual Camera** — click **Start Virtual Camera** in OBS.
5. **Run the matching Python sample** — pick the sample for your chosen
   resolution and run it:

   ```sh
   cd python
   python3 samples/sample_720p.py
   ```

6. **Select the camera** — choose **OBS Virtual Camera** in Zoom / Teams / Meet
   / FaceTime.

Until the first Python frame arrives, the source shows a placeholder: the
selected sample's source code over a dark background with a blinking cursor.
Once frames stream in, the live pattern replaces it.

### Sending from your own code

The samples use the `pyvirtualcam_obs` package (`python/pyvirtualcam_obs/`).
Import `Camera`, open it as a context manager, and call `send()` once per frame
with a BGRA NumPy array of exactly the configured size:

```python
import numpy as np
from pyvirtualcam_obs import Camera

with Camera(width=1280, height=720, fps=30) as cam:
    frame = np.zeros((720, 1280, 4), dtype=np.uint8)  # H x W x 4, BGRA
    frame[:, :, 3] = 255                              # opaque alpha
    frame[310:410, 590:690] = 255                     # add a white box
    while True:
        cam.send(frame)
```

`Camera` connects to the hardcoded socket path (override with
`socket_path=`). See [`python/README.md`](./python/README.md) for details.

## Implementation details

### Architecture

- **Client** — a Python client (`python/pyvirtualcam_obs/`) writes raw BGRA
  frames to a UNIX domain socket; one-way. Other clients can be used as well.
- **Plugin for OBS** — the `obs-py-virtual-cam` plugin reads the frames from the
  socket and provides them to OBS as a video source.
- **OBS** — composites the source in a scene and presents the result through its
  built-in OBS Virtual Camera.

The plugin does no format checks or conversion: the frames go straight through
to OBS, so the client must send them in the correct format.

### Default settings

The default resolution is 1280x720 and can be changed to 854x480 or 1920x1080.

While no client is connected, a default video is displayed. That default video
shows the source code of the Python sample used to send images to the socket. It
is generated at build time: `cmake/GenerateSampleSources.cmake` reads
`python/samples/sample_480p.py`, `sample_720p.py`, and `sample_1080p.py` and
emits escaped C string literals into `src/generated/sample_sources.h`. The
header is produced into the source tree during the build and is gitignored —
edit the samples, rebuild, and the placeholder updates automatically. Never edit
`src/generated/sample_sources.h` by hand.

### Socket path

The socket path is derived identically by the plugin and the Python package
`pyvirtualcam_obs`. Both use:

```text
$TMPDIR/py-virtual-cam/feed.sock
```

falling back to `/tmp` when `TMPDIR` is unset. The parent directory is created
owner-only (`0700`) and the socket file is `0600`. The plugin shows this path as
a **read-only** field in its source properties, handy for debugging or for
pointing another client at it. The client's `socket_path=` argument can override
the path on the Python side if you ever need a different one.

### Data format

A continuous stream of raw, fixed-size BGRA frames, back to back — **no header,
no handshake**. Each frame is exactly `width * height * 4` bytes. The frame size
must match the OBS canvas by construction; a mismatch produces a visibly wrong
image, not an error. Recovery is connection-lifecycle only: on EOF/error the
plugin drops the connection and awaits a new one.

### Repository layout

| Path | Purpose |
|------|---------|
| `src/` | C plugin sources (`plugin-main.c`, `py-virtual-cam-source.c`, `uds.c`, `text_render.c`) |
| `src/generated/` | Build-time generated `sample_sources.h` (gitignored) |
| `python/pyvirtualcam_obs/` | Client package (Python) |
| `python/samples/` | Ready-to-run samples |
| `tests/` | Unit tests (C) and Python roundtrip test |
| `cmake/`, `CMakeLists.txt`, `buildspec.json`, `CMakePresets.json` | Build config |

### Tests

```sh
# C unit tests (no libobs link)
clang -std=c17 -o /tmp/t tests/test_text_render.c src/text_render.c && /tmp/t
clang -std=c17 -o /tmp/u tests/test_uds.c src/uds.c && /tmp/u

# Python UDS roundtrip: asserts exact per-frame byte counts for all three
# resolutions against the real client (requires numpy)
python3 tests/test_uds_roundtrip.py
```

The source-name / default-resolution / blinking-cursor placeholder behaviours
need a running OBS and are verified manually via the usage flow above.

## Other clients

The socket takes raw BGRA frames, so anything that can produce them works — not
only the Python client. The following examples show how to use the plugin with
other clients. However, for a plain video file it's faster and easier to use
OBS's built-in Media Source.

### ffmpeg

FFmpeg can decode a video file and emit 720p BGRA frames, but it can't write
straight to the socket. To make it work, you need a tool like netcat (`nc`)
installed as well:

```sh
ffmpeg -re -i sample.mp4 -s 1280x720 -pix_fmt bgra -f rawvideo pipe:1 \
  | nc -U "$TMPDIR/py-virtual-cam/feed.sock"
```

- `-i sample.mp4` reads the file `sample.mp4` (not included).
- `-re` paces output at the file's native frame rate.
- `-s 1280x720 -pix_fmt bgra` matches the default resolution of the plugin, and
  sets the BGRA-only format.
- `nc -U` connects to the UNIX domain socket (`-U`) and writes the byte stream.

### GStreamer

GStreamer can build the same raw-BGRA stream from a pipeline. Like ffmpeg it
writes to stdout and reaches the socket through `nc`:

```sh
gst-launch-1.0 -q filesrc location=sample.mp4 ! decodebin ! videoconvert \
  ! videoscale ! video/x-raw,format=BGRA,width=1280,height=720 \
  ! fdsink fd=1 \
  | nc -U "$TMPDIR/py-virtual-cam/feed.sock"
```

- `decodebin ! videoconvert ! videoscale` decodes and converts the input.
- `video/x-raw,format=BGRA,width=1280,height=720` forces the plugin's default
  resolution and the BGRA-only format.
- `fdsink fd=1` writes the raw frames to stdout; `nc -U` forwards them to the
  socket.

These clients are worth it only for inputs or processing OBS can't do natively.
The primary use case of this plugin is providing a virtual camera to Python.
