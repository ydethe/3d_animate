#!/usr/bin/env python3
"""
Load a 3D model in Panda3D, spin it at a configurable speed.

Panda3D has no built-in STL loader, so this script parses STL (both the binary
and ASCII variants) directly into a Panda3D Geom. Every other format is handed
to Panda3D's own loader: ``.egg`` and ``.bam`` load natively, and common
formats such as ``.obj``, ``.dae``, ``.fbx``, ``.ply`` and ``.3ds`` load through
the bundled Assimp plugin. glTF (``.gltf`` / ``.glb``) works when the optional
``panda3d-gltf`` package is installed. Any textures the model references are
resolved relative to the model's own directory and loaded automatically.

Usage:
    python stl_cartoon.py model.stl
    python stl_cartoon.py model.glb
    python stl_cartoon.py model.obj --speed 90 --color e86430
    python stl_cartoon.py model.stl --wireframe --edge-color 000000
    python stl_cartoon.py model.stl --bg-color 1a1a2e
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
from pathlib import Path
from typing import Annotated, Optional

import numpy as np
import pyfqmr
import typer
from direct.showbase.ShowBase import ShowBase
from panda3d.core import (
    AmbientLight,
    AsyncTask,
    DirectionalLight,
    Geom,
    GeomLines,
    GeomNode,
    GeomTriangles,
    GeomVertexData,
    GeomVertexFormat,
    GeomVertexWriter,
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
def _is_binary_stl(path: Path) -> bool:
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
    return not header.lstrip()[:5].lower() == b"solid"


def _parse_binary_stl(path: Path) -> list[Triangle]:
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


def _parse_ascii_stl(path: Path) -> list[Triangle]:
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


def load_stl(path: Path) -> list[Triangle]:
    """Read an STL file and return a list of (normal, [v0, v1, v2]) triangles."""
    if _is_binary_stl(path):
        return _parse_binary_stl(path)
    return _parse_ascii_stl(path)


def simplify_triangles(triangles: list[Triangle], target_count: int) -> list[Triangle]:
    """Reduce the mesh to *target_count* triangles using Fast Quadric Mesh Reduction."""
    verts_list: list[Vec3] = []
    index_of: dict[Vec3, int] = {}
    faces_list: list[tuple[int, int, int]] = []

    for _normal, verts in triangles:
        face_indices: list[int] = []
        for v in verts:
            if v not in index_of:
                index_of[v] = len(verts_list)
                verts_list.append(v)
            face_indices.append(index_of[v])
        faces_list.append((face_indices[0], face_indices[1], face_indices[2]))

    vertices = np.array(verts_list, dtype=np.float64)
    faces = np.array(faces_list, dtype=np.uint32)

    simplifier = pyfqmr.Simplify()
    simplifier.setMesh(vertices, faces)
    simplifier.simplify_mesh(target_count=target_count, aggressiveness=10, verbose=False)
    new_verts, new_faces, new_normals = simplifier.getMesh()

    result: list[Triangle] = []
    for i, (a, b, c) in enumerate(new_faces):
        n = tuple(float(x) for x in new_normals[i])
        v = [
            tuple(float(x) for x in new_verts[a]),
            tuple(float(x) for x in new_verts[b]),
            tuple(float(x) for x in new_verts[c]),
        ]
        result.append((n, v))  # type: ignore[arg-type]
    return result


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


def build_facet_outline_geomnode(triangles: list[Triangle], name: str = "outline") -> GeomNode:
    """Build a GeomNode with only the perimeter edges of each flat face.

    Uses trimesh to detect coplanar adjacent triangle groups (facets). Within
    each group, edges shared by exactly one triangle are boundary edges and get
    drawn; edges shared by two triangles are interior to the flat face and are
    suppressed. Triangles not part of any facet group have all three edges drawn.
    """
    import trimesh

    verts_list: list[Vec3] = []
    index_of: dict[Vec3, int] = {}
    faces_list: list[tuple[int, int, int]] = []

    for _normal, verts in triangles:
        face_idxs: list[int] = []
        for v in verts:
            if v not in index_of:
                index_of[v] = len(verts_list)
                verts_list.append(v)
            face_idxs.append(index_of[v])
        faces_list.append((face_idxs[0], face_idxs[1], face_idxs[2]))

    vertices = np.array(verts_list, dtype=np.float64)
    faces = np.array(faces_list, dtype=np.int32)

    mesh = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)

    outline_edges: set[tuple[int, int]] = set()
    faces_in_facets: set[int] = set()

    for face_indices in mesh.facets:
        faces_in_facets.update(face_indices.tolist())
        edge_count: dict[tuple[int, int], int] = {}
        for fi in face_indices:
            a, b, c = int(faces[fi][0]), int(faces[fi][1]), int(faces[fi][2])
            for edge in (
                (min(a, b), max(a, b)),
                (min(b, c), max(b, c)),
                (min(c, a), max(c, a)),
            ):
                edge_count[edge] = edge_count.get(edge, 0) + 1
        for edge, count in edge_count.items():
            if count == 1:
                outline_edges.add(edge)

    for fi, (a, b, c) in enumerate(faces):
        if fi not in faces_in_facets:
            a, b, c = int(a), int(b), int(c)
            outline_edges.add((min(a, b), max(a, b)))
            outline_edges.add((min(b, c), max(b, c)))
            outline_edges.add((min(c, a), max(c, a)))

    fmt = GeomVertexFormat.get_v3()
    vdata = GeomVertexData(name, fmt, Geom.UH_static)
    vdata.set_num_rows(len(verts_list))
    vwriter = GeomVertexWriter(vdata, "vertex")
    for pos in verts_list:
        vwriter.add_data3(*pos)

    prim = GeomLines(Geom.UH_static)
    for a, b in outline_edges:
        prim.add_vertices(a, b)
    prim.close_primitive()

    geom = Geom(vdata)
    geom.add_primitive(prim)
    node = GeomNode(name)
    node.add_geom(geom)
    return node


# --------------------------------------------------------------------------- #
# Panda3D native loading (non-STL formats)
# --------------------------------------------------------------------------- #
def load_panda_model(loader, path: Path) -> NodePath:
    """Load a non-STL 3D file with Panda3D's own loader.

    Panda3D loads ``.egg`` and ``.bam`` natively and, through the bundled Assimp
    plugin, common formats such as ``.obj``, ``.dae``, ``.fbx``, ``.ply`` and
    ``.3ds``. glTF (``.gltf`` / ``.glb``) is handled when the optional
    ``panda3d-gltf`` package is installed. Textures referenced by the model are
    resolved relative to the model's own directory and loaded automatically.
    """
    from panda3d.core import Filename, get_model_path

    suffix = path.suffix.lower()
    if suffix in (".gltf", ".glb"):
        try:
            import gltf

            gltf.patch_loader(loader)
        except ImportError:
            sys.exit(
                f"Loading {suffix} files needs the optional 'panda3d-gltf' package.\n"
                "    pip install panda3d-gltf"
            )

    # Resolve relative texture paths against the model's own directory.
    model_dir = Filename.from_os_specific(str(path.parent.resolve()))
    get_model_path().append_directory(model_dir)

    panda_path = Filename.from_os_specific(str(path.resolve()))
    model = loader.load_model(panda_path, noCache=True, okMissing=True)
    if model is None or model.is_empty():
        sys.exit(f"Panda3D could not load {str(path)!r} — unsupported or corrupt file?")

    textures = model.find_all_textures()
    if textures:
        names = ", ".join(t.get_name() or "<unnamed>" for t in textures)
        print(f"Loaded {textures.get_num_textures()} texture(s) from {path}: {names}")
    else:
        print(f"No embedded textures found in {path}")
    return model


# --------------------------------------------------------------------------- #
# Application
# --------------------------------------------------------------------------- #
class STLViewer(ShowBase):
    def __init__(
        self,
        stl_path: Path,
        speed: float,
        color: tuple[float, float, float],
        wireframe: bool,
        edge_color: tuple[float, float, float],
        bg_color: tuple[float, float, float, float],
        output: str | None = None,
        fps: int = 30,
        width: int = 1920,
        height: int = 1080,
        target_count: int = 0,
    ) -> None:
        self._offline = output is not None

        if bg_color[3] < 1.0 or self._offline:
            loadPrcFileData("", "framebuffer-alpha true")
        if self._offline:
            loadPrcFileData("", "window-type offscreen")
            loadPrcFileData("", f"win-size {width} {height}")

        super().__init__()

        self.set_background_color(*bg_color)
        self.speed = speed
        self.paused = False

        # STL has no Panda3D loader, so parse it ourselves. Every other format
        # goes through Panda3D's loader (Assimp / egg / bam / glTF), which also
        # pulls in any referenced textures.
        is_stl = stl_path.suffix.lower() == ".stl"
        triangles: list[Triangle] | None = None
        has_texture = False

        if is_stl:
            triangles = load_stl(stl_path)
            if not triangles:
                sys.exit(f"No triangles found in {stl_path!r} — is it a valid STL?")
            print(f"Loaded {len(triangles)} triangles from {stl_path}")

            if target_count > 0 and target_count < len(triangles):
                triangles = simplify_triangles(triangles, target_count)
                print(f"Simplified to {len(triangles)} triangles")

            node = stl_to_geomnode(triangles, name=str(stl_path))
            self.model = self.render.attach_new_node(node)
        else:
            if target_count > 0:
                print("Note: --target-count only applies to STL files; ignoring.")
            self.model = load_panda_model(self.loader, stl_path)
            self.model.reparent_to(self.render)
            has_texture = self.model.find_all_textures().get_num_textures() > 0

        self._center_and_scale(self.model)

        self.disable_mouse()
        self.camera.set_pos(0, -8, 1.5)
        self.camera.look_at(0, 0, 0)

        self.pivot = self.render.attach_new_node("pivot")
        self.model.reparent_to(self.pivot)

        if wireframe:
            if triangles is None:
                sys.exit("--wireframe is only supported for STL files.")
            outline_node = build_facet_outline_geomnode(triangles, name=str(stl_path) + "_outline")
            self.outline = self.render.attach_new_node(outline_node)
            self._center_and_scale(self.outline)
            self.outline.set_color(Vec4(*edge_color, 1))
            self.outline.set_render_mode_thickness(2)
            self.outline.reparent_to(self.pivot)
            self.model.hide()
        else:
            self._setup_lights()
            # Keep the model's own textures/materials; only tint untextured
            # models with the requested flat color.
            if has_texture:
                self.model.set_shader_auto()
            else:
                self.model.set_color(Vec4(*color, 1))

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
            codec_args = ["-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p", "-b:v", "0", "-crf", "30"]
        elif ext == ".mov":
            codec_args = ["-c:v", "prores_ks", "-profile:v", "4", "-pix_fmt", "yuva444p10le"]
        else:
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


app = typer.Typer(help="Spin a 3D model with optional wireframe rendering.")


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
    stl: Annotated[
        Path,
        typer.Argument(
            help="path to the model file (.stl, .obj, .gltf, .glb, .egg, .bam, .dae, .fbx, .ply, …)"
        ),
    ],
    speed: Annotated[
        float, typer.Option(help="rotation speed in degrees/second (negative reverses)")
    ] = 45.0,
    color: Annotated[
        str, typer.Option(help="base RGB color as hex, e.g. ff0000 or #ff6600")
    ] = "ff0000",
    wireframe: Annotated[
        bool,
        typer.Option("--wireframe/--no-wireframe", help="render edges only with transparent faces"),
    ] = False,
    edge_color: Annotated[
        str, typer.Option(help="edge color as hex when --wireframe is active, e.g. 000000")
    ] = "000000",
    bg_color: Annotated[
        Optional[str],
        typer.Option(help="background color as hex, e.g. 1a1a2e (default: transparent)"),
    ] = None,
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
    target_count: Annotated[
        int, typer.Option(help="simplify mesh to this many triangles (0 = no simplification)")
    ] = 0,
) -> None:
    bg = (*_parse_hex_color(bg_color), 1.0) if bg_color else (0.0, 0.0, 0.0, 0.0)
    viewer = STLViewer(
        stl,
        speed,
        _parse_hex_color(color),
        wireframe,
        _parse_hex_color(edge_color),
        bg,
        output=output,
        fps=fps,
        width=width,
        height=height,
        target_count=target_count,
    )
    viewer.run()
