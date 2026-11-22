#!/usr/bin/env python3
"""Measure the maintained Narrows and Sweep simulation trace."""

import argparse
from array import array
import csv
from collections import deque
import json
import math
from pathlib import Path
import sys


DISPLAY_WIDTH = 256
DISPLAY_HEIGHT = 96
FIELD_PLANE = DISPLAY_WIDTH * DISPLAY_HEIGHT
FIELD_SHAPE = (361, 3, DISPLAY_HEIGHT, DISPLAY_WIDTH)
CROP_WIDTH = 194
CROP_HEIGHT = 80
CROP_X = 25
CROP_Y = 8
STATIONS = (53, 95, 123, 168, 187, 198, 210, 218)


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def read_float_rows(path: Path, fields: tuple[str, ...]) -> list[dict[str, float]]:
    rows = read_rows(path)
    return [{field: float(row[field]) for field in fields} for row in rows]


def connected_outlet_width(frame: list[float], seed_row: int) -> int:
    """Count outlet cells in the four-connected component seeded at the inlet."""
    count = CROP_WIDTH * CROP_HEIGHT
    if len(frame) != count:
        raise ValueError("unexpected displayed depth crop size")
    seed = seed_row * CROP_WIDTH
    if not 0 <= seed_row < CROP_HEIGHT or frame[seed] < 0.25:
        raise ValueError("displayed channel is disconnected from the narrows")

    visited = bytearray(count)
    visited[seed] = 1
    pending = deque([seed])
    outlet_width = 0
    while pending:
        index = pending.popleft()
        y, x = divmod(index, CROP_WIDTH)
        if x == CROP_WIDTH - 1:
            outlet_width += 1
        neighbours = []
        if x > 0:
            neighbours.append(index - 1)
        if x + 1 < CROP_WIDTH:
            neighbours.append(index + 1)
        if y > 0:
            neighbours.append(index - CROP_WIDTH)
        if y + 1 < CROP_HEIGHT:
            neighbours.append(index + CROP_WIDTH)
        for neighbour in neighbours:
            if not visited[neighbour] and frame[neighbour] >= 0.25:
                visited[neighbour] = 1
                pending.append(neighbour)
    return outlet_width


def interpolate_crop(previous: array, current: array, mix: float) -> list[float]:
    previous_row = CROP_Y * DISPLAY_WIDTH + CROP_X
    result = []
    for y in range(CROP_HEIGHT):
        offset = previous_row + y * DISPLAY_WIDTH
        for x in range(CROP_WIDTH):
            before = float(previous[offset + x])
            after = float(current[offset + x])
            result.append((1.0 - mix) * before + mix * after)
    return result


def measure(trace_dir: Path) -> dict:
    if sys.byteorder != "little":
        raise RuntimeError("fields.f32 is little-endian; this script requires a little-endian host")

    plane_bytes = FIELD_PLANE * 4
    frame_bytes = 3 * plane_bytes
    station_rows = {}
    section_rows = read_float_rows(
        trace_dir / "sections.csv",
        ("x", "center", "near_bank_y", "far_bank_y"),
    )
    if len(section_rows) != DISPLAY_WIDTH:
        raise ValueError(f"expected 256 one-metre section rows, found {len(section_rows)}")
    for x in STATIONS:
        station_rows[x] = round(section_rows[x]["center"])

    seed_row = round(section_rows[CROP_X]["center"]) - CROP_Y
    connected_widths = []
    display_fringe_peak = 0.0
    display_area_over_10cm = 0
    station_depths = {x: [] for x in STATIONS}
    display_frame = 0
    previous_depth = None
    fields_path = trace_dir / "fields.f32"

    with fields_path.open("rb") as stream:
        for snapshot in range(FIELD_SHAPE[0]):
            payload = stream.read(frame_bytes)
            if len(payload) != frame_bytes:
                raise ValueError(f"fields.f32 ends before snapshot {snapshot}")
            values = array("f")
            values.frombytes(payload)
            if len(values) != 3 * FIELD_PLANE:
                raise ValueError(f"snapshot {snapshot} has an invalid field payload")

            depth = values[FIELD_PLANE : 2 * FIELD_PLANE]
            for index, value in enumerate(values):
                if not math.isfinite(value):
                    raise ValueError(f"fields.f32 contains a non-finite value in snapshot {snapshot}")
                if FIELD_PLANE <= index < 2 * FIELD_PLANE and value < 0.0:
                    raise ValueError(f"fields.f32 contains negative depth in snapshot {snapshot}")

            for x in STATIONS:
                station_depths[x].append(float(depth[station_rows[x] * DISPLAY_WIDTH + x]))

            if previous_depth is not None:
                while display_frame < 360:
                    display_seconds = display_frame * 0.18
                    step = int(display_seconds / 0.25)
                    if step != snapshot - 1:
                        break
                    mix = display_seconds / 0.25 - step
                    frame = interpolate_crop(previous_depth, depth, mix)
                    connected_widths.append(connected_outlet_width(frame, seed_row))

                    fringe_cells = 0
                    for y in range(CROP_HEIGHT):
                        global_y = y + CROP_Y
                        for x in range(CROP_WIDTH):
                            global_x = x + CROP_X
                            if global_y <= section_rows[global_x]["near_bank_y"] or global_y >= section_rows[global_x]["far_bank_y"]:
                                value = frame[y * CROP_WIDTH + x]
                                display_fringe_peak = max(display_fringe_peak, value)
                                if value > 0.10:
                                    fringe_cells += 1
                    display_area_over_10cm = max(display_area_over_10cm, fringe_cells)
                    display_frame += 1

            previous_depth = depth

        if stream.read(1):
            raise ValueError("fields.f32 has trailing bytes after the expected snapshots")

    if display_frame != 360:
        raise ValueError(f"expected 360 display samples, measured {display_frame}")

    snapshots = [frame * 0.25 for frame in range(FIELD_SHAPE[0])]
    display_seconds = [frame * 0.18 for frame in range(360)]
    stations = {}
    for x in STATIONS:
        sampled_depth = []
        source = station_depths[x]
        for seconds in display_seconds:
            step = int(seconds / 0.25)
            mix = seconds / 0.25 - step
            sampled_depth.append((1.0 - mix) * source[step] + mix * source[step + 1])
        initial = sampled_depth[0]
        rise = [value - initial for value in sampled_depth]
        peak_frame = max(range(len(rise)), key=rise.__getitem__)
        first_20 = next((i for i in range(peak_frame + 1) if rise[i] >= 0.20), None)
        first_40 = next((i for i in range(peak_frame + 1) if rise[i] >= 0.40), None)
        stations[str(x)] = {
            "x_m": x,
            "peak_rise_m": round(rise[peak_frame], 5),
            "peak_display_frame_30fps": peak_frame,
            "peak_playback_time_s": round(peak_frame / 30.0, 3),
            "peak_simulation_time_s": round(display_seconds[peak_frame], 3),
            "duration_above_30_cm_playback_s": sum(value >= 0.30 for value in rise) / 30.0,
            "end_rise_m": round(rise[-1], 5),
            "rise_20_to_40_playback_s": (
                (first_40 - first_20) / 30.0
                if first_20 is not None and first_40 is not None
                else None
            ),
        }

    ledger = read_float_rows(
        trace_dir / "ledger.csv",
        ("seconds", "water_volume_m3", "water_in_m3", "water_out_m3", "cfl"),
    )
    if len(ledger) != len(snapshots):
        raise ValueError(f"expected 361 water-ledger rows, found {len(ledger)}")
    volumes = [row["water_volume_m3"] for row in ledger]
    residuals = [
        volumes[i] - volumes[0] - ledger[i]["water_in_m3"] + ledger[i]["water_out_m3"]
        for i in range(len(ledger))
    ]
    max_residual = max(abs(value) for value in residuals)
    max_cfl = max(row["cfl"] for row in ledger)

    fringe_rows = read_float_rows(
        trace_dir / "full-grid-fringe.csv",
        ("seconds", "peak_depth_m", "area_over_10cm_m2"),
    )
    if len(fringe_rows) != len(snapshots):
        raise ValueError(f"expected 361 full-grid fringe rows, found {len(fringe_rows)}")
    fringe_time_error = max(
        abs(row["seconds"] - snapshots[i]) for i, row in enumerate(fringe_rows)
    )
    full_grid_peak = max(row["peak_depth_m"] for row in fringe_rows)
    full_grid_area = max(row["area_over_10cm_m2"] for row in fringe_rows)
    min_connected_width = min(connected_widths)

    checks = {
        "fields_finite": True,
        "water_depth_nonnegative": True,
        "ledger_mass_residual_below_0_0001_m3": max_residual < 0.0001,
        "max_cfl_below_0_45": max_cfl < 0.45,
        "connected_outlet_at_least_11m_every_playback_frame": min_connected_width >= 11,
        "narrows_x95_peak_near_0_937m": abs(stations["95"]["peak_rise_m"] - 0.937) <= 0.003,
        "outlet_x218_peak_near_0_698m": abs(stations["218"]["peak_rise_m"] - 0.698) <= 0.003,
        "full_grid_fringe_rows_match_all_snapshots": fringe_time_error <= 0.000001,
        "full_grid_peak_bank_fringe_near_0_0839023m": abs(full_grid_peak - 0.0839023) <= 0.000001,
        "full_grid_area_over_10cm_is_zero": full_grid_area == 0.0,
    }
    return {
        "schema": "cubey-fluid25d-narrows-and-sweep-metrics-v1",
        "snapshots": len(snapshots),
        "display_samples": display_frame,
        "stations": stations,
        "outlet_connectivity": {
            "depth_threshold_m": 0.25,
            "playback_frames_checked": len(connected_widths),
            "minimum_connected_width_m": min_connected_width,
            "minimum_playback_frame_30fps": connected_widths.index(min_connected_width),
        },
        "display_bank_fringe": {
            "peak_depth_m": display_fringe_peak,
            "max_area_over_10cm_m2": display_area_over_10cm,
        },
        "full_grid_bank_fringe": {
            "snapshots_checked": len(fringe_rows),
            "peak_depth_m": full_grid_peak,
            "max_area_over_10cm_m2": full_grid_area,
        },
        "water_ledger": {
            "max_absolute_residual_m3": max_residual,
            "max_cfl": max_cfl,
        },
        "checks": checks,
        "accepted": all(checks.values()),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace_dir", type=Path, help="directory containing the four trace outputs")
    args = parser.parse_args()
    result = measure(args.trace_dir)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
