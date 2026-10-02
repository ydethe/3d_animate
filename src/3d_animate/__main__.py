from enum import Enum
from pathlib import Path
from typing import Annotated

import typer

from .renderer import STLViewer
from .utils import _parse_hex_color

app = typer.Typer(help="Spin a 3D model with optional wireframe rendering.")


class UpAxis(str, Enum):
    # <axis><sign>: the axis to bring up to Z, and the sign of the rotation
    # (p = +90°, m = −90°; zp = no rotation, zm = 180° flip).
    xp = "xp"
    xm = "xm"
    yp = "yp"
    ym = "ym"
    zp = "zp"
    zm = "zm"


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
        str | None,
        typer.Option(help="background color as hex, e.g. 1a1a2e (default: transparent)"),
    ] = None,
    output: Annotated[
        Path | None,
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
    simplify: Annotated[
        float | None,
        typer.Option(
            help=(
                "reduce the mesh to this fraction of its original face count before rendering,"
                " e.g. 0.5 for 50%% (STL only, requires pymeshlab)"
            ),
            min=0.0,
            max=1.0,
        ),
    ] = None,
    up_axis: Annotated[
        UpAxis,
        typer.Option(
            case_sensitive=False,
            help=(
                "the model's up axis and rotation sign (p=+90°, m=-90°); the model is rotated so"
                " the result is Z-up and spins around Z"
            ),
        ),
    ] = UpAxis.zp,
) -> None:
    if simplify is not None and not (0.0 < simplify <= 1.0):
        raise typer.BadParameter("--simplify must be in the range (0, 1]", param_hint="--simplify")

    if output is not None and not output.parent.exists():
        output.parent.mkdir(parents=True, exist_ok=True)

    bg = (*_parse_hex_color(bg_color), 1.0) if bg_color else (0.0, 0.0, 0.0, 0.0)
    viewer = STLViewer(
        stl,
        speed,
        _parse_hex_color(color),
        wireframe,
        _parse_hex_color(edge_color),
        bg,
        output=str(output),
        fps=fps,
        width=width,
        height=height,
        simplify=simplify,
        up_axis=up_axis.value,
    )
    viewer.run()


if __name__ == "__main__":
    app()
