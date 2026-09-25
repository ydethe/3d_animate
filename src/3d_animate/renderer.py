import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from direct.showbase.ShowBase import ShowBase
from panda3d.core import (
    AmbientLight,
    AsyncTask,
    DirectionalLight,
    FrameBufferProperties,
    GraphicsPipe,
    NodePath,
    Vec4,
    WindowProperties,
    loadPrcFileData,
)
from rich.progress import BarColumn, Progress, TextColumn, TimeRemainingColumn

from . import console, logger
from .utils import (
    build_facet_outline_geomnode,
    load_panda_model,
    load_stl,
    nodepath_to_triangles,
    repair_stl_mesh,
    simplify_stl_mesh,
    stl_to_geomnode,
)

# Quiet Panda3D's own notify chatter; only surface warnings and errors.
loadPrcFileData("", "notify-level warning")
loadPrcFileData("", "default-directnotify-level warning")
# No audio is used, so skip OpenAL/ALSA init and the device-error spam it prints.
loadPrcFileData("", "audio-library-name null")

Vec3 = tuple[float, float, float]
Triangle = tuple[Vec3, list[Vec3]]


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
        simplify: float | None = None,
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
            logger.info("Loaded %d triangles from '%s'", len(triangles), stl_path)

            triangles = repair_stl_mesh(triangles)
            logger.info("Repaired mesh (winding, inversion, normals)")

            if simplify is not None:
                triangles = simplify_stl_mesh(triangles, simplify)

            node = stl_to_geomnode(triangles, name=str(stl_path))
            self.model = self.render.attach_new_node(node)
        else:
            self.model = load_panda_model(self.loader, stl_path)
            self.model.reparent_to(self.render)
            has_texture = self.model.find_all_textures().get_num_textures() > 0
            # Wireframe needs raw triangles; Panda3D's loader only hands back an
            # opaque scene graph, so read the geometry back out of the model.
            if wireframe:
                triangles = nodepath_to_triangles(self.model)
                logger.info(
                    "Extracted %d triangles from %s for wireframe", len(triangles), stl_path
                )

        self._center_and_scale(self.model)

        self.disable_mouse()
        self.camera.set_pos(0, -8, 1.5)  # type: ignore
        self.camera.look_at(0, 0, 0)  # type: ignore

        self.pivot = self.render.attach_new_node("pivot")
        self.model.reparent_to(self.pivot)

        if wireframe:
            if triangles is None:
                sys.exit("--wireframe could not obtain mesh geometry for this file.")
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
            self._capture_win = self._make_capture_buffer(width, height, bg_color)
            self._output_path = output
            self._fps = fps
            duration = 360.0 / abs(speed)
            self._total_frames = max(1, round(duration * fps))
            self._frame_idx = 0
            self._temp_dir = tempfile.mkdtemp(prefix="stl_video_")
            logger.info(
                "Rendering %d frames at %d fps (%.2fs for 360°)…",
                self._total_frames,
                fps,
                duration,
            )
            self._progress = Progress(
                TextColumn("[progress.description]{task.description}"),
                BarColumn(),
                TextColumn("{task.completed}/{task.total} frames"),
                TimeRemainingColumn(),
                console=console,
            )
            self._progress.start()
            self._progress_task = self._progress.add_task("Capturing", total=self._total_frames)
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
        logger.info("Paused" if self.paused else "Resumed")

    def _change_speed(self, delta: float) -> None:
        self.speed += delta
        logger.info("Speed: %.0f deg/s", self.speed)

    def _print_help(self) -> None:
        logger.info("Controls:  +/-  speed    space  pause    esc  quit")
        logger.info("Rotation speed: %.0f deg/s", self.speed)

    # -- Offscreen video capture --

    def _make_capture_buffer(
        self, width: int, height: int, bg_color: tuple[float, float, float, float]
    ):
        """Render captures into a dedicated buffer with a guaranteed alpha channel.

        The main window's framebuffer is derived from whatever visual the host
        display offers, which frequently lacks alpha bits even with
        ``framebuffer-alpha true`` set — so its screenshots come out opaque. An
        explicit off-screen buffer that *requires* 8 alpha bits keeps the
        transparent background intact regardless of the host's default visual.
        """
        fb_props = FrameBufferProperties()
        fb_props.set_rgb_color(True)
        fb_props.set_rgba_bits(8, 8, 8, 8)
        fb_props.set_depth_bits(24)
        win_props = WindowProperties.size(width, height)

        buffer = self.graphicsEngine.make_output(
            self.pipe,
            "capture_buffer",
            -100,
            fb_props,
            win_props,
            GraphicsPipe.BFRefuseWindow,
            self.win.get_gsg(),
            self.win,
        )
        if buffer is None:
            logger.warning(
                "Could not create an alpha-capable capture buffer;"
                " falling back to the main window (background may be opaque)."
            )
            return None

        got_alpha = buffer.get_fb_properties().get_alpha_bits()
        logger.debug("Capture buffer alpha bits: %d", got_alpha)
        if got_alpha < 1:
            logger.warning("Capture buffer has no alpha channel; output will be opaque.")

        buffer.set_clear_color(Vec4(*bg_color))
        buffer.set_clear_color_active(True)
        display_region = buffer.make_display_region(0, 1, 0, 1)
        display_region.set_camera(self.cam)
        return buffer

    def _capture_task(self, task: AsyncTask) -> int:
        if self._frame_idx >= self._total_frames:
            self._progress.stop()
            self._encode_video()
            sys.exit(0)

        angle = self._frame_idx * self.speed * (1.0 / self._fps)
        self.pivot.set_h(angle)

        self.graphicsEngine.render_frame()
        frame_path = os.path.join(self._temp_dir, f"frame_{self._frame_idx:06d}.png")
        capture_win = self._capture_win or self.win
        capture_win.save_screenshot(frame_path)  # type: ignore

        self._frame_idx += 1
        self._progress.update(self._progress_task, completed=self._frame_idx)
        return task.cont

    def _encode_video(self) -> None:
        ext = os.path.splitext(self._output_path)[1].lower()  # type: ignore
        frame_pattern = os.path.join(self._temp_dir, "frame_%06d.png")

        if ext == ".webm":
            codec_args = ["-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p", "-b:v", "0", "-crf", "30"]
        elif ext == ".mov":
            codec_args = ["-c:v", "prores_ks", "-profile:v", "4", "-pix_fmt", "yuva444p10le"]
        else:
            if ext == ".mp4":
                logger.warning(
                    "H.264 inside MP4 does not support alpha."
                    " Use .webm or .mov for a transparent output."
                )
            codec_args = ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18"]

        cmd = [
            "ffmpeg",
            "-y",
            "-hide_banner",
            "-loglevel",
            "error",
            "-nostats",
            "-framerate",
            str(self._fps),
            "-i",
            frame_pattern,
            *codec_args,
            self._output_path,
        ]
        logger.info("Encoding %s…", self._output_path)
        logger.debug("ffmpeg command: %s", " ".join(cmd))
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            # ffmpeg was silenced above, so surface whatever it did emit on failure.
            logger.error("ffmpeg failed (exit %d):\n%s", result.returncode, result.stderr.strip())
            shutil.rmtree(self._temp_dir, ignore_errors=True)
            raise subprocess.CalledProcessError(
                result.returncode, cmd, result.stdout, result.stderr
            )
        logger.info("Saved %s", self._output_path)
        shutil.rmtree(self._temp_dir, ignore_errors=True)
