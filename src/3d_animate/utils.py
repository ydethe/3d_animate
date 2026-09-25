import struct
import sys
from pathlib import Path

import numpy as np
import typer
from panda3d.core import (
    Geom,
    GeomLines,
    GeomNode,
    GeomTriangles,
    GeomVertexData,
    GeomVertexFormat,
    GeomVertexWriter,
    LVector3,
    NodePath,
)

from . import logger

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
    return header.lstrip()[:5].lower() != b"solid"


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


def repair_stl_mesh(triangles: list[Triangle]) -> list[Triangle]:
    """Fix winding, inversion and normals of parsed STL triangles via pymeshlab.

    STL files frequently ship with inconsistent face winding or inward-facing
    normals, which makes flat shading look blotchy. We weld the raw triangles
    into a pymeshlab mesh, let it repair the topology, and hand back triangles
    whose per-face normals point consistently outward.
    """
    import pymeshlab  # type: ignore

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

    m = pymeshlab.Mesh(  # type: ignore
        vertex_matrix=np.array(verts_list, dtype=np.float64),
        face_matrix=np.array(faces_list, dtype=np.int32),
    )
    ms = pymeshlab.MeshSet()  # type: ignore
    ms.add_mesh(m)  # type: ignore

    # Remove duplicate/degenerate faces and repair non-manifold topology before
    # attempting orientation, which requires a 2-manifold mesh.
    ms.meshing_remove_duplicate_faces()  # type: ignore
    ms.meshing_remove_unreferenced_vertices()  # type: ignore
    try:
        ms.meshing_repair_non_manifold_edges()  # type: ignore
    except Exception as e:
        logger.warning("Could not repair non-manifold edges (%s); skipping step", e)

    try:
        ms.meshing_repair_non_manifold_vertices()  # type: ignore
    except Exception as e:
        logger.warning("Could not repair non-manifold vertices (%s); skipping step", e)

    # Make winding consistent across connected components, then orient faces
    # outward by geometry (convexity heuristic), and recompute per-face normals.
    try:
        ms.meshing_re_orient_faces_coherently()
    except Exception as exc:
        logger.warning("Could not re-orient faces coherently (%s); skipping step", exc)

    ms.meshing_re_orient_faces_by_geometry()  # type: ignore
    ms.compute_normal_per_face()  # type: ignore

    out = ms.current_mesh()
    mesh_verts = out.vertex_matrix()
    mesh_faces = out.face_matrix()
    mesh_normals = out.face_normal_matrix()

    repaired: list[Triangle] = []
    for fi in range(len(mesh_faces)):
        a, b, c = int(mesh_faces[fi][0]), int(mesh_faces[fi][1]), int(mesh_faces[fi][2])
        n = mesh_normals[fi]
        normal: Vec3 = (float(n[0]), float(n[1]), float(n[2]))
        tri_verts: list[Vec3] = [
            (float(mesh_verts[a][0]), float(mesh_verts[a][1]), float(mesh_verts[a][2])),
            (float(mesh_verts[b][0]), float(mesh_verts[b][1]), float(mesh_verts[b][2])),
            (float(mesh_verts[c][0]), float(mesh_verts[c][1]), float(mesh_verts[c][2])),
        ]
        repaired.append((normal, tri_verts))
    return repaired


def simplify_stl_mesh(triangles: list[Triangle], ratio: float) -> list[Triangle]:
    """Decimate the mesh to *ratio* of its original face count using trimesh.

    *ratio* must be in (0, 1]. Values close to 1 preserve nearly all faces;
    values close to 0 produce a very coarse approximation.
    """
    import trimesh  # type: ignore

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

    target = max(1, round(len(faces_list) * ratio))
    logger.info("Simplifying mesh: %d → ~%d faces (ratio %.2f)", len(faces_list), target, ratio)

    mesh = trimesh.Trimesh(
        vertices=np.array(verts_list, dtype=np.float64),
        faces=np.array(faces_list, dtype=np.int32),
        process=False,
    )
    simplified = mesh.simplify_quadric_decimation(target)

    mesh_verts = np.asarray(simplified.vertices)
    mesh_faces = np.asarray(simplified.faces)
    mesh_normals = np.asarray(simplified.face_normals)

    result: list[Triangle] = []
    for fi in range(len(mesh_faces)):
        a, b, c = int(mesh_faces[fi][0]), int(mesh_faces[fi][1]), int(mesh_faces[fi][2])
        n = mesh_normals[fi]
        normal: Vec3 = (float(n[0]), float(n[1]), float(n[2]))
        tri_verts: list[Vec3] = [
            (float(mesh_verts[a][0]), float(mesh_verts[a][1]), float(mesh_verts[a][2])),
            (float(mesh_verts[b][0]), float(mesh_verts[b][1]), float(mesh_verts[b][2])),
            (float(mesh_verts[c][0]), float(mesh_verts[c][1]), float(mesh_verts[c][2])),
        ]
        result.append((normal, tri_verts))

    logger.info("Simplified to %d faces", len(result))
    return result


def nodepath_to_triangles(model: NodePath) -> list[Triangle]:
    """Extract world-space triangles from an already-loaded Panda3D model.

    Used to obtain the raw geometry needed for wireframe rendering of non-STL
    formats (``.obj``, ``.fbx``, ``.gltf`` …). Panda3D's loader (via its bundled
    Assimp plugin) hands back an opaque scene graph rather than vertex arrays, so
    we walk every ``GeomNode``, decompose its primitives into triangles and read
    back the vertex positions, baking in each node's transform relative to
    *model*. The per-triangle normal is left zeroed — the facet-outline builder
    recomputes connectivity from positions and ignores it.
    """
    from panda3d.core import GeomVertexReader

    zero_normal: Vec3 = (0.0, 0.0, 0.0)
    triangles: list[Triangle] = []

    for np_geom in model.find_all_matches("**/+GeomNode"):
        geom_node = np_geom.node()
        transform = np_geom.get_transform(model).get_mat()
        for gi in range(geom_node.get_num_geoms()):
            geom = geom_node.get_geom(gi)
            vdata = geom.get_vertex_data()
            reader = GeomVertexReader(vdata, "vertex")
            for pi in range(geom.get_num_primitives()):
                prim = geom.get_primitive(pi).decompose()
                indices = prim.get_vertex_list()
                for k in range(0, len(indices) - 2, 3):
                    verts: list[Vec3] = []
                    for j in range(3):
                        reader.set_row(indices[k + j])
                        point = transform.xform_point(reader.get_data3())
                        verts.append((point[0], point[1], point[2]))
                    triangles.append((zero_normal, verts))

    if not triangles:
        sys.exit("No triangle geometry found in the loaded model for wireframe rendering.")
    return triangles


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

            gltf.patch_loader(loader)  # type: ignore
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
        logger.info("Loaded %d texture(s) from %s: %s", textures.get_num_textures(), path, names)
    else:
        logger.info("No embedded textures found in %s", path)
    return model


def _parse_hex_color(value: str) -> tuple[float, float, float]:
    hex_str = value.lstrip("#")
    if len(hex_str) != 6:
        raise typer.BadParameter("Color must be a 6-digit hex string, e.g. ff6600 or #ff6600")
    try:
        r, g, b = int(hex_str[0:2], 16), int(hex_str[2:4], 16), int(hex_str[4:6], 16)
    except ValueError:
        raise typer.BadParameter("Invalid hex color: " + value)
    return r / 255.0, g / 255.0, b / 255.0
