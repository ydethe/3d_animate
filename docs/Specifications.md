# 3d_animate — Functional Specification

This document specifies the observable behaviour of `3d_animate` in a
language- and implementation-agnostic way. It describes *what* the program does —
its inputs, transformations, outputs, and error conditions — not *how* the
reference implementation is coded. A conforming reimplementation in any language
should produce equivalent results given the same inputs.

---

## 1. Purpose

`3d_animate` loads a single 3D model file, frames it, and rotates it continuously
about its vertical axis. It operates in one of two mutually exclusive modes:

- **Interactive mode** — opens a real-time window showing the model spinning,
  with keyboard controls.
- **Export mode** — renders exactly one full 360° revolution offscreen and
  encodes it to a video file, then exits.

Optional rendering styles (flat-shaded solid, or edge-only wireframe) and mesh
preprocessing (decimation/simplification) are available in both modes.

---

## 2. Invocation and inputs

The program is invoked with one required positional argument (the model file
path) and a set of optional named parameters.

### 2.1 Parameters

| Parameter | Type | Default | Meaning |
|-----------|------|---------|---------|
| `model` (positional) | file path | *(required)* | Path to the 3D model to load. |
| `speed` | real number | `45.0` | Rotation rate in **degrees per second**. A negative value reverses the direction of rotation. |
| `color` | hex color string | `ff0000` | Base RGB color applied to the model when it has no texture of its own. |
| `wireframe` | boolean | `false` | When true, render only facet-boundary edges with the solid surface hidden. |
| `edge_color` | hex color string | `000000` | Color of the wireframe edges. Only meaningful when `wireframe` is true. |
| `bg_color` | hex color string or none | none (transparent) | Background color. When omitted, the background is fully transparent (alpha = 0). |
| `output` | file path or none | none | When set, selects **export mode**: render one 360° revolution to this video file and exit. When omitted, the program runs in **interactive mode**. |
| `fps` | integer | `30` | Frames per second of the exported video (export mode only). |
| `width` | integer (pixels) | `1920` | Pixel width of the exported video (export mode only). |
| `height` | integer (pixels) | `1080` | Pixel height of the exported video (export mode only). |
| `simplify` | real number in `(0, 1]` or none | none | Target fraction of the original face count to keep after mesh decimation. Applies to STL input only. |

### 2.2 Color string format

A color is a 6-digit hexadecimal RGB string, optionally prefixed with `#`
(e.g. `ff6600` or `#ff6600`). It is parsed into three channel values in the range
`[0, 1]` by interpreting each pair of hex digits as an 8-bit integer and dividing
by 255. Any string that is not exactly 6 hex digits after stripping a leading `#`
is an input error.

### 2.3 Parameter validation

- `simplify`, if provided, must lie strictly above 0 and at most 1 (i.e. in
  `(0, 1]`). A value outside this range is an input error.
- If `output` is set and its parent directory does not exist, the parent
  directory is created (recursively) before rendering.

---

## 3. Model loading

The loader dispatches on the lowercase file extension.

### 3.1 STL files (`.stl`)

STL files are parsed directly (no third-party loader). The result is a list of
triangles, each represented as a face normal vector plus an ordered list of three
vertex positions `(v0, v1, v2)`.

**Binary vs. ASCII detection.** STL exists in a binary and an ASCII variant. The
variant is determined heuristically:

1. Read the first 80 bytes (header) and the next 4 bytes as a little-endian
   unsigned 32-bit integer `tri_count`.
2. If fewer than 4 bytes are available for the count, the file is treated as
   **not** a valid binary STL.
3. Compute the expected binary size: `84 + tri_count * 50` bytes (80 header +
   4 count + 50 bytes per triangle). If the actual file size equals this value,
   the file is **binary**.
4. Otherwise, the file is **ASCII** if and only if its first non-whitespace
   5 characters are *not* the token `solid` (case-insensitive). (ASCII STL files
   begin with `solid`, but so do some binary files, so the size check above takes
   precedence.)

**Binary parsing.** Skip the 80-byte header, read the 4-byte triangle count, then
for each triangle read a fixed 50-byte record: 3 little-endian 32-bit floats for
the normal, followed by 9 little-endian 32-bit floats for the three vertices
(x, y, z each). The trailing 2-byte attribute field of each record is ignored. If
a record is truncated (fewer than 50 bytes remain), parsing stops.

**ASCII parsing.** Scan line by line, tokenizing on whitespace (case-insensitive
keywords):

- a line beginning with `facet` with at least 5 tokens sets the current normal
  from tokens 3–5 (the `normal nx ny nz` form);
- a line beginning with `vertex` with at least 4 tokens appends a vertex from
  tokens 2–4;
- a line beginning with `endfacet` emits a triangle using the current normal and
  the accumulated vertices **only if exactly three vertices** were collected, then
  resets the vertex accumulator.

Malformed bytes are replaced rather than causing a hard failure.

**Empty result.** If STL parsing yields zero triangles, the program terminates
with an error indicating the file contains no triangles / may be invalid.

### 3.2 Non-STL files

All other formats are loaded through a general 3D asset loader. The reference
implementation relies on the engine's native and plugin loaders and supports, at
minimum: `.egg`, `.bam` (native); `.obj`, `.dae`, `.fbx`, `.ply`, `.3ds` (via an
Assimp-based plugin); and `.gltf`, `.glb` (via an optional glTF plugin).

- For glTF/GLB input, if the optional glTF support is not installed, the program
  terminates with an error telling the user to install it.
- Textures referenced by the model are resolved **relative to the model file's
  own directory** and loaded automatically. The model's directory is added to the
  asset search path before loading.
- If the loader returns nothing or an empty model, the program terminates with an
  error indicating the file is unsupported or corrupt.
- Whether the loaded model carries any textures is recorded; it affects shading
  (see §6.2).

---

## 4. Mesh preprocessing

### 4.1 Simplification / decimation (STL only)

When `simplify = r` is provided (with `r` in `(0, 1]`), the STL triangle list is
decimated to approximately a fraction `r` of its original face count **before**
any geometry is built:

1. Weld vertices by exact coordinate equality to produce an indexed mesh
   (unique vertex list + integer face list).
2. Apply quadric-error-metric edge-collapse decimation with a target of
   `max(1, round(original_face_count * r))` faces. Equivalently, the reduction
   fraction is `1 - r` clamped to `[0, 1]`.
3. Rebuild the triangle list from the decimated vertices and faces, using each
   decimated face's recomputed normal.

Values of `r` near 1 preserve nearly all faces; values near 0 produce a coarse
approximation. Simplification applies only to STL input; it has no effect on
models loaded through the general loader.

### 4.2 Mesh repair (specified, currently disabled)

A mesh-repair capability is defined but is **not active** in the current default
pipeline. When enabled, it welds the raw STL triangles into an indexed mesh and
performs, in order: removal of duplicate and unreferenced geometry, repair of
non-manifold edges and vertices (each best-effort; failures are warned and
skipped), coherent re-orientation of face winding across connected components,
geometry-based outward orientation, and per-face normal recomputation. Its purpose
is to correct inconsistent winding and inward-facing normals that make flat
shading look blotchy. A conforming implementation MAY offer this but is not
required to enable it by default.

---

## 5. Geometry construction

### 5.1 Solid surface (from STL triangles)

STL triangles are converted into a renderable mesh with smooth (per-vertex)
normals:

1. Weld vertices by exact coordinate equality into a unique position list.
2. For each triangle, determine its face normal: use the stored normal if it is
   nonzero; otherwise compute it as the normalized cross product
   `(v1 - v0) × (v2 - v0)`.
3. Accumulate (sum) each face's normal onto every one of its three vertices.
4. After all triangles are processed, normalize each vertex's accumulated normal.
5. Emit an indexed triangle mesh carrying position + normal per vertex.

This produces smooth shading across coplanar and adjacent faces that share welded
vertices.

### 5.2 Extracting triangles from non-STL models (for wireframe)

Wireframe rendering requires raw triangles. For non-STL models, whose loader
returns an opaque scene graph rather than vertex arrays, triangles are read back
out: walk every geometry node, decompose each primitive into triangles, and read
the three vertex positions of each, transforming them into the model's local
coordinate frame. Per-triangle normals are left as zero because the wireframe
builder (§5.3) recomputes connectivity purely from positions. If no triangle
geometry is found, the program terminates with an error.

### 5.3 Facet-aware wireframe / outline

The wireframe is **not** a full edge-per-triangle mesh; it draws only the
boundary edges of each flat facet, suppressing edges interior to a flat region:

1. Weld vertices by exact coordinate equality into an indexed mesh.
2. Group triangles into **facets** — maximal sets of adjacent, coplanar
   triangles.
3. For each facet, count how many triangles within the facet use each undirected
   edge (an edge `(a, b)` is normalized to `(min, max)`):
   - edges used by exactly **one** triangle are facet-boundary edges → **drawn**;
   - edges used by **two** triangles are interior to the flat facet →
     **suppressed**.
4. Triangles that belong to **no** facet group have **all three** of their edges
   drawn.
5. Emit a line-primitive mesh containing exactly the set of drawn edges (each
   unique edge once).

The result is a clean outline that shows the silhouette and the creases between
flat faces without the triangulation noise of a naive wireframe.

---

## 6. Scene setup and rendering

### 6.1 Framing (center and scale)

The model (and, in wireframe mode, the outline) is centered and uniformly scaled
to a consistent on-screen size:

1. Compute the tight axis-aligned bounding box `[min, max]`.
2. Let `largest = max(size_x, size_y, size_z, 1e-6)` where `size = max - min`.
3. Apply a uniform scale factor of `3.0 / largest`.
4. Translate so the (scaled) bounding-box center lands on the world origin.

The constant `3.0` is the target bounding size in world units; the `1e-6` floor
guards against division by zero for degenerate models.

### 6.2 Appearance

- **Wireframe mode:** the solid surface is hidden; only the outline mesh is
  shown, drawn in `edge_color` with a line thickness of 2. No scene lighting is
  applied.
- **Solid mode:** scene lighting is enabled (§6.3).
  - If the loaded model carries its own textures/materials, those are preserved
    (automatic shading is enabled) and the `color` parameter is **not** applied.
  - If the model has no texture, it is tinted with the flat `color`.

### 6.3 Lighting (solid mode only)

Three lights are used:

- A **key** directional light, warm-white (approx. RGB `1.0, 0.98, 0.9`), aimed
  from heading −30°, pitch −60°.
- A **fill** directional light, cool and dim (approx. RGB `0.35, 0.4, 0.5`), aimed
  from heading 150°, pitch −20°.
- An **ambient** light (approx. RGB `0.25, 0.25, 0.3`).

### 6.4 Camera

The camera is fixed (no mouse control). It is positioned at `(0, −8, 1.5)` in
world space and oriented to look at the origin `(0, 0, 0)`. Rotation is applied to
the model via a pivot node, not by moving the camera.

### 6.5 Rotation model

The model is parented under a pivot node at the origin. Rotation is applied as the
pivot's heading (yaw) about the vertical axis. One full revolution is 360°.

- **Interactive mode:** each frame, if not paused, the heading advances by
  `speed * dt`, where `dt` is the elapsed real time since the previous frame.
- **Export mode:** frame `i` (0-indexed) sets the heading deterministically to
  `i * speed / fps` degrees (see §8).

### 6.6 Background

The background clear color is set from `bg_color` with alpha 1.0 when a background
color is given, or to fully transparent `(0, 0, 0, 0)` when none is given. When
the background is transparent, or when exporting, the framebuffer is configured to
include an alpha channel.

---

## 7. Interactive mode

Active when `output` is not set. The window updates continuously, advancing
rotation per §6.5. The following keyboard controls are bound:

| Key | Action |
|-----|--------|
| `+` (or unshifted `=`) | Increase rotation speed by 15 deg/s |
| `-` | Decrease rotation speed by 15 deg/s |
| `Space` | Toggle pause/resume of rotation |
| `Escape` | Quit the program |

Speed changes are additive and unbounded (including passing through zero and
going negative, which reverses direction). On startup, a short help summary and
the current rotation speed are reported to the user.

---

## 8. Export mode

Active when `output` is set. Rendering is performed **offscreen** (no visible
window is required, though a GPU/OpenGL context is still needed), at `width ×
height` pixels.

### 8.1 Frame count

The export covers exactly one full revolution:

- `duration_seconds = 360.0 / |speed|`
- `total_frames = max(1, round(duration_seconds * fps))`

### 8.2 Capture buffer

Rendering targets a dedicated offscreen buffer that explicitly requests 8 bits per
channel **including 8 alpha bits** and a 24-bit depth buffer. This guarantees a
usable alpha channel (for transparent backgrounds) independent of the host
display's default visual. The buffer is cleared to the configured background color
each frame. If such a buffer cannot be created, the implementation falls back to
capturing the main window and warns that the background may be opaque; if the
buffer is created but lacks alpha, it warns that output will be opaque.

### 8.3 Frame rendering loop

For each frame index `i` from `0` to `total_frames - 1`:

1. Set the pivot heading to `i * speed * (1 / fps)` degrees (deterministic;
   independent of real elapsed time).
2. Render one frame.
3. Save the rendered frame to a temporary image file (PNG) named with a
   zero-padded 6-digit index (e.g. `frame_000123.png`).

Progress is reported to the user (frames completed out of total, with an estimated
time remaining). Temporary frames are written to a dedicated temporary directory.

### 8.4 Video encoding

After all frames are captured, they are encoded into the requested container
using an external encoder (the reference implementation shells out to `ffmpeg`).
The codec and pixel format are selected by the output file's lowercase extension:

| Extension | Codec | Pixel format | Alpha | Notes |
|-----------|-------|--------------|-------|-------|
| `.webm` | VP9 | `yuva420p` | yes (transparent) | Constant-quality: `-b:v 0 -crf 30`. |
| `.mov` | ProRes 4444 | `yuva444p10le` | yes (transparent) | `-profile:v 4`. |
| `.mp4` | H.264 | `yuv420p` | no (opaque) | `-crf 18`; a warning is emitted that MP4/H.264 cannot carry alpha. |
| any other | H.264 | `yuv420p` | no (opaque) | Same as `.mp4` (default fallback), without the warning. |

The input frame rate passed to the encoder is `fps`. Existing output files are
overwritten.

### 8.5 Cleanup and exit

On successful encoding, the temporary frame directory is removed and the program
reports the saved output path, then exits. If the external encoder fails
(non-zero exit), its error output is surfaced, the temporary directory is removed,
and the program terminates with an error.

---

## 9. Error conditions (summary)

The program terminates with a user-facing error in these cases:

- Color string is not exactly 6 hex digits (after optional `#`), or contains
  non-hex characters.
- `simplify` is provided but outside `(0, 1]`.
- An STL file parses to zero triangles.
- A non-STL file cannot be loaded, or loads to an empty model.
- A `.gltf`/`.glb` file is given but optional glTF support is not installed.
- Wireframe is requested but no triangle geometry can be obtained from the model.
- The external video encoder fails during export.

---

## 10. External dependencies and environment

- A 3D rendering engine providing a scene graph, lighting, offscreen rendering,
  and native/plugin model loaders (STL excepted — it is parsed directly).
- Mesh-processing libraries for facet detection (wireframe), quadric decimation
  (simplify), and — if the optional repair step is enabled — topology repair.
- An external video encoder (`ffmpeg`) available on the system `PATH`, required
  **only** for export mode.
- Export mode performs offscreen rendering and therefore works without a visible
  display, but still requires a working OpenGL context (hardware GPU, or a
  software renderer such as Mesa `llvmpipe` on headless servers).
