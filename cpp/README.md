# 3d_animate (C++)

A C++ reimplementation of `3d_animate` per [`../docs/Specifications.md`](../docs/Specifications.md):
load a 3D model, frame it, and spin it about its vertical axis — either in an
interactive window or by rendering exactly one 360° revolution offscreen to a
video file.

Unlike the Python reference (which is built on Panda3D), this port uses a small
self-contained OpenGL 3.3 renderer plus [Assimp](https://github.com/assimp/assimp)
for non-STL formats. STL is parsed directly.

## Dependencies

Build tools:

- A C++17 compiler (`g++` or `clang++`) and CMake ≥ 3.16.

Libraries (Debian/Ubuntu package names):

```bash
sudo apt-get update && sudo apt-get install -y \
    libassimp-dev \
    libgl1-mesa-dev libegl1-mesa-dev \
    libglfw3-dev          # only needed for the interactive viewer
```

For **headless export** on a machine with no GPU, also install a software GL
driver and force it on at runtime:

```bash
sudo apt-get install -y libgl1-mesa-dri libegl1
export LIBGL_ALWAYS_SOFTWARE=1
```

Runtime:

- [`ffmpeg`](https://ffmpeg.org/) on `PATH` — required for video export only.

Vendored (no action needed), under `third_party/`:

- `stb_image.h`, `stb_image_write.h` — image load / PNG write.
- `Simplify.h` — quadric-error-metric mesh decimation.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# -> build/3d_animate
```

To build export-only (no GLFW/interactive viewer):

```bash
cmake -S . -B build -DBUILD_INTERACTIVE=OFF
cmake --build build -j
```

## Usage

```
3d_animate <model> [options]
```

Options mirror the specification (`--speed`, `--color`, `--wireframe`,
`--edge-color`, `--bg-color`, `--output`, `--fps`, `--width`, `--height`,
`--simplify`); run `3d_animate --help` for the full list.

```bash
# Interactive viewer (needs a display)
build/3d_animate ../models/nda.stl --speed 90 --color e86430

# Transparent WebM (VP9), headless
LIBGL_ALWAYS_SOFTWARE=1 build/3d_animate ../models/nda.stl --output /tmp/nda.webm

# Transparent MOV (ProRes 4444)
build/3d_animate ../models/Car.fbx --output /tmp/car.mov --fps 60 --width 1280 --height 720

# Opaque MP4 (H.264) with simplified STL
build/3d_animate "../models/APS Bat Principal - Part 30.stl" --simplify 0.3 --output /tmp/s.mp4

# Facet-aware wireframe
build/3d_animate ../models/nda.stl --wireframe --edge-color 000000 --output /tmp/wire.mov
```

**Interactive controls:** `+`/`-` speed, `Space` pause/resume, `Esc` quit.

## Differences from the Python reference

These are intentional and do not affect the observable output for the supported
formats:

- **Model loading** uses Assimp (`.obj`, `.dae`, `.fbx`, `.ply`, `.3ds`,
  `.gltf`, `.glb`). Panda3D's native `.egg`/`.bam` formats are **not** supported.
  glTF needs no separate plugin — Assimp loads it directly.
- Assimp-loaded models are converted from Y-up to Z-up to match how Panda3D
  presents them (STL, typically Z-up CAD data, is used as-is).
- The camera field of view (unspecified by the spec, which only fixes the camera
  position) is a fixed 40° vertical.
- The optional, disabled-by-default mesh repair step (spec §4.2) is not
  implemented.

## Layout

```
src/            application sources (one concern per file)
third_party/    vendored single-header libraries
CMakeLists.txt  build definition
```
