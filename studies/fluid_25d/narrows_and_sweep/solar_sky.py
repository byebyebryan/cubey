"""Capture shared solar-clock sky plates from the native Atmosphere app."""

from __future__ import annotations

from collections.abc import Callable, Mapping
from pathlib import Path
import subprocess


SOLAR_HOURS = (14.0, 15.5, 17.0)

_CAMERA_YAW_OFFSET_DEG = "54.463104"
_CAMERA_PITCH_OFFSET_DEG = "-46.124506"
_CAMERA_ALTITUDE_KM = "0.18"


def capture_solar_sky(
    output: Path,
    atmosphere: Path,
    width: int,
    height: int,
    environment: Mapping[str, str],
    run: Callable[..., object],
) -> list[Path]:
    """Bake the ordered 14:00, 15:30, and 17:00 RGB24 sky slabs in ``output``.

    ``output`` must already exist. The injected ``run`` is the capture driver's
    command runner so the native app and FFmpeg invocations share its logging.
    """
    output = Path(output)
    atmosphere = Path(atmosphere)
    if not output.is_dir():
        raise ValueError(f"Sky output directory does not exist: {output}")
    if width <= 0 or height <= 0:
        raise ValueError("Sky dimensions must be positive")

    log_path = output / "sky.log"
    slabs: list[Path] = []
    ffmpeg = [
        "ffmpeg",
        "-hide_banner",
        "-nostdin",
        "-v",
        "error",
        "-y",
    ]
    expected_bytes = width * height * 3

    with log_path.open("wb") as log:
        for index, solar_hour in enumerate(SOLAR_HOURS):
            png = output / f"sky-{index}.png"
            slab = output / f"sky-{index}.rgb"
            atmosphere_command: list[str | Path | int] = [
                atmosphere,
                "--headless",
                "--frames",
                "1",
                "--width",
                width,
                "--height",
                height,
                "--time-of-day-mode",
                "solar",
                "--time-hours",
                f"{solar_hour:.1f}",
                "--camera-altitude-km",
                _CAMERA_ALTITUDE_KM,
                "--camera-yaw-offset-deg",
                _CAMERA_YAW_OFFSET_DEG,
                "--camera-pitch-offset-deg",
                _CAMERA_PITCH_OFFSET_DEG,
                "--no-reference-geometry",
                "--no-clouds",
                "--atmosphere-ground-mode",
                "sky-only-no-ground-occlusion",
                "--output",
                png,
            ]
            run(
                atmosphere_command,
                env=environment,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
            run(
                [
                    *ffmpeg,
                    "-i",
                    png,
                    "-frames:v",
                    "1",
                    "-f",
                    "rawvideo",
                    "-pix_fmt",
                    "rgb24",
                    slab,
                ],
                env=environment,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
            actual_bytes = slab.stat().st_size
            if actual_bytes != expected_bytes:
                raise RuntimeError(
                    f"Sky slab has {actual_bytes} bytes; expected {expected_bytes}: {slab}"
                )
            slabs.append(slab)

    return slabs
