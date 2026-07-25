#!/usr/bin/env python3

from __future__ import annotations

import dataclasses
import json
import math
import os
import re
import shutil
import sys
import tempfile
import time
from pathlib import Path
from types import ModuleType

CATALOG_SCHEMA = "cubey.terrain.source-presets.v1"
BUNDLE_SCHEMA = "cubey.terrain.source-preset-bundle.v1"
OPTIONAL_GENERATION_LIMIT_SECONDS = 180.0
ID_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


@dataclasses.dataclass(frozen=True)
class SourcePresetRecipe:
    id: str
    label: str
    tier: str
    asset_directory: str
    generation_target: str
    source_study: str | None
    generator_mode: str
    climate_output: bool
    candidate: dict[str, object] | None
    seed: int
    expected_elevation_sha256: str
    expected_climate_sha256: str | None


@dataclasses.dataclass(frozen=True)
class SourcePresetCatalog:
    path: Path
    default_preset: str
    recipes: tuple[SourcePresetRecipe, ...]

    def recipe(self, preset_id: str) -> SourcePresetRecipe:
        for recipe in self.recipes:
            if recipe.id == preset_id:
                return recipe
        raise RuntimeError(f"unknown terrain source preset: {preset_id}")


def generation_marker_path(output_dir: Path) -> Path:
    return output_dir.parent / f"{output_dir.name}.generating"


def _require_object(value: object, label: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise TypeError(f"terrain source preset {label} must be an object")
    return value


def _require_string(record: dict[str, object], key: str) -> str:
    value = record.get(key)
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"terrain source preset {key} must be a non-empty string")
    return value


def _require_int(record: dict[str, object], key: str) -> int:
    value = record.get(key)
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError(f"terrain source preset {key} must be an integer")
    return value


def _require_float(record: dict[str, object], key: str) -> float:
    value = record.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise TypeError(f"terrain source preset {key} must be numeric")
    result = float(value)
    if not math.isfinite(result):
        raise RuntimeError(f"terrain source preset {key} must be finite")
    return result


def _validate_sha256(value: object, label: str, allow_none: bool = False) -> str | None:
    if value is None and allow_none:
        return None
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        raise RuntimeError(f"terrain source preset {label} must be a SHA-256")
    return value


def _candidate_record(record: dict[str, object]) -> dict[str, object]:
    expected_keys = {
        "seed",
        "coarse_i",
        "coarse_j",
        "land_fraction",
        "land_p25_m",
        "land_p50_m",
        "land_p90_m",
        "relief_m",
        "temperature_median_c",
        "temperature_stddev_median_c",
        "precipitation_median_mm",
        "precipitation_cv_median",
        "distance_squared",
    }
    if set(record) != expected_keys:
        raise RuntimeError("terrain source preset candidate fields are incompatible")
    result: dict[str, object] = {
        "seed": _require_int(record, "seed"),
        "coarse_i": _require_int(record, "coarse_i"),
        "coarse_j": _require_int(record, "coarse_j"),
        "distance_squared": _require_int(record, "distance_squared"),
    }
    for key in sorted(expected_keys - set(result)):
        result[key] = _require_float(record, key)
    if result["seed"] < 0:
        raise RuntimeError("terrain source preset seed must be non-negative")
    if not 0.0 <= result["land_fraction"] <= 1.0:
        raise RuntimeError("terrain source preset land fraction is outside [0, 1]")
    if result["relief_m"] <= 0.0:
        raise RuntimeError("terrain source preset relief must be positive")
    return result


def load_source_preset_catalog(
    common: ModuleType, catalog_path: Path
) -> SourcePresetCatalog:
    path = catalog_path.resolve()
    try:
        document = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(
            f"failed to load terrain source preset catalog: {error}"
        ) from error
    if document.get("schema") != CATALOG_SCHEMA:
        raise RuntimeError("terrain source preset catalog schema is incompatible")

    producer = _require_object(document.get("producer"), "producer")
    expected_producer = {
        "code_revision": common.CODE_REVISION,
        "model_id": common.MODEL_ID,
        "model_revision": common.MODEL_REVISION,
        "native_resolution_m": common.MODEL_NATIVE_RESOLUTION_M,
    }
    if producer != expected_producer:
        raise RuntimeError(
            "terrain source preset producer does not match pinned source"
        )

    policy = _require_object(document.get("policy"), "policy")
    if policy != {
        "generated_assets_committed": False,
        "default_generation": "single-preset-single-query",
        "optional_generation": "one-preset-per-explicit-target",
        "study_generation": "developer-only",
    }:
        raise RuntimeError("terrain source preset generation policy is incompatible")

    default_preset = _require_string(document, "default_preset")
    records = document.get("presets")
    if not isinstance(records, list) or not records:
        raise RuntimeError("terrain source preset catalog contains no presets")

    ids = set()
    recipes = []
    for raw_record in records:
        record = _require_object(raw_record, "entry")
        preset_id = _require_string(record, "id")
        label = _require_string(record, "label")
        tier = _require_string(record, "tier")
        asset_directory = _require_string(record, "asset_directory")
        generation_target = _require_string(record, "generation_target")
        if (
            ID_PATTERN.fullmatch(preset_id) is None
            or preset_id in ids
            or tier not in {"default", "optional"}
        ):
            raise RuntimeError(
                "terrain source preset identity is invalid or duplicated"
            )
        ids.add(preset_id)
        if tier == "optional" and asset_directory != preset_id:
            raise RuntimeError("optional terrain preset directory must match its id")
        expected_target = (
            "cubey_terrain_generate_default_asset"
            if tier == "default"
            else f"cubey_terrain_generate_{preset_id.replace('-', '_')}"
        )
        if generation_target != expected_target:
            raise RuntimeError(
                "terrain source preset generation target is incompatible"
            )

        generator = _require_object(record.get("generator"), "generator")
        mode = _require_string(generator, "mode")
        climate_output = generator.get("climate_output")
        if not isinstance(climate_output, bool):
            raise TypeError("terrain source preset climate output must be boolean")

        source_study = record.get("source_study")
        if source_study is not None and (
            not isinstance(source_study, str) or not source_study
        ):
            raise RuntimeError("terrain source preset study provenance is invalid")

        candidate = None
        if tier == "default":
            if (
                mode != "canonical-default"
                or preset_id != default_preset
                or climate_output
            ):
                raise RuntimeError("terrain default preset generator is incompatible")
            seed = _require_int(generator, "seed")
            origin = _require_object(
                generator.get("model_native_origin"), "model native origin"
            )
            if (
                seed != common.ORDER_CHECK_SEED
                or _require_int(origin, "i") != -2048
                or _require_int(origin, "j") != -6144
            ):
                raise RuntimeError("terrain default preset identity changed")
        else:
            if mode != "natural-region" or not climate_output:
                raise RuntimeError("optional terrain preset generator is incompatible")
            candidate = _candidate_record(
                _require_object(generator.get("candidate"), "candidate")
            )
            seed = int(candidate["seed"])
            if seed not in common.SEEDS:
                raise RuntimeError("optional terrain preset seed is not pinned")

        expected = _require_object(record.get("expected"), "expected")
        elevation_sha256 = _validate_sha256(
            expected.get("elevation_sha256"), "elevation hash"
        )
        climate_sha256 = _validate_sha256(
            expected.get("climate_sha256"),
            "climate hash",
            allow_none=tier == "default",
        )
        if tier == "default" and (
            elevation_sha256 != common.DEFAULT_ASSET_ELEVATION_SHA256
            or climate_sha256 is not None
        ):
            raise RuntimeError("terrain default preset hashes changed")
        if tier == "optional" and climate_sha256 is None:
            raise RuntimeError("optional terrain preset climate hash is missing")

        recipes.append(
            SourcePresetRecipe(
                id=preset_id,
                label=label,
                tier=tier,
                asset_directory=asset_directory,
                generation_target=generation_target,
                source_study=source_study,
                generator_mode=mode,
                climate_output=climate_output,
                candidate=candidate,
                seed=seed,
                expected_elevation_sha256=elevation_sha256,
                expected_climate_sha256=climate_sha256,
            )
        )

    defaults = [recipe for recipe in recipes if recipe.tier == "default"]
    if len(defaults) != 1 or defaults[0].id != default_preset:
        raise RuntimeError("terrain source preset catalog must contain one default")
    return SourcePresetCatalog(path, default_preset, tuple(recipes))


def _landscape_selection(common: ModuleType, recipe: SourcePresetRecipe):
    if recipe.candidate is None:
        raise RuntimeError("default terrain preset uses the canonical generator")
    candidate = common.LandscapeCandidate(**recipe.candidate)
    variant = common.LandscapeVariantSpec(
        recipe.id,
        recipe.label,
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
    return common.LandscapeSelection(variant, candidate, 0.0)


def validate_source_preset_asset(
    common: ModuleType,
    catalog_path: Path,
    preset_id: str,
    output_dir: Path,
) -> bool:
    try:
        catalog = load_source_preset_catalog(common, catalog_path)
        recipe = catalog.recipe(preset_id)
        if recipe.tier != "optional":
            return False
        root = output_dir.resolve()
        bundle = json.loads((root / "preset.json").read_text())
        if (
            bundle.get("schema") != BUNDLE_SCHEMA
            or bundle.get("id") != recipe.id
            or bundle.get("label") != recipe.label
            or bundle.get("tier") != "optional"
            or bundle.get("seed") != recipe.seed
            or bundle.get("source_study") != recipe.source_study
            or bundle.get("catalog_sha256") != common.sha256_file(catalog.path)
            or bundle.get("elevation_sha256") != recipe.expected_elevation_sha256
            or bundle.get("climate_sha256") != recipe.expected_climate_sha256
        ):
            raise RuntimeError("terrain source preset bundle contract changed")
        selection = bundle.get("selection", {}).get("candidate", {})
        if (
            selection.get("seed"),
            selection.get("coarse_i"),
            selection.get("coarse_j"),
        ) != (
            recipe.candidate["seed"],
            recipe.candidate["coarse_i"],
            recipe.candidate["coarse_j"],
        ):
            raise RuntimeError("terrain source preset selection changed")
        elevation_sha256 = common._validate_heightfield_bundle(
            root, recipe.expected_elevation_sha256, recipe.seed
        )
        climate_sha256 = common._validate_surface_bundle(
            root,
            elevation_sha256,
            recipe.expected_climate_sha256,
            recipe.seed,
        )
        return (
            elevation_sha256 == recipe.expected_elevation_sha256
            and climate_sha256 == recipe.expected_climate_sha256
        )
    except (
        OSError,
        KeyError,
        TypeError,
        ValueError,
        json.JSONDecodeError,
        RuntimeError,
    ):
        return False


def bake_source_preset_asset(
    common: ModuleType,
    reference_root: Path,
    catalog_path: Path,
    preset_id: str,
    output_dir: Path,
    data_cache: Path,
) -> None:
    import torch
    from huggingface_hub import snapshot_download

    reference_root = reference_root.resolve()
    output_dir = output_dir.resolve()
    data_cache = data_cache.resolve()
    catalog = load_source_preset_catalog(common, catalog_path)
    recipe = catalog.recipe(preset_id)
    if recipe.tier != "optional":
        raise RuntimeError(
            f"{preset_id} is the canonical default; use {recipe.generation_target}"
        )
    if output_dir.name != recipe.asset_directory:
        raise RuntimeError("terrain source preset output directory must match its id")
    actual_revision = common.git_revision(reference_root)
    if actual_revision != common.CODE_REVISION:
        raise RuntimeError(
            f"terrain-diffusion checkout must be {common.CODE_REVISION}, "
            f"got {actual_revision}"
        )
    if not torch.cuda.is_available():
        raise RuntimeError("Terrain source preset generation requires CUDA")

    marker = generation_marker_path(output_dir)
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    if marker.exists():
        raise RuntimeError(f"terrain source preset is already generating: {marker}")
    marker.write_text(
        json.dumps(
            {"schema": BUNDLE_SCHEMA, "id": recipe.id, "pid": os.getpid()},
            sort_keys=True,
        )
        + "\n"
    )

    temporary = None
    try:
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
            common._seed_process_rngs(torch, recipe.seed)
            world = WorldPipeline.from_pretrained(
                str(snapshot),
                seed=recipe.seed,
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
                selection = _landscape_selection(common, recipe)
                generated = common._generate_landscape_region(world, torch, selection)
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
                "process_rng_seeding": "seed-value-v1",
                "climate_output": True,
                "source_preset": recipe.id,
                "direct_pinned_region": True,
            },
        }

        temporary = Path(
            tempfile.mkdtemp(
                prefix=f"{output_dir.name}.tmp.",
                dir=output_dir.parent,
            )
        )
        record = common._write_landscape_variation_region(
            temporary, generated, common_source
        )
        actual_hashes = (
            record.get("elevation_sha256"),
            record.get("climate_sha256"),
        )
        expected_hashes = (
            recipe.expected_elevation_sha256,
            recipe.expected_climate_sha256,
        )
        if actual_hashes != expected_hashes:
            raise RuntimeError(
                f"terrain source preset {recipe.id} payload changed: "
                f"expected {expected_hashes}, got {actual_hashes}"
            )

        generation_seconds = time.perf_counter() - generation_start
        region = temporary / recipe.id
        bundle = {
            "schema": BUNDLE_SCHEMA,
            "id": recipe.id,
            "label": recipe.label,
            "tier": recipe.tier,
            "source_study": recipe.source_study,
            "catalog_sha256": common.sha256_file(catalog.path),
            "seed": recipe.seed,
            "selection": selection.as_json(),
            "elevation_sha256": recipe.expected_elevation_sha256,
            "climate_sha256": recipe.expected_climate_sha256,
            "generation": {
                "method": "direct-pinned-region-v1",
                "seconds": generation_seconds,
                "limit_seconds": OPTIONAL_GENERATION_LIMIT_SECONDS,
                "scan_performed": False,
                "probe_performed": False,
            },
        }
        (region / "preset.json").write_text(
            json.dumps(bundle, indent=2, sort_keys=True, allow_nan=False) + "\n"
        )
        if generation_seconds > OPTIONAL_GENERATION_LIMIT_SECONDS:
            raise RuntimeError(
                f"terrain source preset generation took {generation_seconds:.3f} "
                f"seconds, over the {OPTIONAL_GENERATION_LIMIT_SECONDS:.0f}-second limit"
            )
        if not validate_source_preset_asset(common, catalog.path, recipe.id, region):
            raise RuntimeError("generated terrain source preset failed validation")

        if output_dir.exists():
            shutil.rmtree(output_dir)
        os.replace(region, output_dir)
        shutil.rmtree(temporary)
        temporary = None
    finally:
        if temporary is not None:
            shutil.rmtree(temporary, ignore_errors=True)
        marker.unlink(missing_ok=True)

    print(f"terrain source preset {recipe.id}: wrote {output_dir}")
