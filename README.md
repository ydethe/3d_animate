# 3d_animate

Spin a 3D model in a viewer and optionally render a full 360° rotation to video.

Built on [Panda3D](https://www.panda3d.org/) with a custom STL parser and Panda3D's
native loaders for everything else.

## Features

- Loads binary and ASCII STL files (custom parser)
- Loads other formats through Panda3D: `.egg`/`.bam` natively, `.obj`/`.dae`/`.fbx`/`.ply`/`.3ds`
  via the bundled Assimp plugin, and `.gltf`/`.glb` via `panda3d-gltf`
- Loads referenced/embedded textures automatically (resolved next to the model file)
- Interactive viewer with keyboard controls
- Wireframe / outline mode (facet-aware edge detection via trimesh; STL only)
- Offscreen video export: `.webm` (transparent), `.mov` (transparent), `.mp4` (opaque)
- Configurable rotation speed, model color, edge color, and background color

## Requirements

- Python ≥ 3.12
- [ffmpeg](https://ffmpeg.org/) in `PATH` (required for video export only)

## Installation

```bash
pip install 3d_animate
```

Or from source with [PDM](https://pdm-project.org/):

```bash
pdm install
```

## Usage

```
python -m 3d_animate <model.stl> [OPTIONS]
```

### Interactive mode

```bash
# Basic viewer
python -m 3d_animate model.stl

# Custom speed and color
python -m 3d_animate model.stl --speed 90 --color e86430

# Wireframe with custom edge color
python -m 3d_animate model.stl --wireframe --edge-color 000000

# Dark background
python -m 3d_animate model.stl --bg-color 1a1a2e
```

**Keyboard controls (interactive mode only):**

| Key | Action |
|-----|--------|
| `+` / `-` | Speed up / slow down rotation |
| `Space` | Pause / resume |
| `Escape` | Quit |

### Video export

```bash
# Transparent WebM (VP9)
python -m 3d_animate model.stl --output spin.webm

# Transparent MOV (ProRes 4444)
python -m 3d_animate model.stl --output spin.mov --fps 60 --width 1280 --height 720

# Opaque MP4 (H.264)
python -m 3d_animate model.stl --output spin.mp4
```

### Headless / server rendering

Video export uses an offscreen window, so it works on a machine with no display —
but Panda3D still needs an OpenGL context. On a headless server you typically have
neither an X display nor EGL, so rendering fails with:

```
Unable to load libp3headlessgl.so: libEGL.so.1: cannot open shared object file
Unable to open 'offscreen' window.
```

Install EGL plus a software OpenGL renderer (one-time, needs root):

```bash
sudo apt-get update && sudo apt-get install -y libegl1 libgl1-mesa-dri libgbm1
```

Then export as usual. On a server with no GPU, force the Mesa software renderer:

```bash
LIBGL_ALWAYS_SOFTWARE=1 python -m 3d_animate model.stl --output spin.mp4
```

`libegl1` provides the `libEGL.so.1` Panda3D's headless GL needs, and
`libgl1-mesa-dri` provides Mesa's software renderer (`llvmpipe`) so no GPU is
required.

### All options

| Option | Default | Description |
|--------|---------|-------------|
| `stl` | *(required)* | Path to the `.stl` file |
| `--speed` | `45.0` | Rotation speed in degrees/second (negative reverses) |
| `--color` | `ff0000` | Model RGB color as hex (`ff6600` or `#ff6600`) |
| `--wireframe` / `--no-wireframe` | off | Render edges only with transparent faces |
| `--edge-color` | `000000` | Edge color as hex (wireframe mode) |
| `--bg-color` | transparent | Background color as hex |
| `--output` | *(none)* | Render 360° to this video file and exit |
| `--fps` | `30` | Frames per second for video output |
| `--width` | `1920` | Video width in pixels |
| `--height` | `1080` | Video height in pixels |

## License

See [LICENSE](LICENSE).
