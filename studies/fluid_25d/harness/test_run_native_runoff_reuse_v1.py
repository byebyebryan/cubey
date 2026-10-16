#!/usr/bin/env python3
"""Portable, GPU-free tests for the native runoff reuse control harness."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import numpy as np

import run_native_runoff_reuse_v1 as runoff


class NativeRunoffReuseTests(unittest.TestCase):
    def test_ascii_roundtrip_keeps_north_first_row_and_nodata_mask(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "fixture.asc"
            values = np.asarray([[1.25, 2.5], [3.75, runoff.NODATA]], dtype=np.float64)
            runoff.write_ascii(path, values, cellsize_m=10.0)
            header, actual, valid = runoff.read_ascii(path)
            self.assertEqual(actual.shape, (2, 2))
            self.assertTrue(np.array_equal(actual, values))
            self.assertTrue(np.array_equal(valid, [[True, True], [True, False]]))
            self.assertEqual(header["cellsize"], 10.0)
            self.assertEqual(actual[0, 0], 1.25, "row 0 must not be flipped")

    def test_frozen_control_dimensions_and_physical_measurement_interiors(self):
        specs = runoff.control_specs()
        self.assertEqual(len(specs), 9)
        flat = [spec for spec in specs if spec["family"] == "flat-rain"]
        self.assertEqual([spec["bed_datum_m"] for spec in flat], [0.0, 2000.0])
        sheets = [spec for spec in specs if spec["family"] == "thin-sheet"]
        self.assertEqual([spec["slope"] for spec in sheets[:5]], list(runoff.SHEET_SLOPES))
        self.assertEqual(int(runoff.sheet_measure_mask(sheets[0]).sum()), 768)
        self.assertEqual(int(runoff.sheet_measure_mask(sheets[5]).sum()), 88)
        self.assertEqual(int(runoff.sheet_measure_mask(sheets[6]).sum()), 768)
        self.assertEqual(runoff.bed_grid(sheets[-1])[0, 0], 999.5)
        self.assertLess(runoff.bed_grid(sheets[-1])[-1, 0], runoff.bed_grid(sheets[-1])[0, 0])

    def test_frozen_mountain_crop_roi_masks_and_refinement_triangles(self):
        fixture, crop, halo, identity = runoff.mountain_source_arrays()
        self.assertEqual(fixture["transformed_crop_sha256"], identity["transformed_crop_sha256"])
        self.assertEqual(crop.shape, (512, 512))
        self.assertEqual(halo.shape, (514, 514))
        self.assertEqual(identity["crop_xzwh"], [1152, 1408, 512, 512])
        rois = runoff.fixed_roi_masks()
        self.assertEqual({name: int(mask.sum()) for name, mask in rois.items()},
                         {"upper": 441, "transit": 441, "collection": 441})
        self.assertTrue(rois["upper"][203, 203])
        self.assertFalse(rois["upper"][202, 203])
        depressions, dep_identity = runoff.fixed_depression_masks()
        self.assertEqual(dep_identity["labels_sha256"],
                         "a5dc0de3e680f5d142649158df8377cf009d845c28a190b1b5c6de0567270a3f")
        self.assertEqual({name: int(mask.sum()) for name, mask in depressions.items()},
                         {"label-20": 984, "label-172": 229, "label-119": 150})
        self.assertEqual(dep_identity["fixed_boolean_mask_sha256"], {
            "20": "3ec05f7ebddd1704bb16a8297176cc4cac91cbf493a0e859d89037a115821b41",
            "172": "a41d9dea45ad22943e2a5a3ca05ec001d9172c3fb28bb9e4defe8ad534ab67d5",
            "119": "f807c1c67a32a7a4c735e2e6a5d2498e18f70e3b07a637ea12eed993f856f5ba",
        })

        specs = runoff.mountain_case_specs()
        self.assertEqual(specs["probe"]["output_interval_s"], 30)
        self.assertEqual(specs["full"]["output_interval_s"], 60)
        self.assertEqual(specs["replay"]["output_interval_s"], 60)
        self.assertEqual(specs["refinement"]["output_interval_s"], 60)
        self.assertTrue(all(spec["rain_history_end_s"] == 7200 for spec in specs.values()))

        first_triangle = np.zeros((514, 514), dtype=np.float32)
        first_triangle[102, 101] = 1.0
        first_refined = runoff.piecewise_linear_refine_2x(first_triangle)
        self.assertEqual(first_refined.shape, (1024, 1024))
        self.assertEqual(first_refined.dtype, np.dtype("<f8"))
        self.assertAlmostEqual(float(first_refined[201, 200]), 0.25, places=6)
        second_triangle = np.zeros((514, 514), dtype=np.float32)
        second_triangle[101, 102] = 1.0
        second_refined = runoff.piecewise_linear_refine_2x(second_triangle)
        self.assertAlmostEqual(float(second_refined[200, 201]), 0.25, places=6)

    def test_d4_component_connectivity_and_mountain_metrics_keep_export_speed_caveats(self):
        diagonal = np.eye(2, dtype=bool)
        self.assertEqual([part.tolist() for part in runoff.d4_components(diagonal)], [[0], [3]])
        adjacent = np.asarray([[1, 1], [0, 1]], dtype=bool)
        self.assertEqual([part.tolist() for part in runoff.d4_components(adjacent)], [[0, 1, 3]])
        _, bed, _, _ = runoff.mountain_source_arrays()
        depression_masks = {"label-20": np.zeros((512, 512), dtype=bool)}
        depression_masks["label-20"][10:12, 10:13] = True
        spec = runoff.mountain_case_specs()["probe"]
        depth = np.zeros((512, 512), dtype=np.float64)
        hux = np.zeros_like(depth)
        huy = np.zeros_like(depth)
        depth[10:12, 10:13] = 0.01
        hux[0, 0] = 0.001  # h==0 momentum must be counted, never assigned zero speed.
        for offset in range(11):
            depth[20, 20 + offset] = 0.03
            hux[20, 20 + offset] = 0.03 * 0.03
        snapshots = {
            time_s: {"h": depth.copy(), "hUx": hux.copy(), "hUy": huy.copy()}
            for time_s in (0.0, 30.0)
        }
        metrics = runoff.mountain_metrics(spec, snapshots, bed, np.zeros_like(bed),
                                          runoff.fixed_roi_masks(), depression_masks)
        self.assertEqual(metrics["status"], "healthy")
        self.assertGreater(metrics["global_observations"][0]["h_zero_nonzero_hU_count"], 0)
        self.assertGreater(metrics["global_observations"][0]["maximum_export_derived_speed_m_per_s"], 0.02)
        self.assertTrue(metrics["corridor_900s_support_criterion_met"] is False)
        self.assertTrue(metrics["moving_concentrated_corridor_observations"][1]["any_component_span_ge_300m"])
        self.assertFalse(metrics["pond_15min_criteria_met_by_frozen_depression"]["label-20"])
        storage = metrics["fixed_depression_net_rain_observations"]["label-20"]
        self.assertEqual([item["time_s"] for item in storage], [0.0, 30.0])
        self.assertEqual(storage[1]["raster_cell_count"], 6)
        self.assertEqual(storage[1]["equivalent_30m_cell_count"], 6)
        self.assertEqual(storage[1]["direct_local_rain_volume_m3"],
                         runoff.MOUNTAIN_RAIN_RATE_M_PER_S * 30.0 * 6 * 30.0 * 30.0)
        self.assertAlmostEqual(storage[1]["output_quantization_interval_m3"][0], -0.0027)
        self.assertAlmostEqual(storage[1]["output_quantization_interval_m3"][1], 0.0027)

    def test_15m_area_masks_and_fine_pond_area_threshold_are_physical(self):
        spec = runoff.mountain_case_specs()["refinement"]
        bed = np.zeros((1024, 1024), dtype=np.float64)
        depth = np.zeros_like(bed)
        hux = np.zeros_like(bed)
        huy = np.zeros_like(bed)
        base_mask = np.zeros((512, 512), dtype=bool)
        base_mask[10:12, 10:12] = True
        depth[20:24, 20:24] = 0.01  # 16 fine cells = four physical 30m cells.
        metrics = runoff.mountain_metrics(
            spec,
            {600.0: {"h": depth, "hUx": hux, "hUy": huy}},
            bed,
            bed,
            runoff.fixed_roi_masks(),
            {"label-20": base_mask},
        )
        storage = metrics["fixed_depression_net_rain_observations"]["label-20"][0]
        self.assertEqual(storage["equivalent_30m_cell_count"], 4)
        self.assertEqual(storage["raster_cell_count"], 16)
        self.assertEqual(storage["physical_mask_area_m2"], 3600.0)
        self.assertAlmostEqual(storage["output_quantization_interval_m3"][1], 0.0018)
        pond = metrics["pond_observations_by_frozen_depression"]["label-20"][0]
        self.assertTrue(pond["any_quiet_near_level_candidate"])
        self.assertIn("refinement-only diagnostic", pond["candidate_classification_scope"])
        self.assertEqual(pond["components"][0]["minimum_candidate_area_m2"], 3600.0)
        self.assertIsNone(pond["15min_candidate_support_observed"])

    def test_2x_area_aggregation_is_h_q_arithmetic_mean(self):
        fine = np.zeros((1024, 1024), dtype=np.float64)
        fine[0:2, 0:2] = [[1.0, 3.0], [5.0, 7.0]]
        coarse = runoff.aggregate_2x2_area_mean(fine)
        self.assertEqual(coarse.shape, (512, 512))
        self.assertEqual(coarse[0, 0], 4.0)
        self.assertEqual(float(coarse.sum()), 4.0)

    def test_replay_comparison_requires_exact_bytes_and_arrays(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            full_root = root / "full"
            replay_root = root / "replay"
            for case_root, times in ((full_root, (0, 60, 120)), (replay_root, (0, 60))):
                output = case_root / "native" / "output"
                output.mkdir(parents=True)
                for time_s in times:
                    for field in ("h", "hUx", "hUy"):
                        runoff.write_ascii(output / f"{field}_{time_s}.asc",
                                           np.full((2, 2), float(time_s)), 30.0)
            full_record = {"working_directory": str(full_root), "status": "healthy"}
            replay_record = {"working_directory": str(replay_root), "status": "healthy"}
            exact = runoff.compare_mountain_replay(full_record, replay_record)
            self.assertTrue(exact["all_h_hUx_hUy_exact_array_and_file_equality"])
            self.assertEqual(exact["compared_export_count"], 6)
            runoff.write_ascii(replay_root / "native" / "output" / "h_60.asc",
                               np.full((2, 2), 60.000001), 30.0)
            mismatch = runoff.compare_mountain_replay(full_record, replay_record)
            self.assertFalse(mismatch["all_h_hUx_hUy_exact_array_and_file_equality"])
            self.assertEqual(mismatch["first_mismatch"], {"time_s": 60.0, "field": "h"})

    def test_flat_analytic_gate_accepts_roundable_profile_and_rejects_depth_error(self):
        spec = runoff.control_specs()[0]
        snapshots = {}
        for timestamp in runoff.time_grid(spec):
            depth = np.full((32, 32), runoff.RAIN_RATE_M_PER_S * timestamp, dtype=np.float64)
            snapshots[timestamp] = {
                "h": depth,
                "hUx": np.zeros_like(depth),
                "hUy": np.zeros_like(depth),
                "h_zero_nonzero_hU_count": 0,
            }
        self.assertTrue(runoff.flat_metrics(spec, snapshots)["gate_passed"])
        snapshots[30.0]["h"][0, 0] += 1.0e-6
        self.assertFalse(runoff.flat_metrics(spec, snapshots)["gate_passed"])

    def test_output_name_and_timestep_log_health(self):
        spec = runoff.control_specs()[0]
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            for timestamp in runoff.time_grid(spec):
                for field in ("h", "hUx", "hUy"):
                    runoff.write_ascii(output / f"{field}_{timestamp:g}.asc",
                                       np.zeros((32, 32)), 10.0)
            paths = runoff.snapshot_paths(output)
            self.assertEqual(sorted(paths["h"]), runoff.time_grid(spec))
            log = output / "timestep_log.txt"
            log.write_text("30 30\n60 30\n90 30\n120 30\n150 30\n180 30\n210 30\n240 30\n270 30\n300 30\n330 30\n360 30\n390 30\n420 30\n450 30\n480 30\n510 30\n540 30\n570 30\n600 0\n", encoding="ascii")
            metrics = runoff.parse_timestep_log(log, 600)
            self.assertEqual(metrics["rows"], 20)
            self.assertEqual(metrics["derived_consumed_dt_min_positive_s"], 30)
            self.assertEqual(metrics["derived_consumed_dt_max_positive_s"], 30)
            self.assertEqual(metrics["terminal_zero_next_dt_rows"], 1)

            rounded = output / "rounded-timestep-log.txt"
            rounded.write_text("0.000001 0.2\n0.000001 0.2\n600 0\n", encoding="ascii")
            rounded_metrics = runoff.parse_timestep_log(rounded, 600)
            self.assertEqual(rounded_metrics["rounded_duplicate_current_time_rows"], 1)
            self.assertIn("not proof of native stagnation", rounded_metrics["derived_consumed_dt_precision"])

    def test_sheet_rotated_sign_mapping_and_volume_gate_cover_all_saved_frames(self):
        spec = [case for case in runoff.control_specs() if case["orientation"] == "world-z-row-downhill"][0]
        reference_u = runoff.SHEET_H_M ** (2.0 / 3.0) * np.sqrt(spec["slope"]) / runoff.SHEET_N
        snapshots = {}
        for timestamp in runoff.time_grid(spec):
            depth = np.full((spec["rows"], spec["cols"]), runoff.SHEET_H_M)
            hux = np.zeros_like(depth)
            huy = np.full_like(depth, -runoff.SHEET_H_M * reference_u)
            snapshots[timestamp] = {"h": depth, "hUx": hux, "hUy": huy, "h_zero_nonzero_hU_count": 0}
        metrics = runoff.sheet_metrics(spec, snapshots)
        self.assertTrue(metrics["gate_passed"])
        self.assertTrue(metrics["orientation_sign_gate_passed"])
        self.assertLess(metrics["observations"][0]["native_downhill_momentum_mean_m2_per_s"], 0)
        self.assertGreater(metrics["observations"][0]["mapped_worldz_velocity_mean_m_per_s"], 0)
        self.assertEqual(len(metrics["closed_volume_observations_all_saved_times"]), 13)

        wrong_sign = {
            timestamp: {key: value.copy() if isinstance(value, np.ndarray) else value for key, value in fields.items()}
            for timestamp, fields in snapshots.items()
        }
        for fields in wrong_sign.values():
            fields["hUy"] *= -1
        wrong_sign_metrics = runoff.sheet_metrics(spec, wrong_sign)
        self.assertFalse(wrong_sign_metrics["gate_passed"])
        self.assertFalse(wrong_sign_metrics["orientation_sign_gate_passed"])
        self.assertGreater(wrong_sign_metrics["maximum_relative_normal_velocity_error"], 1.0)

        # A volume failure at an unmeasured export time must still fail the contract.
        snapshots[25.0]["h"] += 1.0e-6
        failed = runoff.sheet_metrics(spec, snapshots)
        self.assertFalse(failed["gate_passed"])
        frame_25s = next(item for item in failed["closed_volume_observations_all_saved_times"] if item["time_s"] == 25.0)
        self.assertFalse(frame_25s["gate_passed"])

    def test_fresh_output_leaf_refuses_overwrite_and_escape(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            existing = root / "existing"
            existing.mkdir()
            with self.assertRaises(FileExistsError):
                runoff.create_fresh_leaf(root, existing)
            with self.assertRaises(runoff.RunoffError):
                runoff.create_fresh_leaf(root, root.parent / "outside")

    def test_released_inputmodel_serializes_forcing_sinks_rigid_boundary_and_times(self):
        try:
            import synxflow  # noqa: F401
        except ImportError:
            self.skipTest("SynxFlow is available only in the isolated 3.11 setup environment")
        spec = runoff.control_specs()[0]
        with tempfile.TemporaryDirectory() as temp:
            case = Path(temp) / "flat-case"
            case.mkdir()
            runoff.write_ascii(case / "DEM.asc", runoff.bed_grid(spec), spec["dx_m"])
            hashes = runoff._make_model_inputs(spec, case)
            self.assertTrue(hashes)
            field = case / "native" / "input" / "field"
            self.assertEqual((case / "native" / "input" / "times_setup.dat").read_text().split(),
                             ["0", "600", "30", "600"])
            source = np.loadtxt(field / "precipitation_source_all.dat", skiprows=1, ndmin=2)
            self.assertTrue(np.allclose(source[:, 0], [0, 600]))
            self.assertTrue(np.allclose(source[:, 1], [runoff.RAIN_RATE_M_PER_S] * 2, rtol=1e-7))
            self.assertEqual(np.unique(runoff._element_values(field / "precipitation_mask.dat")).tolist(), [0.0])
            self.assertEqual(np.unique(runoff._element_values(field / "manning.dat")).tolist(), [0.05])
            for name in ("sewer_sink", "cumulative_depth", "hydraulic_conductivity",
                         "capillary_head", "water_content_diff"):
                self.assertEqual(np.unique(runoff._element_values(field / f"{name}.dat")).tolist(), [0.0])
            h_codes = runoff._serialized_boundary_codes(field / "h.dat")
            hu_codes = runoff._serialized_boundary_codes(field / "hU.dat")
            self.assertTrue(h_codes)
            self.assertTrue(hu_codes)
            self.assertTrue(all(code == (2, 0, 0) for code in h_codes))
            self.assertTrue(all(code == (2, 2, 0) for code in hu_codes))

    def test_released_inputmodel_serializes_fall_boundary_and_bottom_up_z_ids(self):
        try:
            import synxflow  # noqa: F401
        except ImportError:
            self.skipTest("SynxFlow is available only in the isolated 3.11 setup environment")
        spec = {
            "name": "small-asymmetric-fall-test", "family": "mountain-rain",
            "rows": 4, "cols": 5, "dx_m": 30.0, "duration_s": 60,
            "output_interval_s": 30, "manning_n": 0.05,
            "rain_rate_m_per_s": runoff.MOUNTAIN_RAIN_RATE_M_PER_S,
            "initial_depth_m": 0.0, "boundary": "fall",
        }
        bed = np.asarray([[1000.0, 1001.0, 1002.0, 1003.0, 1004.0],
                          [1010.0, 1011.0, 1012.0, 1013.0, 1014.0],
                          [1020.0, 1021.0, 1022.0, 1023.0, 1024.0],
                          [1030.0, 1031.0, 1032.0, 1033.0, 1034.0]])
        with tempfile.TemporaryDirectory() as temp:
            case = Path(temp) / "fall-case"
            case.mkdir()
            runoff.write_ascii(case / "DEM.asc", bed, spec["dx_m"])
            runoff._make_model_inputs(spec, case)
            audit = runoff._native_input_audit(spec, case)
            field = case / "native" / "input" / "field"
            h_codes = runoff._serialized_boundary_codes(field / "h.dat")
            hu_codes = runoff._serialized_boundary_codes(field / "hU.dat")
            self.assertTrue(all(code == (3, 0, 0) for code in h_codes))
            self.assertTrue(all(code == (3, 0, 0) for code in hu_codes))
            serialized_grid, native_grid = runoff._grid_from_native_ids(field / "z.dat", 4, 5)
            self.assertEqual(serialized_grid[0, 0], bed[0, 0])
            self.assertEqual(serialized_grid[-1, 0], bed[-1, 0])
            self.assertTrue(np.array_equal(native_grid, bed.astype("<f4")))
            self.assertEqual(audit["declared_boundary"], "fall")
            self.assertEqual(audit["native_id_mapping"].split(":")[0], "released InputModel indep_functions._get_cell_id_array")


if __name__ == "__main__":
    unittest.main()
