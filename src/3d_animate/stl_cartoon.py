#!/usr/bin/env python3
"""
Load an STL file in Panda3D, spin it at a configurable speed, and render it
with a cartoon (cel) shader plus ink outlines.

Panda3D has no built-in STL loader, so this script parses STL (both the binary
and ASCII variants) directly into a Panda3D Geom.

Usage:
    python stl_cartoon.py model.stl
    python stl_cartoon.py model.stl --speed 90 --color e86430
    python stl_cartoon.py model.stl --speed -45 --no-ink --levels 4
    python stl_cartoon.py model.stl --output spin.webm --fps 60 --width 1280 --height 720

Controls (interactive mode only):
    +/-      speed up / slow down the rotation
    space    pause / resume
    escape   quit
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Annotated, Optional

import typer

from direct.filter.CommonFilters import CommonFilters
from direct.showbase.ShowBase import ShowBase
from panda3d.core import (
    AmbientLight,
    AsyncTask,
    DirectionalLight,
    Geom,
    GeomNode,
    GeomTriangles,
    GeomVertexData,
    GeomVertexFormat,
    GeomVertexWriter,
    LightRampAttrib,
    LVector3,
    NodePath,
    Vec4,
    loadPrcFileData,
)

Vec3 = tuple[float, float, float]
Triangle = tuple[Vec3, list[Vec3]]


# --------------------------------------------------------------------------- #
# STL parsing
# --------------------------------------------------------------------------- #
def _is_binary_stl(path: str) -> bool:
    """Heuristically decide whether an STL file is binary or ASCII.

    ASCII files begin with the token ``solid``, but so can some binary files,
    so we also validate the size implied by the binary triangle count.
    """
    with open(path, "rb") as fh:
        header = fh.read(80)
        count_bytes = fh.read(4)
        if len(count_bytes) < 4:
            return False  # too small to be a valid binary file
        (tri_count,) = struct.unpack("<I", count_bytes)
        fh.seek(0, 2)
        size = fh.tell()

    expected_binary_size = 84 + tri_count * 50
    if size == expected_binary_size:
        return True
    # Fall back to the leading-token check.
    return not header.lstrip()[:5].lower() == b"solid"


def _parse_binary_stl(path: str) -> list[Triangle]:
    triangles: list[Triangle] = []
    with open(path, "rb") as fh:
        fh.read(80)  # skip header
        (tri_count,) = struct.unpack("<I", fh.read(4))
        for _ in range(tri_count):
            data = fh.read(50)
            if len(data) < 50:
                break
            nx, ny, nz = struct.unpack("<3f", data[0:12])
            verts = struct.unpack("<9f", data[12:48])
            v: list[Vec3] = [
                (verts[0], verts[1], verts[2]),
                (verts[3], verts[4], verts[5]),
                (verts[6], verts[7], verts[8]),
            ]
            triangles.append(((nx, ny, nz), v))
    return triangles


def _parse_ascii_stl(path: str) -> list[Triangle]:
    triangles: list[Triangle] = []
    normal: Vec3 = (0.0, 0.0, 0.0)
    verts: list[Vec3] = []
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            parts = line.split()
            if not parts:
                continue
            key = parts[0].lower()
            if key == "facet" and len(parts) >= 5:
                normal = (float(parts[2]), float(parts[3]), float(parts[4]))
            elif key == "vertex" and len(parts) >= 4:
                verts.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif key == "endfacet":
                if len(verts) == 3:
                    triangles.append((normal, verts))
                verts = []
    return triangles


def load_stl(path: str) -> list[Triangle]:
    """Read an STL file and return a list of (normal, [v0, v1, v2]) triangles."""
    if _is_binary_stl(path):
        return _parse_binary_stl(path)
    return _parse_ascii_stl(path)


def _face_normal(v0: Vec3, v1: Vec3, v2: Vec3) -> LVector3:
    a = LVector3(*v1) - LVector3(*v0)
    b = LVector3(*v2) - LVector3(*v0)
    n = a.cross(b)
    if n.length_squared() > 0:
        n.normalize()
    return n


def stl_to_geomnode(triangles: list[Triangle], name: str = "stl") -> GeomNode:
    """Convert parsed STL triangles into a Panda3D GeomNode with normals.

    Vertex normals are accumulated per shared position so the model shades
    smoothly; positions are welded by exact coordinate match.
    """
    # Weld identical vertices and accumulate smooth normals.
    index_of: dict[Vec3, int] = {}
    positions: list[Vec3] = []
    normals: list[LVector3] = []
    faces: list[list[int]] = []

    for tri_normal, verts in triangles:
        n = LVector3(*tri_normal)
        if n.length_squared() == 0:
            n = _face_normal(*verts)
        face: list[int] = []
        for vp in verts:
            idx = index_of.get(vp)
            if idx is None:
                idx = len(positions)
                index_of[vp] = idx
                positions.append(vp)
                normals.append(LVector3(0, 0, 0))
            normals[idx] += n
            face.append(idx)
        faces.append(face)

    for n in normals:
        if n.length_squared() > 0:
            n.normalize()

    fmt = GeomVertexFormat.get_v3n3()
    vdata = GeomVertexData(name, fmt, Geom.UH_static)
    vdata.set_num_rows(len(positions))
    vwriter = GeomVertexWriter(vdata, "vertex")
    nwriter = GeomVertexWriter(vdata, "normal")
    for pos, nrm in zip(positions, normals):
        vwriter.add_data3(*pos)
        nwriter.add_data3(nrm)

    prim = GeomTriangles(Geom.UH_static)
    for a, b, c in faces:
        prim.add_vertices(a, b, c)
    prim.close_primitive()

    geom = Geom(vdata)
    geom.add_primitive(prim)
    node = GeomNode(name)
    node.add_geom(geom)
    return node


# --------------------------------------------------------------------------- #
# Application
# --------------------------------------------------------------------------- #
class STLViewer(ShowBase):
    def __init__(
        self,
        stl_path: str,
        speed: float,
        color: tuple[float, float, float],
        levels: int,
        ink: bool,
        output: str | None = None,
        fps: int = 30,
        width: int = 1920,
        height: int = 1080,
    ) -> None:
        self._offline = output is not None

        if self._offline:
            loadPrcFileData("", "window-type offscreen")
            loadPrcFileData("", "framebuffer-alpha true")
            loadPrcFileData("", f"win-size {width} {height}")

        super().__init__()

        if self._offline:
            self.set_background_color(0, 0, 0, 0)
        else:
            self.set_background_color(0.15, 0.16, 0.2, 1)

        self.speed = speed
        self.paused = False

        triangles = load_stl(stl_path)
        if not triangles:
            sys.exit(f"No triangles found in {stl_path!r} — is it a valid STL?")
        print(f"Loaded {len(triangles)} triangles from {stl_path}")

        node = stl_to_geomnode(triangles, name=stl_path)
        self.model = self.render.attach_new_node(node)
        self.model.set_color(Vec4(*color, 1))

        self._center_and_scale(self.model)
        self._setup_lights()
        self._setup_cartoon_shading(levels, ink)

        self.disable_mouse()
        self.camera.set_pos(0, -8, 1.5)
        self.camera.look_at(0, 0, 0)

        self.pivot = self.render.attach_new_node("pivot")
        self.model.reparent_to(self.pivot)

        if self._offline:
            self._output_path = output
            self._fps = fps
            duration = 360.0 / abs(speed)
            self._total_frames = max(1, int(round(duration * fps)))
            self._frame_idx = 0
            self._temp_dir = tempfile.mkdtemp(prefix="stl_video_")
            print(
                f"Rendering {self._total_frames} frames at {fps} fps"
                f" ({duration:.2f}s for 360°)…"
            )
            self.taskMgr.add(self._capture_task, "capture")
        else:
            self.taskMgr.add(self._spin_task, "spin")
            self._setup_keys()
            self._print_help()

    def _center_and_scale(self, np: NodePath) -> None:
        """Recenter the model on the origin and scale it to a unit-ish size."""
        min_b, max_b = np.get_tight_bounds()
        center = (min_b + max_b) * 0.5
        size = max_b - min_b
        largest = max(size.x, size.y, size.z, 1e-6)
        np.set_scale(3.0 / largest)
        np.set_pos(-center * (3.0 / largest))

    def _setup_lights(self) -> None:
        key = DirectionalLight("key")
        key.set_color(Vec4(1.0, 0.98, 0.9, 1))
        key_np = self.render.attach_new_node(key)
        key_np.set_hpr(-30, -60, 0)
        self.render.set_light(key_np)

        fill = DirectionalLight("fill")
        fill.set_color(Vec4(0.35, 0.4, 0.5, 1))
        fill_np = self.render.attach_new_node(fill)
        fill_np.set_hpr(150, -20, 0)
        self.render.set_light(fill_np)

        ambient = AmbientLight("ambient")
        ambient.set_color(Vec4(0.25, 0.25, 0.3, 1))
        self.render.set_light(self.render.attach_new_node(ambient))

    def _setup_cartoon_shading(self, levels: int, ink: bool) -> None:
        """Apply cel shading (a stepped light ramp) and optional ink outlines."""
        # Auto shader turns the fixed-function lights into a shader we can ramp.
        self.render.set_shader_auto()

        if levels <= 2:
            ramp = LightRampAttrib.make_single_threshold(0.5, 0.6)
        else:
            ramp = LightRampAttrib.make_double_threshold(0.3, 0.65, 0.4, 0.8)
        self.render.set_attrib(ramp)

        if ink:
            self.filters = CommonFilters(self.win, self.cam)
            ok = self.filters.set_cartoon_ink(separation=1, color=(0.0, 0.0, 0.0, 1.0))
            if not ok:
                print("Warning: cartoon ink filter unavailable on this GPU.")

    # -- Interactive mode tasks and keys --

    def _spin_task(self, task: AsyncTask) -> int:
        if not self.paused:
            dt = self.clock.get_dt()
            self.pivot.set_h(self.pivot.get_h() + self.speed * dt)
        return task.cont

    def _setup_keys(self) -> None:
        self.accept("escape", sys.exit)
        self.accept("space", self._toggle_pause)
        self.accept("+", self._change_speed, [15])
        self.accept("=", self._change_speed, [15])  # unshifted +
        self.accept("-", self._change_speed, [-15])

    def _toggle_pause(self) -> None:
        self.paused = not self.paused
        print("Paused" if self.paused else "Resumed")

    def _change_speed(self, delta: float) -> None:
        self.speed += delta
        print(f"Speed: {self.speed:.0f} deg/s")

    def _print_help(self) -> None:
        print("Controls:  +/-  speed    space  pause    esc  quit")
        print(f"Rotation speed: {self.speed:.0f} deg/s")

    # -- Offscreen video capture --

    def _capture_task(self, task: AsyncTask) -> int:
        if self._frame_idx >= self._total_frames:
            print()  # newline after progress line
            self._encode_video()
            sys.exit(0)

        angle = self._frame_idx * self.speed * (1.0 / self._fps)
        self.pivot.set_h(angle)

        self.graphicsEngine.render_frame()
        frame_path = os.path.join(self._temp_dir, f"frame_{self._frame_idx:06d}.png")
        self.win.save_screenshot(frame_path)

        self._frame_idx += 1
        print(
            f"\rFrame {self._frame_idx}/{self._total_frames}",
            end="",
            flush=True,
        )
        return task.cont

    def _encode_video(self) -> None:
        ext = os.path.splitext(self._output_path)[1].lower()
        frame_pattern = os.path.join(self._temp_dir, "frame_%06d.png")

        if ext == ".webm":
            # VP9 supports RGBA — full transparency
            codec_args = ["-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p", "-b:v", "0", "-crf", "30"]
        elif ext == ".mov":
            # ProRes 4444 supports RGBA — full transparency
            codec_args = ["-c:v", "prores_ks", "-profile:v", "4", "-pix_fmt", "yuva444p10le"]
        else:
            # H.264 / generic: no alpha channel; composite against black
            if ext == ".mp4":
                print(
                    "Note: H.264 inside MP4 does not support alpha."
                    " Use .webm or .mov for a transparent output."
                )
            codec_args = ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18"]

        cmd = [
            "ffmpeg",
            "-y",
            "-framerate",
            str(self._fps),
            "-i",
            frame_pattern,
            *codec_args,
            self._output_path,
        ]
        print(f"Encoding: {' '.join(cmd)}")
        subprocess.run(cmd, check=True)
        print(f"Saved: {self._output_path!r}")
        shutil.rmtree(self._temp_dir, ignore_errors=True)


app = typer.Typer(help="Spin an STL model with a cartoon shader.")


def _parse_hex_color(value: str) -> tuple[float, float, float]:
    hex_str = value.lstrip("#")
    if len(hex_str) != 6:
        raise typer.BadParameter("Color must be a 6-digit hex string, e.g. ff6600 or #ff6600")
    try:
        r, g, b = int(hex_str[0:2], 16), int(hex_str[2:4], 16), int(hex_str[4:6], 16)
    except ValueError:
        raise typer.BadParameter("Invalid hex color: " + value)
    return r / 255.0, g / 255.0, b / 255.0


@app.command()
def main(
    stl: Annotated[str, typer.Argument(help="path to the .stl file")],
    speed: Annotated[
        float, typer.Option(help="rotation speed in degrees/second (negative reverses)")
    ] = 45.0,
    color: Annotated[
        str, typer.Option(help="base RGB color as hex, e.g. ff0000 or #ff6600")
    ] = "ff0000",
    levels: Annotated[
        int, typer.Option(help="number of cel-shading bands (2 = hard, 3+ = softer)")
    ] = 3,
    ink: Annotated[
        bool, typer.Option("--ink/--no-ink", help="enable or disable the black ink outline")
    ] = True,
    output: Annotated[
        Optional[str],
        typer.Option(
            help=(
                "render a full 360° rotation to this video file and exit"
                " (.webm/.mov = transparent background, .mp4 = opaque)"
            )
        ),
    ] = None,
    fps: Annotated[int, typer.Option(help="frames per second for video output")] = 30,
    width: Annotated[int, typer.Option(help="video width in pixels")] = 1920,
    height: Annotated[int, typer.Option(help="video height in pixels")] = 1080,
) -> None:
    viewer = STLViewer(
        stl,
        speed,
        _parse_hex_color(color),
        levels,
        ink,
        output=output,
        fps=fps,
        width=width,
        height=height,
    )
    viewer.run()
