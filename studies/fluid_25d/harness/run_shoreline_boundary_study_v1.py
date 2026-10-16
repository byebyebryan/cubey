#!/usr/bin/env python3
"""Offline reference-algorithm study; optional dependencies, never solver code.

Requires numpy, scikit-image, vtk, shapely and matplotlib in an isolated venv.
The frozen protocol is supplied separately and written before candidate runs.
"""
from __future__ import annotations

import argparse
import hashlib
import html
import importlib.metadata
import json
import math
from pathlib import Path
import subprocess
import time

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import tri
from scipy.ndimage import map_coordinates
from shapely import contains_xy
from shapely.geometry import LineString, MultiLineString, Point, box
from shapely.ops import polygonize, unary_union
from skimage.measure import find_contours
from vtkmodules.vtkCommonCore import vtkDoubleArray, vtkIdList, vtkPoints
from vtkmodules.vtkCommonDataModel import vtkCellArray, vtkImageData, vtkPolyData
from vtkmodules.vtkFiltersCore import vtkConstrainedSmoothingFilter, vtkSurfaceNets2D
from vtkmodules.util.numpy_support import numpy_to_vtk, vtk_to_numpy

import run_native_presentation_v1 as reference
import run_native_shoreline_raster_v1 as existing

METHODS = ("marching-squares-vtk-constrained", "vtk-surface-nets-2d")
STUDY_FILES = (Path(__file__), Path(__file__).with_name("test_shoreline_boundary_study_v1.py"))
SOURCES = {
    "marching_squares": "https://scikit-image.org/docs/stable/api/skimage.measure.html#skimage.measure.find_contours",
    "constrained_smoothing": "https://vtk.org/doc/nightly/html/classvtkConstrainedSmoothingFilter.html",
    "surface_nets": "https://vtk.org/doc/nightly/html/classvtkSurfaceNets2D.html",
}


def digest_array(a: np.ndarray) -> str:
    return hashlib.sha256(np.ascontiguousarray(a).tobytes()).hexdigest()


def checkpoint(out: Path) -> dict:
    before = reference.runtime_identity()
    patch = reference.rtk_output("git", "diff", "--binary", "HEAD").stdout
    reference.write_text_exclusive(out / "checkpoint/prior.patch", patch)
    untracked = reference.rtk_output("git", "ls-files", "--others", "--exclude-standard").stdout.splitlines()
    own = {str(p.relative_to(reference.ROOT)) for p in STUDY_FILES}
    hashes = {p: reference.sha256_file(reference.ROOT / p) for p in untracked if p not in own}
    tracked = reference.rtk_output("git", "ls-files", "projects/fluid").stdout.splitlines()
    protected = {p: reference.sha256_file(reference.ROOT / p) for p in tracked}
    result = {"runtime": before, "prior_untracked": hashes, "protected_fluid_files": protected,
              "protocol_sha256": reference.sha256_file(out / "protocol.json"),
              "study_tools_sha256": {p.name: reference.sha256_file(p) for p in STUDY_FILES},
              "python_packages": {n: importlib.metadata.version(n) for n in
                                  ("numpy", "scipy", "scikit-image", "vtk", "shapely", "matplotlib")},
              "reference_sources": SOURCES}
    reference.write_json_exclusive(out / "checkpoint/identity.json", result)
    packages = subprocess.run(["rtk", "proxy", str(Path(__import__("sys").executable)),
                               "-m", "pip", "freeze"], check=True, capture_output=True, text=True)
    reference.write_text_exclusive(out / "checkpoint/requirements.txt", packages.stdout)
    return result


def fixtures() -> dict[str, np.ndarray]:
    result = {name: np.array(existing.fields(name)[1], dtype=float).reshape(existing.ROWS, existing.COLS)
              for name in existing.CASES}
    y, x = np.mgrid[:existing.ROWS, :existing.COLS]
    result["diagonal-stream"] = np.where((abs(x - (0.8 * y + 7)) < 1.2) &
                                         (y >= 3) & (y <= 16), 0.08, 0.001)
    result["diagonal-separated"] = np.full(x.shape, .001)
    result["diagonal-separated"][9,10] = .08
    result["diagonal-separated"][10,11] = .08
    # Deliberately sampled smooth analytic boundaries; synthetic tests, not demos.
    center = 15.5 + 4 * np.sin((y - 3) / 13 * np.pi)
    result["curved-stream"] = np.where((abs(x-center) < 1.7) & (y >= 3) & (y <= 16), 0.08, 0.001)
    radius = np.hypot(x-15.5, y-9.5)
    result["island-ring"] = np.where((radius > 2.5) & (radius < 7), 0.08, 0.001)
    result["round-pool"] = np.where(radius < 6, 0.08, 0.001)
    result["map-edge"] = np.where((y >= 7) & (y <= 11) & (x <= 20), 0.08, 0.001)
    result["dry-gap"] = result["positive-film-gap"].copy()
    result["dry-gap"][result["dry-gap"] == 0.001] = 0
    return result


def polydata(curves: list[np.ndarray], distance: float) -> tuple[vtkPolyData, list[tuple[int, int, bool]]]:
    points, lines = vtkPoints(), vtkCellArray()
    points.SetDataTypeToDouble()
    constraints = vtkDoubleArray()
    constraints.SetName("SmoothingConstraints")
    spans = []
    for curve in curves:
        closed = bool(np.array_equal(curve[0], curve[-1]))
        values = curve[:-1] if closed else curve
        start = points.GetNumberOfPoints()
        for i, (x, y) in enumerate(values):
            points.InsertNextPoint(float(x), float(y), 0)
            constraints.InsertNextValue(distance if closed or 0 < i < len(values)-1 else 0)
        for i in range(len(values)-1 + int(closed)):
            lines.InsertNextCell(2)
            lines.InsertCellPoint(start+i)
            lines.InsertCellPoint(start+(i+1) % len(values))
        spans.append((start, len(values), closed))
    mesh = vtkPolyData()
    mesh.SetPoints(points)
    mesh.SetLines(lines)
    mesh.GetPointData().AddArray(constraints)
    return mesh, spans


def constrained(curves: list[np.ndarray], variant: dict, relaxation: float) -> list[np.ndarray]:
    if not variant["iterations"] or not curves:
        return [c.copy() for c in curves]
    mesh, spans = polydata(curves, variant["distance_cells"])
    smoother = vtkConstrainedSmoothingFilter()
    smoother.SetInputData(mesh)
    smoother.SetNumberOfIterations(variant["iterations"])
    smoother.SetRelaxationFactor(relaxation)
    smoother.SetConstraintDistance(variant["distance_cells"])
    smoother.SetConvergence(0)
    smoother.Update()
    points = vtk_to_numpy(smoother.GetOutput().GetPoints().GetData())[:, :2]
    result = []
    for start, count, closed in spans:
        c = points[start:start+count].copy()
        result.append(np.concatenate((c, c[:1])) if closed else c)
    return result


def nets_curves(h: np.ndarray, level: float) -> list[np.ndarray]:
    # Segment only for this label-map method. Marching squares retains scalar h.
    image = vtkImageData()
    image.SetDimensions(h.shape[1], h.shape[0], 1)
    scalars = numpy_to_vtk(np.ascontiguousarray(h > level, dtype=np.uint8).ravel(), deep=True)
    image.GetPointData().SetScalars(scalars)
    nets = vtkSurfaceNets2D()
    nets.SetInputData(image)
    nets.SetValue(0, 1)
    nets.SetBackgroundLabel(0)
    nets.SmoothingOff()
    nets.Update()
    mesh = nets.GetOutput()
    if not mesh.GetNumberOfPoints():
        return []
    vertices = vtk_to_numpy(mesh.GetPoints().GetData())[:, :2].astype(float)
    ids = vtkIdList()
    cells = mesh.GetLines()
    cells.InitTraversal()
    edges = []
    while cells.GetNextCell(ids):
        edge = [ids.GetId(i) for i in range(ids.GetNumberOfIds())]
        edges.extend(zip(edge[:-1], edge[1:]))
    # Graph traversal only; extraction and smoothing math belong to VTK.
    adjacency = {}
    for a, b in edges:
        adjacency.setdefault(a, set()).add(b)
        adjacency.setdefault(b, set()).add(a)
    if any(len(n) > 2 for n in adjacency.values()):
        raise ValueError("surface net contains a branching contour; cannot silently reconnect it")
    result = []
    while adjacency:
        ends = [p for p, neighbors in adjacency.items() if len(neighbors) == 1]
        current = min(ends) if ends else min(adjacency)
        chain = [current]
        while current in adjacency and adjacency[current]:
            following = min(adjacency[current])
            adjacency[current].remove(following)
            adjacency[following].remove(current)
            if not adjacency[current]:
                del adjacency[current]
            chain.append(following)
            current = following
        adjacency.pop(current, None)
        if len(chain) > 1:
            result.append(vertices[chain])
    # Surface Nets may emit a half-pixel exterior apron. Restrict extraction to
    # the same sample-center domain as the triangle/MS reference. This also
    # anchors true map-edge intersections before any smoothing is applied.
    domain = box(0,0,h.shape[1]-1,h.shape[0]-1)
    clipped = []
    for curve in result:
        shape = LineString(curve).intersection(domain)
        lines = [shape] if shape.geom_type == "LineString" else list(shape.geoms) if shape.geom_type == "MultiLineString" else []
        clipped.extend(np.asarray(line.coords) for line in lines if line.length > 0)
    return clipped


def extract(h: np.ndarray, level: float, method: str) -> list[np.ndarray]:
    if method == METHODS[0]:
        return [c[:, ::-1].copy() for c in find_contours(h, level, fully_connected="low")]
    if method == METHODS[1]:
        return nets_curves(h, level)
    raise ValueError("unknown boundary method")


def contour_faces(curves: list[np.ndarray], h: np.ndarray):
    domain = box(0, 0, h.shape[1]-1, h.shape[0]-1)
    vertices = {}
    index = 0
    for curve in curves:
        points = curve[:-1] if np.array_equal(curve[0],curve[-1]) else curve
        for p in points:
            key = tuple(p)
            if key in vertices:
                raise ValueError("contours share a vertex; face correspondence is ambiguous")
            vertices[key] = index
            index += 1
    for i,p in enumerate(domain.exterior.coords[:-1]):
        vertices.setdefault(tuple(p),-i-1)
    linework = unary_union([domain.boundary, *[LineString(c) for c in curves]])
    faces = {}
    for polygon in polygonize(linework):
        try:
            key = frozenset(vertices[tuple(p)] for ring in (polygon.exterior,*polygon.interiors)
                            for p in ring.coords[:-1])
        except KeyError as error:
            raise ValueError("contour intersection creates an unmatched face vertex") from error
        if key in faces:
            raise ValueError("contour faces do not have unique correspondence")
        faces[key] = polygon
    return faces


def filled(curves: list[np.ndarray], h: np.ndarray, level: float,
           reference_curves: list[np.ndarray] | None = None):
    domain = box(0, 0, h.shape[1]-1, h.shape[0]-1)
    if not curves:
        return domain if np.all(h > level) else domain.difference(domain)
    # Freeze inside/outside labels on the ORIGINAL contour faces. Re-evaluating
    # original h at moved face centroids would invent topology changes as tiny
    # loops move. Stable vertex identities transfer labels, not water values.
    original = contour_faces(reference_curves if reference_curves is not None else curves,h)
    moved = contour_faces(curves,h) if reference_curves is not None else original
    if original.keys() != moved.keys():
        raise ValueError("smoothing changed the contour face graph")
    wet = []
    for key,polygon in original.items():
        p = polygon.representative_point()
        if map_coordinates(h,[[p.y],[p.x]],order=1,mode="nearest")[0] > level:
            wet.append(moved[key])
    return unary_union(wet)


def topology(shape) -> tuple[int, int]:
    polygons = [shape] if shape.geom_type == "Polygon" else list(shape.geoms) if shape.geom_type == "MultiPolygon" else []
    return len(polygons), sum(len(p.interiors) for p in polygons)


def resample(curve: np.ndarray, spacing: float = 0.125) -> np.ndarray:
    lengths = np.linalg.norm(np.diff(curve, axis=0), axis=1)
    distance = np.r_[0., np.cumsum(lengths)]
    if distance[-1] == 0:
        return curve.copy()
    closed = np.array_equal(curve[0], curve[-1])
    t = np.linspace(0, distance[-1], max(3, math.ceil(distance[-1]/spacing)), endpoint=not closed)
    return np.column_stack([np.interp(t, distance, curve[:, k]) for k in (0, 1)])


def turn_energy(curves: list[np.ndarray]) -> float:
    total, length = 0., 0.
    for c in curves:
        p = resample(c)
        closed = np.array_equal(c[0], c[-1])
        edges = np.diff(np.vstack((p, p[:1])) if closed else p, axis=0)
        norms = np.linalg.norm(edges, axis=1)
        good = norms > 1e-12
        directions = edges[good] / norms[good, None]
        a, b = (directions, np.roll(directions, -1, axis=0)) if closed else (directions[:-1], directions[1:])
        angles = np.arctan2(a[:, 0]*b[:, 1]-a[:, 1]*b[:, 0], np.sum(a*b, axis=1))
        total += float(np.sum(angles*angles))
        length += float(np.sum(norms))
    return total / max(length, 1e-12)


def width_samples(shape) -> list[float]:
    return [shape.intersection(LineString([(0, y), (31, y)])).length for y in (6., 9., 12., 14.)]


def linework_simple(curves: list[np.ndarray]) -> bool:
    return not curves or bool(MultiLineString(curves).is_simple)


def comparison(name: str, h: np.ndarray, level: float, raw: list[np.ndarray], curves: list[np.ndarray],
               protocol: dict) -> tuple[dict, object]:
    shape, original = filled(curves, h, level, raw), filled(raw, h, level)
    limits = protocol["gates"]
    displacement = max((float(np.max(np.linalg.norm(a-b, axis=1))) for a, b in zip(raw, curves, strict=True)), default=0.)
    area_change = abs(shape.area-original.area) / max(original.area, 1e-12)
    widths = width_samples(shape) if name == "one-cell-stream" else []
    raw_widths = width_samples(original) if widths else []
    width_change = max((abs(a-b)/max(b, 1e-12) for a, b in zip(widths, raw_widths)), default=0.)
    expected = (2, 0) if name in ("positive-film-gap", "dry-gap", "diagonal-separated") else (1, 1) if name == "island-ring" else (1, 0)
    reduction = 1-turn_energy(curves)/max(turn_energy(raw), 1e-12)
    checks = {
        "valid_shape": bool(shape.is_valid), "nonintersecting_contours": linework_simple(curves),
        "topology_preserved": topology(shape) == topology(original),
        "expected_topology": topology(shape) == expected,
        "displacement_bounded": displacement <= limits["max_boundary_displacement_cells"] + 1e-9,
        "area_bounded": area_change <= limits["max_relative_area_change"],
        "width_bounded": width_change <= limits["max_relative_straight_stream_width_change"],
    }
    if name in ("round-pool", "curved-stream"):
        checks["steps_reduced"] = reduction >= limits["minimum_curved_fixture_turn_energy_reduction"]
    return {"case": name, "level_m": level, "components_holes": list(topology(shape)),
            "max_displacement_cells": displacement, "relative_area_change": area_change,
            "area_cells2": shape.area, "relative_width_change": width_change,
            "turn_energy_reduction": reduction, "checks": checks}, shape


def draw(ax, h: np.ndarray, curves: list[np.ndarray], shape, title: str) -> None:
    ax.imshow(h, origin="lower", extent=(-.5,h.shape[1]-.5,-.5,h.shape[0]-.5),
              cmap="Greys", vmin=0, vmax=.1, alpha=.4, interpolation="nearest")
    polygons = [shape] if shape.geom_type == "Polygon" else list(shape.geoms) if shape.geom_type == "MultiPolygon" else []
    for p in polygons:
        xy = np.array(p.exterior.coords)
        ax.fill(xy[:,0], xy[:,1], color="#2094c6", alpha=.5)
        for hole in p.interiors:
            xy = np.array(hole.coords)
            ax.fill(xy[:,0],xy[:,1],color="white")
    for c in curves:
        ax.plot(c[:,0],c[:,1], color="#d64226", lw=1.1)
    ax.set(xlim=(-.5,h.shape[1]-.5), ylim=(-.5,h.shape[0]-.5), title=title, aspect="equal")
    ax.set_xticks(np.arange(0, h.shape[1], 2))
    ax.set_yticks(np.arange(0, h.shape[0], 2))
    ax.grid(alpha=.15)


def run(out: Path) -> dict:
    protocol = json.loads((out / "protocol.json").read_text())
    if tuple(protocol["methods"]) != METHODS:
        raise ValueError("unexpected protocol methods")
    initial = checkpoint(out)
    reference.reserve_directory(out / "offline")
    reference.reserve_directory(out / "review")
    fields = fixtures()
    rows, summaries, assets = [], [], []
    for method in METHODS:
        raw_cache, extraction_errors = {}, {}
        for name,h in fields.items():
            for level in protocol["levels_m"]:
                try:
                    raw_cache[(name,level)] = extract(h,level,method)
                except ValueError as error:
                    extraction_errors[(name,level)] = str(error)
        for variant in protocol["variants"]:
            shapes, contours = {}, {}
            group = []
            for name, h in fields.items():
                frozen_h = digest_array(h)
                for level in protocol["levels_m"]:
                    if (name,level) in extraction_errors:
                        group.append({"case":name,"level_m":level,"method":method,"variant":variant["name"],
                                      "error":extraction_errors[(name,level)],"checks":{"nonbranching_contour":False}})
                        continue
                    raw = raw_cache[(name, level)]
                    start = time.perf_counter()
                    curves = constrained(raw, variant, protocol["relaxation"])
                    elapsed = (time.perf_counter()-start)*1000
                    repeated = constrained(extract(h, level, method), variant, protocol["relaxation"])
                    deterministic = len(curves) == len(repeated) and all(np.array_equal(a,b) for a,b in zip(curves,repeated))
                    row, shape = comparison(name,h,level,raw,curves,protocol)
                    row.update(method=method,variant=variant["name"],smoothing_ms=elapsed)
                    row["checks"].update(deterministic=deterministic, input_unchanged=digest_array(h)==frozen_h)
                    shapes[(name,level)], contours[(name,level)] = shape, curves
                    rows.append(row)
                    group.append(row)
            for level in protocol["levels_m"]:
                delta = shapes[("recession-late",level)].difference(shapes[("recession-early",level)]).area
                group.append({"case":"recession-subset","level_m":level,"method":method,"variant":variant["name"],
                              "new_area_cells2":delta,"checks":{"recession_subset":delta <= protocol["gates"]["recession_new_area_tolerance_cells2"]}})
            for name in fields:
                for low,high in zip(protocol["levels_m"][:-1], protocol["levels_m"][1:]):
                    if (name,high) not in shapes or (name,low) not in shapes:
                        continue
                    delta = shapes[(name,high)].difference(shapes[(name,low)]).area
                    group.append({"case":name,"levels_m":[low,high],"method":method,"variant":variant["name"],
                                  "outside_lower_band_area_cells2":delta,
                                  "checks":{"nested_bands":delta <= protocol["gates"]["nesting_area_tolerance_cells2"]}})
            failed = [r for r in group if not all(r["checks"].values())]
            # Raw is a control, not a smoothing candidate. Do not promote it.
            summary = {"method":method,"variant":variant["name"],"passes":not failed and variant["name"]!="raw",
                       "failed_rows":len(failed),"failed_check_names":sorted({k for r in failed for k,v in r["checks"].items() if not v}),
                       "checks":group}
            summaries.append(summary)
            fig, axes = plt.subplots(3,3,figsize=(15,10),layout="constrained")
            selected = ["one-cell-stream","positive-film-gap","diagonal-stream","curved-stream","round-pool",
                        "island-ring","map-edge","recession-early","recession-late"]
            for ax,name in zip(axes.flat,selected):
                draw(ax,fields[name],contours[(name,.026)],shapes[(name,.026)],name)
            fig.suptitle(f"{method} / {variant['name']} — 26 mm DISPLAY contour, grid-cell coordinates")
            path = out / "review" / f"{method}-{variant['name']}.png"
            fig.savefig(path,dpi=140)
            plt.close(fig)
            assets.append(str(path.relative_to(out)))
            print(json.dumps({k:v for k,v in summary.items() if k!="checks"}),flush=True)
    candidates = [s for s in summaries if s["passes"]]
    verdict = "2d-pass-awaiting-cubey-terrain-contact" if candidates else "reject-before-cubey-integration"
    report = {"schema":"cubey.fluid25d.boundary_study.v1","status":verdict,"protocol":protocol,
              "summaries":summaries,"scope":"Synthetic offline boundary reference; no solver or Cubey candidate integration",
              "integration_authorized_by_gate":bool(candidates),"human_visual_acceptance":"deferred"}
    reference.write_json_exclusive(out / "offline/report.json",report)
    assets.append("offline/report.json")
    links = "\n".join(f'<h2>{html.escape(Path(p).stem)}</h2><a href="../{p}"><img width="100%" src="../{p}"></a>'
                       for p in assets if p.endswith(".png"))
    reference.write_text_exclusive(out / "review/index.html",f'<!doctype html><meta charset="utf-8"><title>Cell-scale boundary study</title>'
                                  f'<body style="max-width:1300px;margin:auto;font:18px sans-serif"><h1>Cell-scale boundary study</h1>'
                                  f'<p>{verdict}. These are 2D reference tests, not Cubey renderings or solver results.</p>'
                                  f'<p>Blue: filled display region. Red: extracted/smoothed outline. Gray: original sampled depth.</p>'
                                  f'<p>All grids and axes are world/grid coordinates, not screen-space AA. <a href="../offline/report.json">Full gate results</a></p>{links}</body>')
    current = reference.runtime_identity()
    reference.assert_same_runtime(initial["runtime"], current, "after offline boundary study")
    protected = {**initial["prior_untracked"],**initial["protected_fluid_files"]}
    if any(reference.sha256_file(reference.ROOT/p) != sha for p,sha in protected.items()):
        raise ValueError("pre-existing fluid files changed")
    if reference.sha256_file(out/"protocol.json") != initial["protocol_sha256"]:
        raise ValueError("protocol changed after candidate evaluation")
    reference.write_json_exclusive(out/"offline/integrity.json",{"status":"pass","unchanged_runtime_inputs":True,
        "protected_files":len(protected),"assets":{p:reference.sha256_file(out/p) for p in assets},
        "integration_gate":verdict})
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.parent != reference.OUTPUT_ROOT or not out.name.startswith("shoreline-boundary-study-") or out.is_symlink():
        raise ValueError("output must be a study leaf under outputs/fluid")
    report = run(out)
    print(report["status"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
