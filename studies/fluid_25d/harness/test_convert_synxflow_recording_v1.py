"""Focused tests for the portable SynxFlow recording adapter."""

from __future__ import annotations

from array import array
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from convert_synxflow_recording_v1 import (
    RecordingConversionError,
    convert_case,
)


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _write_asc(path: Path, values: list[float], width: int = 2, height: int = 2) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    body = []
    for row in range(height):
        start = row * width
        body.append(" ".join(f"{value:.6f}" for value in values[start : start + width]))
    path.write_text(
        "\n".join(
            [
                f"ncols {width}",
                f"nrows {height}",
                "xllcorner 0.0",
                "yllcorner 0.0",
                "cellsize 10.0",
                "NODATA_value -9999.0",
                *body,
                "",
            ]
        ),
        encoding="ascii",
    )


def _make_case(root: Path) -> Path:
    case = root / "case"
    (case / "native/input/field").mkdir(parents=True)
    output = case / "native/output"
    output.mkdir(parents=True)
    spec = {
        "boundary": "fall",
        "cols": 2,
        "duration_s": 2,
        "dx_m": 10.0,
        "initial_depth_m": 0.0,
        "manning_n": 0.05,
        "name": "asymmetric-fixture",
        "orientation": "world-z-row-downhill",
        "output_interval_s": 1,
        "rain_rate_m_per_s": 0.000001,
        "rain_history_end_s": 2,
        "rows": 2,
    }
    (case / "case-spec.json").write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
    _write_asc(case / "DEM.asc", [1.0, 2.0, 3.0, 4.0])
    native_bed = case / "native/input/field/z.dat"
    native_bed.write_text(
        "$Element Number\n4\n$Element_id  Value\n"
        "0 101.001\n1 102.002\n2 103.003\n3 104.004\n"
        "$Boundary Numbers\n4\n$Element_id  Value\n"
        "0 2 0 0\n1 2 0 0\n2 2 0 0\n3 2 0 0\n",
        encoding="ascii",
    )
    protocol = {
        "case": spec,
        "expected_saved_times_s": [0.0, 1.0, 2.0],
        "input_dem_ascii_sha256": _sha256(case / "DEM.asc"),
        "terrain_source_identity": {
            "crop_cell_size_m": 10,
            "crop_xzwh": [0, 0, 2, 2],
            "elevation_dtype": "little-endian float32",
            "elevation_sha256": "1" * 64,
            "elevation_shape": [2, 2],
            "fixture_sha256": "2" * 64,
            "manifest_sha256": "3" * 64,
            "physical_extent_m": [20, 20],
            "row_orientation": "row zero is low source world-Z",
            "transform": "float32 ((raw_float32 + float32(offset_m))*float32(scale))",
            "transformed_crop_dtype": "little-endian float32",
            "transformed_crop_sha256": "4" * 64,
            "transformed_crop_shape": [2, 2],
            "transformed_halo_sha256": "5" * 64,
            "transformed_halo_shape": [4, 4],
        },
    }
    (case / "case-protocol.json").write_text(
        json.dumps(protocol, indent=2) + "\n", encoding="utf-8"
    )
    result = {
        "case": spec,
        "input_provenance": {
            "case_dem_ascii_sha256": _sha256(case / "DEM.asc"),
            "case_source_bed_semantics": "fixture float32 point crop",
            "native_input_audit_before_solver": {
                "z_element_count": 4,
                "z_field_sha256": _sha256(native_bed),
            },
        },
    }
    (case / "case-result.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8"
    )

    _write_asc(output / "h_0.asc", [0.0, 0.0, 0.0, 0.0])
    _write_asc(output / "hUx_0.asc", [0.0, 0.0, 0.0, 0.0])
    _write_asc(output / "hUy_0.asc", [0.0, 0.0, 0.0, 0.0])
    _write_asc(output / "h_1.asc", [0.1, 0.0, 0.2, 0.4])
    _write_asc(output / "hUx_1.asc", [0.02, 0.0, 0.4, 0.6])
    _write_asc(output / "hUy_1.asc", [-0.03, 0.0, 0.2, 0.1])
    _write_asc(output / "h_2.asc", [0.0, 0.3, 0.2, 0.1])
    _write_asc(output / "hUx_2.asc", [0.0, -0.03, 0.02, 0.0])
    _write_asc(output / "hUy_2.asc", [0.0, 0.02, -0.01, 0.0])
    return case


def _snapshot(case: Path) -> dict[str, str]:
    return {
        path.relative_to(case).as_posix(): _sha256(path)
        for path in sorted(case.rglob("*"))
        if path.is_file()
    }


def _set_rainfall_history(case: Path, history: list[list[float]]) -> None:
    spec_path = case / "case-spec.json"
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    spec["rainfall_history"] = history
    spec_path.write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")

    protocol_path = case / "case-protocol.json"
    protocol = json.loads(protocol_path.read_text(encoding="utf-8"))
    protocol["case"] = spec
    protocol["settings"] = {"rain_source_schedule": history}
    protocol_path.write_text(json.dumps(protocol, indent=2) + "\n", encoding="utf-8")

    source_path = case / "native/input/field/precipitation_source_all.dat"
    source_bytes = ("1\n" + "".join(
        f"{time_s:.12g} {rate:.12g}\n" for time_s, rate in history
    )).encode("ascii")
    source_path.write_bytes(source_bytes)
    result_path = case / "case-result.json"
    result = json.loads(result_path.read_text(encoding="utf-8"))
    result["case"] = spec
    audit = result["input_provenance"]["native_input_audit_before_solver"]
    audit["serialized_rainfall_history"] = history
    audit["rain_source_sha256"] = hashlib.sha256(source_bytes).hexdigest()
    result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


class SynxFlowRecordingConverterTests(unittest.TestCase):
    def test_asymmetric_axis_mapping_precision_and_inputs_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            before = _snapshot(case)
            manifest_path = convert_case(case, root / "converted")
            after = _snapshot(case)
            self.assertEqual(before, after)

            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["schema"], "cubey.fluid25d.recording.v1")
            self.assertEqual(manifest["encoding"], "float32-little-endian")
            self.assertEqual(manifest["fields"], ["h_m", "qx_m2_per_s", "qz_m2_per_s"])
            self.assertEqual(manifest["protocol"]["name"], "asymmetric-fixture")
            self.assertEqual(
                manifest["protocol"],
                json.loads((case / "case-spec.json").read_text(encoding="utf-8")),
            )
            self.assertNotIn("rainfall_history_proof", manifest["provenance"])
            self.assertEqual(manifest["grid"]["row_direction"], "world-z-positive")
            self.assertEqual([entry["time_s"] for entry in manifest["frames"]], [0, 1, 2])

            recording_dir = manifest_path.parent
            bed = array("f")
            bed.frombytes((recording_dir / "bed.f32").read_bytes())
            self.assertEqual(list(bed), list(array("f", [103.003, 104.004, 101.001, 102.002])))
            source_bed = array("f")
            source_bed.frombytes((recording_dir / "source-bed.f32").read_bytes())
            self.assertEqual(list(source_bed), [1.0, 2.0, 3.0, 4.0])

            frame_path = recording_dir / manifest["frames"][1]["path"]
            frame = array("f")
            frame.frombytes(frame_path.read_bytes())
            self.assertEqual(list(frame[:4]), list(array("f", [0.1, 0.0, 0.2, 0.4])))
            self.assertEqual(list(frame[4:8]), list(array("f", [0.02, 0.0, 0.4, 0.6])))
            self.assertEqual(list(frame[8:]), list(array("f", [0.03, 0.0, -0.2, -0.1])))
            self.assertEqual(manifest["frames"][1]["dry_nonzero_momentum_cells"], 0)
            self.assertGreater(manifest["frames"][1]["max_speed_m_per_s"], 0.0)
            self.assertEqual(
                manifest["provenance"]["precision"]["native_output_ascii_decimal_places"], 6
            )
            self.assertAlmostEqual(
                manifest["provenance"]["precision"][
                    "native_bed_vs_source_bed_max_abs_error_m"
                ],
                102.004,
                places=4,
            )
            self.assertEqual(
                manifest["provenance"]["raw_asc_sha256"][1]["hUx"],
                _sha256(case / "native/output/hUx_1.asc"),
            )
            self.assertEqual(
                manifest["provenance"]["source_sha256"]["native_numerical_bed"],
                _sha256(case / "native/input/field/z.dat"),
            )
            self.assertEqual(
                manifest["provenance"]["source_sha256"]["case_result"],
                _sha256(case / "case-result.json"),
            )

    def test_refuses_existing_output_without_changing_it(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            output = root / "already-there"
            output.mkdir()
            marker = output / "keep.txt"
            marker.write_text("preserve", encoding="utf-8")
            before = _snapshot(case)
            with self.assertRaises(RecordingConversionError):
                convert_case(case, output)
            self.assertEqual(marker.read_text(encoding="utf-8"), "preserve")
            self.assertEqual(before, _snapshot(case))

    def test_rejects_missing_or_mismatched_timestamps(self) -> None:
        for name in ("hUy_1.asc", "hUx_2.asc"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                case = _make_case(root)
                (case / "native/output" / name).unlink()
                with self.assertRaises(RecordingConversionError):
                    convert_case(case, root / "converted")
                self.assertFalse((root / "converted").exists())

    def test_rejects_mismatched_raster_geometry(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            path = case / "native/output/hUx_1.asc"
            path.write_text(
                path.read_text(encoding="ascii").replace("ncols 2", "ncols 1", 1),
                encoding="ascii",
            )
            with self.assertRaises(RecordingConversionError):
                convert_case(case, root / "converted")
            self.assertFalse((root / "converted").exists())

    def test_rejects_unsupported_timestamp_spelling_and_oversized_cadence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            output = case / "native/output"
            (output / "h_01.asc").write_bytes((output / "h_1.asc").read_bytes())
            with self.assertRaisesRegex(RecordingConversionError, "unsupported field timestamp"):
                convert_case(case, root / "bad-timestamp")
            self.assertFalse((root / "bad-timestamp").exists())

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            spec_path = case / "case-spec.json"
            spec = json.loads(spec_path.read_text(encoding="utf-8"))
            spec["duration_s"] = 100000
            spec_path.write_text(json.dumps(spec), encoding="utf-8")
            with self.assertRaisesRegex(RecordingConversionError, "frame limit"):
                convert_case(case, root / "too-many-frames")
            self.assertFalse((root / "too-many-frames").exists())

    def test_rejects_viewer_unsupported_single_cell_dimension(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            spec_path = case / "case-spec.json"
            spec = json.loads(spec_path.read_text(encoding="utf-8"))
            spec["cols"] = 1
            spec_path.write_text(json.dumps(spec), encoding="utf-8")
            with self.assertRaisesRegex(RecordingConversionError, "dimensions must both be at least two"):
                convert_case(case, root / "single-cell-width")

    def test_rejects_dry_nonzero_momentum(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            _write_asc(case / "native/output/h_1.asc", [0.0, 0.0, 0.0, 0.0])
            with self.assertRaisesRegex(RecordingConversionError, "zero depth with nonzero momentum"):
                convert_case(case, root / "converted")
            self.assertFalse((root / "converted").exists())

    def test_rejects_bad_numerical_bed_ids_and_precision_underflow(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            z_path = case / "native/input/field/z.dat"
            z_path.write_text(
                "$Element Number\n4\n$Element_id  Value\n"
                "1 101.001\n0 102.002\n2 103.003\n3 104.004\n"
                "$Boundary Numbers\n4\n$Element_id  Value\n"
                "0 2 0 0\n1 2 0 0\n2 2 0 0\n3 2 0 0\n",
                encoding="ascii",
            )
            result_path = case / "case-result.json"
            result = json.loads(result_path.read_text(encoding="utf-8"))
            result["input_provenance"]["native_input_audit_before_solver"]["z_field_sha256"] = (
                _sha256(z_path)
            )
            result_path.write_text(json.dumps(result), encoding="utf-8")
            with self.assertRaisesRegex(RecordingConversionError, "IDs must be sequential"):
                convert_case(case, root / "converted")

        with self.assertRaisesRegex(RecordingConversionError, "underflows float32"):
            from convert_synxflow_recording_v1 import _as_float32

            _as_float32([1.0e-100], "fixture")

    def test_rejects_duplicate_boundary_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            z_path = case / "native/input/field/z.dat"
            z_path.write_text(
                z_path.read_text(encoding="ascii").replace("3 2 0 0", "2 2 0 0"),
                encoding="ascii",
            )
            result_path = case / "case-result.json"
            result = json.loads(result_path.read_text(encoding="utf-8"))
            result["input_provenance"]["native_input_audit_before_solver"]["z_field_sha256"] = (
                _sha256(z_path)
            )
            result_path.write_text(json.dumps(result), encoding="utf-8")
            with self.assertRaisesRegex(RecordingConversionError, "IDs are invalid or repeated"):
                convert_case(case, root / "converted")

    def test_optional_recorded_rainfall_history_is_proved_and_integrated(self) -> None:
        history = [[0.0, 1.0e-6], [1.0, 1.0e-6], [1.5, 0.0], [2.0, 0.0]]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            _set_rainfall_history(case, history)
            before = _snapshot(case)
            manifest_path = convert_case(case, root / "converted")
            self.assertEqual(before, _snapshot(case))
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            proof = manifest["provenance"]["rainfall_history_proof"]
            self.assertEqual(manifest["protocol"]["rainfall_history"], history)
            self.assertEqual(proof["history_m_per_s"], history)
            self.assertEqual(
                proof["native_source_sha256"],
                _sha256(case / "native/input/field/precipitation_source_all.dat"),
            )
            self.assertAlmostEqual(
                proof["piecewise_linear_cumulative_scheduled_rain_mm"], 0.00125, places=12
            )
            self.assertTrue(proof["not_a_measured_water_balance"])

    def test_malformed_or_unproved_rainfall_history_is_rejected(self) -> None:
        malformed = (
            [],
            [[1.0, 1.0e-6], [2.0, 0.0]],
            [[0.0, 1.0e-6], [1.0, -1.0], [2.0, 0.0]],
            [[0.0, 1.0e-6], [1.0, 0.0], [1.0, 0.0], [2.0, 0.0]],
            [[0.0, 2.0e-6], [2.0, 0.0]],
            [[0.0, 1.0e-6], [1.0, 0.0]],
            [[0.0, 1.0e-6], [1.0, float("nan")], [2.0, 0.0]],
        )
        for history in malformed:
            with self.subTest(history=history), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                case = _make_case(root)
                _set_rainfall_history(case, history)
                with self.assertRaises(RecordingConversionError):
                    convert_case(case, root / "converted")
                self.assertFalse((root / "converted").exists())

        for proof_field in ("protocol", "audit_history", "audit_hash", "serialized"):
            with self.subTest(proof_field=proof_field), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                case = _make_case(root)
                history = [[0.0, 1.0e-6], [1.0, 1.0e-6], [1.5, 0.0], [2.0, 0.0]]
                _set_rainfall_history(case, history)
                protocol_path = case / "case-protocol.json"
                result_path = case / "case-result.json"
                protocol = json.loads(protocol_path.read_text(encoding="utf-8"))
                result = json.loads(result_path.read_text(encoding="utf-8"))
                audit = result["input_provenance"]["native_input_audit_before_solver"]
                if proof_field == "protocol":
                    protocol["settings"]["rain_source_schedule"][1][1] = 0.0
                    protocol_path.write_text(json.dumps(protocol), encoding="utf-8")
                elif proof_field == "audit_history":
                    audit["serialized_rainfall_history"][1][1] = 0.0
                    result_path.write_text(json.dumps(result), encoding="utf-8")
                elif proof_field == "audit_hash":
                    audit["rain_source_sha256"] = "0" * 64
                    result_path.write_text(json.dumps(result), encoding="utf-8")
                else:
                    source_path = case / "native/input/field/precipitation_source_all.dat"
                    source_path.write_text(
                        "1\n0 1e-6\n1 1e-6\n1.5 0\n2 0\n", encoding="ascii"
                    )
                with self.assertRaises(RecordingConversionError):
                    convert_case(case, root / "converted")
                self.assertFalse((root / "converted").exists())

    def test_terminal_max_depth_auxiliary_is_hashed_not_treated_as_a_frame(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = _make_case(root)
            auxiliary = case / "native/output/h_max_2.asc"
            _write_asc(auxiliary, [0.1, 0.2, 0.3, 0.4])
            manifest_path = convert_case(case, root / "converted")
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual([entry["time_s"] for entry in manifest["frames"]], [0, 1, 2])
            [record] = manifest["provenance"]["native_auxiliary_outputs"]
            self.assertEqual(record["path"], "h_max_2.asc")
            self.assertEqual(record["sha256"], _sha256(auxiliary))
            self.assertIn("not a saved frame", record["role"])

        for name in ("h_max_02.asc", "h_max_3.asc", "h_max_2.txt", "h_max_2_extra.asc"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                case = _make_case(root)
                _write_asc(case / "native/output" / name, [0.1, 0.2, 0.3, 0.4])
                with self.assertRaisesRegex(RecordingConversionError, "max-depth auxiliary"):
                    convert_case(case, root / "converted")
                self.assertFalse((root / "converted").exists())


if __name__ == "__main__":
    unittest.main()
