#!/usr/bin/env python3

from __future__ import annotations

import dataclasses
import json
import math
import os
import shutil
import sys
import tempfile
import time
from pathlib import Path
from types import ModuleType

STUDY_SCHEMA = "cubey.terrain.desert-canyon-study.v1"
PROBE_SCHEMA = "cubey.terrain.landform-probe.v1"
PROBE_SIZE = 768
PROBE_DOWNSAMPLE = 4
PROBE_ANALYSIS_SIZE = PROBE_SIZE // PROBE_DOWNSAMPLE
PROBE_SPACING_M = 120.0
PROBE_CLOSING_SIZE = 33
PROBE_MARGIN = PROBE_CLOSING_SIZE // 2
CANYON_SHORTLIST_SIZE = 12
CANYON_SELECTION_SIZE = 4
STUDY_PACKAGE_LIMIT_BYTES = 256 * 1024 * 1024
STUDY_GENERATION_LIMIT_SECONDS = 900.0
EXPECTED_STUDY_SELECTIONS = {
    "desert-high-relief": ("desert", 9012, 88, -112),
    "desert-intermediate-relief": ("desert", 9012, -32, 8),
    "desert-low-relief": ("desert", 12345, -64, 88),
    "canyon-candidate-1": ("canyon", 0, 48, -128),
    "canyon-candidate-2": ("canyon", 0, 104, -104),
    "canyon-candidate-3": ("canyon", 12345, -56, -128),
    "canyon-candidate-4": ("canyon", 12345, 8, 8),
}
EXPECTED_STUDY_HASHES = {
    "desert-high-relief": (
        "cd05ae46686a6e0b62b018b9b9f8682a0fd37de84005f2ed42a5ab4bc4350cdc",
        "814b235f18efde7df8fb2285d6510e5ee64323f472d6dae6495fd53ddccac939",
    ),
    "desert-intermediate-relief": (
        "00fc8836855c52caba7d9b114b00d8ba426d0b9f716825f7e19c42f2e273c28d",
        "f40bb2830fd4fc0302747b128b601b859e11b397b882427f0005d56a6984b81f",
    ),
    "desert-low-relief": (
        "f072dd7bc0991d0de1b4ef2c6839373271e0b40e65a23f80aec9fdfd08476cd0",
        "22caddbf02790a7c2f8dee9b345780d83897c3b0d55f846f6cd14a7d9c34bd03",
    ),
    "canyon-candidate-1": (
        "4c6cda32de46801ca52b5edc37a925a947438de59537e55ec7b08c8883f68b51",
        "d2a74ee2b2a35fdb6f54c0a7df184dc8ecd9475ca4f439a75eb5a12a3f9fac64",
    ),
    "canyon-candidate-2": (
        "2a919b516d8ae4fb8c193cdd8db1a8ba055ba702e5cbd1ff50ad2b7fc6ab3c48",
        "e36484c232042800965eea606833bd4ef755c178669a0126180414216934e01e",
    ),
    "canyon-candidate-3": (
        "c35a8d9757fbd856a44bc48dceb49cd939f16b9819727ea07ef97c5d37a7f7d5",
        "b1c92c76a07dd3fdf6ddd51666f28e369463e5d69a5c432767cdb63f40dbd67f",
    ),
    "canyon-candidate-4": (
        "88cc6c1fdacbd9759f90e6781ddf4ff570e4516fb0652438dc28f7ad23086d8b",
        "aa23a8941e27f59d6d946e093a63b6ca1c575661b8546e6428a6278a3abe25bf",
    ),
}


@dataclasses.dataclass(frozen=True)
class ProbeMetrics:
    relief_p05_p95_m: float
    hypsometric_median: float
    mean_slope: float
    p95_slope: float
    plateau_fraction: float
    valley_depth_mean_m: float
    valley_depth_p95_m: float
    valley_depth_p99_m: float
    deep_valley_fraction: float
    largest_deep_component_fraction: float
    deep_component_count: int
    canyon_score: float

    def as_json(self) -> dict[str, object]:
        return dataclasses.asdict(self)


@dataclasses.dataclass(frozen=True)
class ProbeAnalysis:
    metrics: ProbeMetrics
    elevation: object
    slope: object
    valley_depth: object


@dataclasses.dataclass(frozen=True)
class ProbedCandidate:
    candidate: object
    analysis: ProbeAnalysis
    query_seconds: float

    def identity(self) -> tuple[int, int, int]:
        return (
            self.candidate.seed,
            self.candidate.coarse_i,
            self.candidate.coarse_j,
        )

    def as_json(self) -> dict[str, object]:
        return {
            "candidate": self.candidate.as_json(),
            "probe": self.analysis.metrics.as_json(),
            "query_seconds": self.query_seconds,
        }


@dataclasses.dataclass(frozen=True)
class StudySelection:
    id: str
    label: str
    kind: str
    probed: ProbedCandidate

    def as_json(self) -> dict[str, object]:
        return {
            "id": self.id,
            "label": self.label,
            "kind": self.kind,
            **self.probed.as_json(),
        }


def _identity(candidate) -> tuple[int, int, int]:
    return candidate.seed, candidate.coarse_i, candidate.coarse_j


def _coarse_upper_mass(candidate) -> float:
    span = candidate.land_p90_m - candidate.land_p25_m
    if span <= 0.0:
        return 0.0
    return (candidate.land_p50_m - candidate.land_p25_m) / span


def select_desert_candidates(candidates: list[object]) -> list[object]:
    eligible = [
        candidate
        for candidate in candidates
        if candidate.land_fraction >= 0.95
        and candidate.temperature_median_c >= 20.0
        and candidate.precipitation_median_mm <= 200.0
    ]
    if len(eligible) < 3:
        raise RuntimeError(
            f"desert study requires at least three strict candidates, found {len(eligible)}"
        )

    high = max(
        eligible,
        key=lambda candidate: (
            candidate.relief_m,
            -candidate.precipitation_median_mm,
            -candidate.distance_squared,
        ),
    )
    low = min(
        (
            candidate
            for candidate in eligible
            if _identity(candidate) != _identity(high)
        ),
        key=lambda candidate: (
            candidate.relief_m,
            candidate.precipitation_median_mm,
            candidate.distance_squared,
        ),
    )
    remaining = [
        candidate
        for candidate in eligible
        if _identity(candidate) not in {_identity(high), _identity(low)}
    ]
    middle = min(
        remaining,
        key=lambda candidate: (
            candidate.precipitation_median_mm,
            abs(candidate.relief_m - (high.relief_m + low.relief_m) * 0.5),
            candidate.distance_squared,
        ),
    )
    return [high, middle, low]


def select_canyon_shortlist(
    candidates: list[object],
    excluded: set[tuple[int, int, int]],
    limit: int = CANYON_SHORTLIST_SIZE,
) -> list[object]:
    eligible = [
        candidate
        for candidate in candidates
        if _identity(candidate) not in excluded
        and candidate.land_fraction >= 0.95
        and candidate.relief_m >= 1_200.0
        and candidate.temperature_median_c >= 8.0
        and candidate.precipitation_median_mm <= 800.0
    ]
    ranked = sorted(
        eligible,
        key=lambda candidate: (
            -(
                candidate.relief_m
                * (0.75 + max(0.0, min(_coarse_upper_mass(candidate), 1.0)))
            ),
            -_coarse_upper_mass(candidate),
            candidate.precipitation_median_mm,
            candidate.distance_squared,
            candidate.seed,
            candidate.coarse_i,
            candidate.coarse_j,
        ),
    )
    selected = []
    per_seed: dict[int, int] = {}
    for candidate in ranked:
        if per_seed.get(candidate.seed, 0) >= 4:
            continue
        selected.append(candidate)
        per_seed[candidate.seed] = per_seed.get(candidate.seed, 0) + 1
        if len(selected) == limit:
            break
    if len(selected) != limit:
        raise RuntimeError(
            f"canyon study requires {limit} shortlist candidates, found {len(selected)}"
        )
    return selected


def _area_average_2d(values, factor: int):
    import numpy as np

    field = np.asarray(values, dtype=np.float32)
    if field.ndim != 2 or factor <= 0:
        raise ValueError("landform probe must be a two-dimensional field")
    if field.shape[0] % factor or field.shape[1] % factor:
        raise ValueError(
            "landform probe dimensions must be divisible by the analysis factor"
        )
    height, width = field.shape
    return (
        field.reshape(height // factor, factor, width // factor, factor)
        .mean(axis=(1, 3), dtype=np.float64)
        .astype(np.float32)
    )


def analyze_probe(elevation) -> ProbeAnalysis:
    import numpy as np
    from scipy import ndimage

    field = _area_average_2d(elevation, PROBE_DOWNSAMPLE)
    smooth = ndimage.gaussian_filter(
        field.astype(np.float64), sigma=1.25, mode="nearest"
    )
    gradient_z, gradient_x = np.gradient(smooth, PROBE_SPACING_M)
    slope = np.sqrt(gradient_x * gradient_x + gradient_z * gradient_z)
    closed = ndimage.grey_closing(
        smooth,
        size=(PROBE_CLOSING_SIZE, PROBE_CLOSING_SIZE),
        mode="nearest",
    )
    valley_depth = np.maximum(closed - smooth, 0.0)

    interior = (
        slice(PROBE_MARGIN, -PROBE_MARGIN),
        slice(PROBE_MARGIN, -PROBE_MARGIN),
    )
    elevation_inner = smooth[interior]
    slope_inner = slope[interior]
    depth_inner = valley_depth[interior]
    p05, p50, p60, p95 = np.percentile(elevation_inner, (5.0, 50.0, 60.0, 95.0))
    relief = max(float(p95 - p05), 1.0)
    hypsometric_median = float((p50 - p05) / relief)
    plateau_fraction = float(np.mean((elevation_inner >= p60) & (slope_inner <= 0.12)))
    deep = depth_inner >= 100.0
    deep_count = int(np.count_nonzero(deep))
    labeled, _component_count = ndimage.label(
        deep, structure=np.ones((3, 3), dtype=np.uint8)
    )
    component_sizes = np.bincount(labeled.reshape(-1))[1:]
    meaningful = component_sizes[component_sizes >= 8]
    largest_fraction = (
        float(np.max(meaningful, initial=0) / deep_count) if deep_count else 0.0
    )
    meaningful_count = int(meaningful.size)
    depth_mean = float(np.mean(depth_inner))
    depth_p95 = float(np.percentile(depth_inner, 95.0))
    depth_p99 = float(np.percentile(depth_inner, 99.0))
    deep_fraction = float(np.mean(deep))
    dissection_penalty = (
        1.0
        + max(deep_fraction - 0.20, 0.0) * 4.0
        + min(math.log1p(meaningful_count) * 0.12, 0.8)
    )
    canyon_score = (
        min(depth_p95 / 500.0, 4.0)
        * (0.5 + largest_fraction * 1.5)
        * (0.5 + plateau_fraction * 2.0)
        * (0.5 + max(0.0, min(hypsometric_median, 1.0)))
        / dissection_penalty
    )
    metrics = ProbeMetrics(
        relief_p05_p95_m=relief,
        hypsometric_median=hypsometric_median,
        mean_slope=float(np.mean(slope_inner)),
        p95_slope=float(np.percentile(slope_inner, 95.0)),
        plateau_fraction=plateau_fraction,
        valley_depth_mean_m=depth_mean,
        valley_depth_p95_m=depth_p95,
        valley_depth_p99_m=depth_p99,
        deep_valley_fraction=deep_fraction,
        largest_deep_component_fraction=largest_fraction,
        deep_component_count=meaningful_count,
        canyon_score=float(canyon_score),
    )
    return ProbeAnalysis(
        metrics=metrics,
        elevation=field,
        slope=slope.astype(np.float32),
        valley_depth=valley_depth.astype(np.float32),
    )


def select_canyon_candidates(
    probes: list[ProbedCandidate],
    limit: int = CANYON_SELECTION_SIZE,
) -> list[ProbedCandidate]:
    ranked = sorted(
        probes,
        key=lambda probe: (
            -probe.analysis.metrics.canyon_score,
            -probe.analysis.metrics.valley_depth_p95_m,
            -probe.analysis.metrics.largest_deep_component_fraction,
            probe.candidate.seed,
            probe.candidate.distance_squared,
            probe.candidate.coarse_i,
            probe.candidate.coarse_j,
        ),
    )
    selected = []
    per_seed: dict[int, int] = {}
    for probe in ranked:
        if per_seed.get(probe.candidate.seed, 0) >= 2:
            continue
        overlaps = any(
            probe.candidate.seed == other.candidate.seed
            and abs(probe.candidate.coarse_i - other.candidate.coarse_i) < 16
            and abs(probe.candidate.coarse_j - other.candidate.coarse_j) < 16
            for other in selected
        )
        if overlaps:
            continue
        selected.append(probe)
        per_seed[probe.candidate.seed] = per_seed.get(probe.candidate.seed, 0) + 1
        if len(selected) == limit:
            break
    if len(selected) < limit:
        selected_identities = {probe.identity() for probe in selected}
        for probe in ranked:
            if probe.identity() in selected_identities:
                continue
            selected.append(probe)
            selected_identities.add(probe.identity())
            if len(selected) == limit:
                break
    if len(selected) != limit:
        raise RuntimeError(
            f"canyon study requires {limit} selected probes, found {len(selected)}"
        )
    return selected


def _study_selections(
    desert_probes: list[ProbedCandidate],
    canyon_probes: list[ProbedCandidate],
) -> list[StudySelection]:
    desert_ids = (
        ("desert-high-relief", "Desert: high relief"),
        ("desert-intermediate-relief", "Desert: intermediate relief"),
        ("desert-low-relief", "Desert: low relief"),
    )
    result = [
        StudySelection(id_value, label, "desert", probe)
        for (id_value, label), probe in zip(desert_ids, desert_probes, strict=True)
    ]
    result.extend(
        StudySelection(
            f"canyon-candidate-{index}",
            f"Canyon candidate {index}",
            "canyon",
            probe,
        )
        for index, probe in enumerate(canyon_probes, start=1)
    )
    return result


def validate_study_selections(selections: list[StudySelection]) -> None:
    actual = {
        selection.id: (
            selection.kind,
            selection.probed.candidate.seed,
            selection.probed.candidate.coarse_i,
            selection.probed.candidate.coarse_j,
        )
        for selection in selections
    }
    if actual != EXPECTED_STUDY_SELECTIONS:
        raise RuntimeError("terrain desert/canyon study selections changed")


def _probe_bounds(common: ModuleType, candidate) -> tuple[int, int, int, int]:
    native_i = candidate.coarse_i * common.COARSE_CELL_NATIVE_SAMPLES
    native_j = candidate.coarse_j * common.COARSE_CELL_NATIVE_SAMPLES
    inset = (common.FIELD_SIZE - PROBE_SIZE) // 2
    return (
        native_i + inset,
        native_j + inset,
        native_i + inset + PROBE_SIZE,
        native_j + inset + PROBE_SIZE,
    )


def _query_probes(
    common: ModuleType,
    world,
    torch,
    candidates: list[object],
) -> list[ProbedCandidate]:
    result = []
    active_seed = None
    for candidate in sorted(
        candidates,
        key=lambda value: (
            value.seed,
            value.coarse_i,
            value.coarse_j,
        ),
    ):
        if candidate.seed != active_seed:
            common._seed_process_rngs(torch, candidate.seed)
            world.change_seed(candidate.seed)
            active_seed = candidate.seed
        tile = common._query_tile(
            world,
            torch,
            f"probe-{candidate.seed}-{candidate.coarse_i}-{candidate.coarse_j}",
            _probe_bounds(common, candidate),
            False,
        )
        result.append(
            ProbedCandidate(
                candidate=candidate,
                analysis=analyze_probe(tile.elevation),
                query_seconds=tile.seconds,
            )
        )
    return result


def _landscape_selection(common: ModuleType, selection: StudySelection):
    candidate = selection.probed.candidate
    variant = common.LandscapeVariantSpec(
        selection.id,
        selection.label,
        candidate.relief_m,
        0.0,
        max(candidate.relief_m * 2.0, 1.0),
        candidate.temperature_median_c,
        None,
        None,
        candidate.precipitation_median_mm,
        None,
        None,
        0.0,
    )
    return common.LandscapeSelection(
        variant,
        candidate,
        selection.probed.analysis.metrics.canyon_score,
    )


def _generate_selected_regions(
    common: ModuleType,
    world,
    torch,
    selections: list[StudySelection],
) -> list[dict[str, object]]:
    generated_by_id = {}
    active_seed = None
    for selection in sorted(
        selections,
        key=lambda value: (
            value.probed.candidate.seed,
            value.id,
        ),
    ):
        candidate = selection.probed.candidate
        if candidate.seed != active_seed:
            common._seed_process_rngs(torch, candidate.seed)
            world.change_seed(candidate.seed)
            active_seed = candidate.seed
        generated_by_id[selection.id] = common._generate_landscape_region(
            world,
            torch,
            _landscape_selection(common, selection),
        )
    return [generated_by_id[selection.id] for selection in selections]


def _write_probe_previews(
    common: ModuleType,
    root: Path,
    probed: ProbedCandidate,
) -> dict[str, str]:
    import numpy as np

    identity = (
        f"seed-{probed.candidate.seed}-"
        f"{probed.candidate.coarse_i}-{probed.candidate.coarse_j}"
    )
    directory = root / "probes" / identity
    directory.mkdir(parents=True, exist_ok=True)
    rendered_height = (
        np.asarray(probed.analysis.elevation, dtype=np.float64)
        + common.DEFAULT_ASSET_HEIGHT_OFFSET_M
    ) * common.DEFAULT_ASSET_HEIGHT_SCALE
    common._write_rgb8(
        directory / "height.png",
        common.height_preview_rgb(rendered_height),
    )
    common._write_rgb8(
        directory / "slope.png",
        common.slope_preview_rgb(probed.analysis.slope),
    )
    common._write_rgb8(
        directory / "valley-depth.png",
        common.height_preview_rgb(
            np.asarray(probed.analysis.valley_depth, dtype=np.float64) * 3.5
        ),
    )
    return {
        "height": str((directory / "height.png").relative_to(root)),
        "slope": str((directory / "slope.png").relative_to(root)),
        "valley_depth": str((directory / "valley-depth.png").relative_to(root)),
    }


def _directory_size(path: Path) -> int:
    return sum(item.stat().st_size for item in path.rglob("*") if item.is_file())


def validate_existing_asset(common: ModuleType, output_dir: Path) -> bool:
    try:
        root = output_dir.resolve()
        index = json.loads((root / "study-index.json").read_text())
        if index.get("schema") != STUDY_SCHEMA:
            raise RuntimeError("terrain landform study schema changed")
        common._validate_pinned_source(index.get("source"))
        variants = index.get("variants")
        if not isinstance(variants, list) or len(variants) != 7:
            raise RuntimeError("terrain landform study catalog is incomplete")
        if [record.get("id") for record in variants] != list(EXPECTED_STUDY_SELECTIONS):
            raise RuntimeError("terrain landform study catalog order changed")
        if [record.get("kind") for record in variants].count("desert") != 3:
            raise RuntimeError("terrain landform desert catalog is incomplete")
        if [record.get("kind") for record in variants].count("canyon") != 4:
            raise RuntimeError("terrain landform canyon catalog is incomplete")
        for record in variants:
            name = record["id"]
            selection = record.get("study_selection", {})
            candidate = selection.get("candidate", {})
            actual_selection = (
                record.get("kind"),
                candidate.get("seed"),
                candidate.get("coarse_i"),
                candidate.get("coarse_j"),
            )
            if actual_selection != EXPECTED_STUDY_SELECTIONS.get(name):
                raise RuntimeError("terrain landform study selection changed")
            expected_hashes = EXPECTED_STUDY_HASHES.get(name)
            if (
                expected_hashes is None
                or (
                    record.get("elevation_sha256"),
                    record.get("climate_sha256"),
                )
                != expected_hashes
            ):
                raise RuntimeError("terrain landform study hash changed")
            directory = root / name
            elevation_hash = common._validate_heightfield_bundle(
                directory,
                record.get("elevation_sha256"),
                record.get("seed"),
            )
            climate_hash = common._validate_surface_bundle(
                directory,
                elevation_hash,
                record.get("climate_sha256"),
                record.get("seed"),
            )
            if elevation_hash != record.get(
                "elevation_sha256"
            ) or climate_hash != record.get("climate_sha256"):
                raise RuntimeError("terrain landform study identity changed")
        return True
    except (
        OSError,
        KeyError,
        TypeError,
        ValueError,
        json.JSONDecodeError,
        RuntimeError,
    ):
        return False


def bake_landform_study_assets(
    common: ModuleType,
    reference_root: Path,
    output_dir: Path,
    data_cache: Path,
) -> None:
    import torch
    from huggingface_hub import snapshot_download

    reference_root = reference_root.resolve()
    output_dir = output_dir.resolve()
    data_cache = data_cache.resolve()
    actual_revision = common.git_revision(reference_root)
    if actual_revision != common.CODE_REVISION:
        raise RuntimeError(
            f"terrain-diffusion checkout must be {common.CODE_REVISION}, got {actual_revision}"
        )
    if not torch.cuda.is_available():
        raise RuntimeError("Terrain desert/canyon study generation requires CUDA")

    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    torch.set_float32_matmul_precision("highest")
    torch.use_deterministic_algorithms(True, warn_only=True)

    common._prepare_data_cache(reference_root, data_cache)
    snapshot = Path(
        snapshot_download(
            repo_id=common.MODEL_ID,
            revision=common.MODEL_REVISION,
        )
    ).resolve()
    sys.path.insert(0, str(reference_root))
    from terrain_diffusion.inference.world_pipeline import WorldPipeline

    generation_start = time.perf_counter()
    with common._working_directory(data_cache):
        common._seed_process_rngs(torch, common.SEEDS[0])
        world = WorldPipeline.from_pretrained(
            str(snapshot),
            seed=common.SEEDS[0],
            latents_batch_size=common.LATENTS_BATCH_SIZE,
            torch_compile=False,
            dtype=None,
            caching_strategy="direct",
            cache_limit=None,
            log_mode="info",
        )
        world.to("cuda")
        world.bind()
        if float(world.native_resolution) != common.MODEL_NATIVE_RESOLUTION_M:
            raise RuntimeError(
                f"expected {common.MODEL_NATIVE_RESOLUTION_M:g} m model, "
                f"got {world.native_resolution}"
            )

        try:
            all_candidates = []
            scan_records = []
            scan_start = time.perf_counter()
            for seed in common.SEEDS:
                common._seed_process_rngs(torch, seed)
                world.change_seed(seed)
                coarse, candidates = common._coarse_landscape_scan(world, seed)
                common._cuda_sync(torch)
                all_candidates.extend(candidates)
                scan_records.append(
                    {
                        "seed": seed,
                        "shape": list(coarse.shape),
                        "candidate_count": len(candidates),
                    }
                )
            scan_seconds = time.perf_counter() - scan_start

            desert_candidates = select_desert_candidates(all_candidates)
            canyon_shortlist = select_canyon_shortlist(
                all_candidates,
                {_identity(candidate) for candidate in desert_candidates},
            )
            unique_probe_candidates = {
                _identity(candidate): candidate
                for candidate in [*desert_candidates, *canyon_shortlist]
            }
            probe_start = time.perf_counter()
            probes = _query_probes(
                common,
                world,
                torch,
                list(unique_probe_candidates.values()),
            )
            probe_seconds = time.perf_counter() - probe_start
            probes_by_identity = {probe.identity(): probe for probe in probes}
            desert_probes = [
                probes_by_identity[_identity(candidate)]
                for candidate in desert_candidates
            ]
            canyon_shortlist_probes = [
                probes_by_identity[_identity(candidate)]
                for candidate in canyon_shortlist
            ]
            canyon_probes = select_canyon_candidates(canyon_shortlist_probes)
            selections = _study_selections(desert_probes, canyon_probes)
            validate_study_selections(selections)
            generated_regions = _generate_selected_regions(
                common,
                world,
                torch,
                selections,
            )
        finally:
            world.close()

    common_source = {
        "id": "terrain-diffusion-30m",
        "generator": "terrain-diffusion",
        "code_revision": common.CODE_REVISION,
        "model_id": common.MODEL_ID,
        "model_revision": common.MODEL_REVISION,
        "model_snapshot": str(snapshot),
        "native_resolution_m": common.MODEL_NATIVE_RESOLUTION_M,
        "settings": {
            "device": "cuda",
            "dtype": "fp32",
            "latents_batch_size": common.LATENTS_BATCH_SIZE,
            "torch_compile": False,
            "caching_strategy": "direct",
            "custom_conditioning": False,
            "frequency_mult": [1.5, 3.0, 3.0, 3.0, 3.0],
            "drop_water_pct": 0.5,
            "cond_snr": [0.3, 0.1, 1.0, 0.1, 1.0],
            "coarse_pooling": 1,
            "process_rng_seeding": "seed-value-v1",
            "climate_output": True,
        },
    }

    output_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(
        tempfile.mkdtemp(prefix=f"{output_dir.name}.tmp.", dir=output_dir.parent)
    )
    try:
        probe_records = []
        preview_paths = {}
        for probe in probes:
            previews = _write_probe_previews(common, temporary, probe)
            preview_paths[probe.identity()] = previews
            probe_records.append({**probe.as_json(), "previews": previews})

        variants = []
        for selection, generated in zip(selections, generated_regions, strict=True):
            record = common._write_landscape_variation_region(
                temporary,
                generated,
                common_source,
            )
            expected_hashes = EXPECTED_STUDY_HASHES[selection.id]
            actual_hashes = (
                record.get("elevation_sha256"),
                record.get("climate_sha256"),
            )
            if actual_hashes != expected_hashes:
                raise RuntimeError(
                    f"terrain landform study hash changed for {selection.id}"
                )
            variants.append(
                {
                    **record,
                    "kind": selection.kind,
                    "study_selection": selection.as_json(),
                    "probe_previews": preview_paths[selection.probed.identity()],
                }
            )

        generation_seconds = time.perf_counter() - generation_start
        index = {
            "schema": STUDY_SCHEMA,
            "source": common_source,
            "seeds": list(common.SEEDS),
            "scan": {
                "coarse_bounds": {
                    "begin_i": common.CLIMATE_SCAN_BEGIN,
                    "begin_j": common.CLIMATE_SCAN_BEGIN,
                    "end_i": common.CLIMATE_SCAN_END,
                    "end_j": common.CLIMATE_SCAN_END,
                },
                "sample_spacing_m": common.CLIMATE_MACRO_SPACING_M,
                "window_cells": common.COARSE_WINDOW_CELLS,
                "window_extent_m": common.FIELD_SIZE * common.MODEL_NATIVE_RESOLUTION_M,
                "selection_seconds": scan_seconds,
                "candidate_count": len(all_candidates),
                "worlds": scan_records,
            },
            "probe_contract": {
                "schema": PROBE_SCHEMA,
                "native_size": [PROBE_SIZE, PROBE_SIZE],
                "native_spacing_m": common.MODEL_NATIVE_RESOLUTION_M,
                "analysis_size": [
                    PROBE_ANALYSIS_SIZE,
                    PROBE_ANALYSIS_SIZE,
                ],
                "analysis_spacing_m": PROBE_SPACING_M,
                "downsample": PROBE_DOWNSAMPLE,
                "closing_size": PROBE_CLOSING_SIZE,
                "deep_valley_threshold_m": 100.0,
                "probe_seconds": probe_seconds,
            },
            "selection": {
                "method": "coarse-shortlist-intermediate-morphology-v1",
                "desert_contract": {
                    "minimum_land_fraction": 0.95,
                    "minimum_temperature_c": 20.0,
                    "maximum_precipitation_mm": 200.0,
                },
                "canyon_contract": {
                    "minimum_land_fraction": 0.95,
                    "minimum_relief_m": 1_200.0,
                    "minimum_temperature_c": 8.0,
                    "maximum_precipitation_mm": 800.0,
                    "shortlist_size": CANYON_SHORTLIST_SIZE,
                    "selection_size": CANYON_SELECTION_SIZE,
                },
                "selected": [selection.as_json() for selection in selections],
            },
            "probes": probe_records,
            "field_contract": {
                "elevation_size": [common.FIELD_SIZE, common.FIELD_SIZE],
                "elevation_spacing_m": common.MODEL_NATIVE_RESOLUTION_M,
                "climate_size": [
                    common.SURFACE_STUDY_SIZE,
                    common.SURFACE_STUDY_SIZE,
                ],
                "climate_spacing_m": common.MODEL_NATIVE_RESOLUTION_M
                * common.SURFACE_STUDY_DOWNSAMPLE,
            },
            "variants": variants,
            "validation": {
                "desert_count": 3,
                "canyon_count": 4,
                "generation_seconds": generation_seconds,
                "generation_limit_seconds": STUDY_GENERATION_LIMIT_SECONDS,
                "within_generation_limit": generation_seconds
                <= STUDY_GENERATION_LIMIT_SECONDS,
                "package_limit_bytes": STUDY_PACKAGE_LIMIT_BYTES,
                "package_bytes": 0,
                "within_package_limit": True,
            },
        }
        index_path = temporary / "study-index.json"
        for _ in range(8):
            index_path.write_text(
                json.dumps(index, indent=2, sort_keys=True, allow_nan=False) + "\n"
            )
            package_bytes = _directory_size(temporary)
            if index["validation"]["package_bytes"] == package_bytes:
                break
            index["validation"]["package_bytes"] = package_bytes
        else:
            raise RuntimeError("terrain landform study package size did not stabilize")

        package_bytes = _directory_size(temporary)
        if package_bytes > STUDY_PACKAGE_LIMIT_BYTES:
            raise RuntimeError(
                f"terrain landform study package is {package_bytes} bytes, over "
                f"the {STUDY_PACKAGE_LIMIT_BYTES}-byte limit"
            )
        if generation_seconds > STUDY_GENERATION_LIMIT_SECONDS:
            raise RuntimeError(
                f"terrain landform study generation took {generation_seconds:.3f} "
                f"seconds, over the {STUDY_GENERATION_LIMIT_SECONDS:.0f}-second limit"
            )
        if output_dir.exists():
            shutil.rmtree(output_dir)
        os.replace(temporary, output_dir)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise

    print(f"terrain desert/canyon study assets: wrote {output_dir}")
