from decimal import Decimal
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_hillside_rain_v1 as rain
import run_hillside_rain_demo as demo
import run_hillside_rain_pacing_v1 as pacing


def flat_profile(rate: Decimal = rain.RATES["main"], horizon: int = 120):
    frames = {}
    for frame in range(horizon // rain.DT):
        t = Decimal((frame + 1) * rain.DT)
        depth = rate * t / Decimal(3600000)
        source = depth * rain.AREA
        wet = Decimal(512 * 512 if depth >= Decimal("0.01") else 0)
        values = {(rain.WATER, key): Decimal(0) for key in rain.WATER_FIELDS}
        values.update({(rain.WATER, "cumulative_source_volume_m3"): source,
                       (rain.WATER, "total_water_volume_m3"): source,
                       rain.FLAGS: Decimal(0),
                       ("fluid_25d.hydraulic_identity", "hash_hi_u32"): Decimal(1),
                       ("fluid_25d.hydraulic_identity", "hash_lo_u32"): Decimal(frame)})
        values.update({(rain.RAIN, key): Decimal(0) for key in rain.RAIN_FIELDS})
        values.update({(rain.RAIN, "physical_time_s"): t,
                       (rain.RAIN, "rate_mm_per_hour"): rate,
                       (rain.RAIN, "total_input_m3_per_s"): rain.AREA * rate / Decimal(3600000),
                       (rain.RAIN, "cumulative_depth_m"): depth,
                       (rain.RAIN, "scheduled_volume_m3"): source,
                       (rain.RAIN, "enabled"): Decimal(1),
                       (rain.RAIN, "material_wet_cells"): wet})
        for region in rain.REGIONS:
            category = f"{rain.RAIN}.region.{region}"
            values.update({(category, key): Decimal(0) for key in rain.REGION_FIELDS})
            values.update({(category, "water_volume_m3"): depth * rain.ROI_AREA,
                           (category, "valid"): Decimal(1),
                           (category, "direct_rain_volume_m3"): depth * rain.ROI_AREA,
                           (category, "maximum_depth_m"): depth,
                           (category, "material_wet_cells"): Decimal(441 if wet else 0)})
        frames[frame] = values
    return frames


class RainContractTests(unittest.TestCase):
    def test_predefined_volume_budget(self):
        weak = rain.validate_profile(flat_profile(rain.RATES["equal-input"], 7200), rain.RATES["equal-input"], 7200)
        main = rain.validate_profile(flat_profile(horizon=7200), rain.RATES["main"], 7200)
        self.assertEqual(Decimal(weak["final_input_m3"]), Decimal(720000))
        self.assertEqual(Decimal(main["final_input_m3"]), Decimal("5662310.4"))
        self.assertTrue(main["numerical_gate_passed"])
        self.assertFalse(main["network_support_gate_passed"])
        self.assertEqual(main["gathering_regions"], [])

    def test_quantized_csv_rounding_is_not_a_loosened_ledger(self):
        frames = flat_profile(rain.RATES["equal-input"])
        rounded = {frame: {key: value.quantize(Decimal("0.000001")) for key, value in row.items()}
                   for frame, row in frames.items()}
        self.assertTrue(rain.validate_profile(rounded, rain.RATES["equal-input"], 120)["numerical_gate_passed"])
        rounded[59][(rain.WATER, "total_water_volume_m3")] += 5
        rounded[59][(rain.WATER, "conservation_residual_m3")] += 5
        with self.assertRaisesRegex(ValueError, "water ledger"):
            rain.validate_profile(rounded, rain.RATES["equal-input"], 120)

    def test_requires_continuous_corridor_and_lateral_gathering(self):
        frames = flat_profile(horizon=1200)
        for row in frames.values():
            for key, value in (("material_wet_cells", 11), ("material_active_cells", 11),
                               ("converged_moving_cells", 11), ("largest_corridor_cells", 11),
                               ("largest_corridor_span_m", 300), ("converged_water_volume_m3", 100)):
                row[(rain.RAIN, key)] = Decimal(value)
        result = rain.validate_profile(frames, rain.RATES["main"], 1200)
        self.assertEqual(result["longest_continuous_corridor_support_seconds"], 1200)
        self.assertFalse(result["network_support_gate_passed"])
        for row in frames.values():
            cat = f"{rain.RAIN}.region.collection"
            addition = max(Decimal(40), row[(cat, "direct_rain_volume_m3")] / 5)
            row[(cat, "water_volume_m3")] += addition
            row[(cat, "net_lateral_storage_m3")] = addition
        result = rain.validate_profile(frames, rain.RATES["main"], 1200)
        self.assertTrue(result["network_support_gate_passed"])
        self.assertEqual(result["gathering_regions"], ["collection"])
        frames[300][(rain.RAIN, "largest_corridor_span_m")] = Decimal(0)
        self.assertFalse(rain.validate_profile(frames, rain.RATES["main"], 1200)["network_support_gate_passed"])

    def test_failure_matrix(self):
        modifications = (
            (rain.FLAGS, Decimal(1)),
            ((rain.RAIN, "enabled"), Decimal(0)),
            ((rain.RAIN, "physical_time_s"), Decimal(99)),
            ((rain.RAIN, "rate_mm_per_hour"), Decimal(60)),
            ((rain.RAIN, "scheduled_volume_m3"), Decimal(1)),
            ((rain.RAIN, "material_active_cells"), Decimal("1.5")),
            ((rain.RAIN, "converged_water_volume_m3"), Decimal(-1)),
            ((rain.WATER, "cumulative_sink_volume_m3"), Decimal(1)),
            ((rain.WATER, "conservation_residual_m3"), Decimal(3)),
            ((f"{rain.RAIN}.region.upper", "net_lateral_storage_m3"), Decimal(3)),
            ((f"{rain.RAIN}.region.upper", "valid"), Decimal(0)),
            ((f"{rain.RAIN}.region.upper", "direct_rain_volume_m3"), Decimal(0)),
        )
        for key, value in modifications:
            with self.subTest(key=key):
                frames = flat_profile()
                frames[10][key] = value
                with self.assertRaises(ValueError):
                    rain.validate_profile(frames, rain.RATES["main"], 120)

    def test_missing_duplicate_nonfinite_and_cadence(self):
        frames = flat_profile()
        del frames[10]
        with self.assertRaises(ValueError):
            rain.validate_profile(frames, rain.RATES["main"], 120)
        frames = flat_profile()
        del frames[10][(rain.RAIN, "cumulative_depth_m")]
        with self.assertRaises(ValueError):
            rain.validate_profile(frames, rain.RATES["main"], 120)
        frames = flat_profile()
        frames[10][(rain.RAIN, "cumulative_depth_m")] = Decimal("NaN")
        with self.assertRaises(ValueError):
            rain.validate_profile(frames, rain.RATES["main"], 120)

    def test_exact_extension_prefix_excludes_only_timing(self):
        short = flat_profile(horizon=120)
        long = flat_profile(horizon=240)
        self.assertTrue(rain.compare_prefix(short, long)["passed"])
        long[0][("fluid_25d.solver", "gpu_step_ms")] = Decimal(5)
        self.assertTrue(rain.compare_prefix(short, long)["passed"])
        long[0][("fluid_25d.hydraulic_identity", "hash_lo_u32")] += 1
        with self.assertRaisesRegex(ValueError, "prefix"):
            rain.compare_prefix(short, long)

    def test_launcher_never_turns_continuous_rain_into_a_long_pulse(self):
        for case in rain.RATES:
            args = demo.arguments(case, "collection", markers=True, developed=True)
            self.assertNotIn("--headless", args)
            self.assertNotIn("--frames", args)
            self.assertNotIn("--fluid25d-source-active-duration-seconds", args)
            self.assertNotIn("--fluid25d-natural-flow-recipe", args)
            self.assertNotIn("--fluid25d-natural-flow-source-m3-per-s", args)
            self.assertIn("--fluid25d-hillside-advance-and-continue-seconds", args)
            self.assertEqual(args[args.index("--fluid25d-motion-marker-mode") + 1], "local")
            self.assertEqual(args[args.index("--fluid25d-rainfall-rate-mm-per-hour") + 1], str(rain.RATES[case]))
        with self.assertRaises(ValueError):
            rain.arguments(Decimal(60))
        with self.assertRaises(ValueError):
            demo.arguments(camera="source")

    def test_frame_and_label_clocks_are_post_step(self):
        folder = Path("/tmp/rain-test")
        command = rain.capture_arguments(folder, "travel", "-0.72", "composite", 7200)
        self.assertIn("--fluid25d-natural-flow-home-pitch-radians", command)
        self.assertEqual(command[command.index("--frames") + 1], "3600")
        self.assertIn("n*2+2", rain.video_filter("travel"))
        command = rain.still_command(folder, 600)
        self.assertIn(r"select=eq(n\,299)", command)

    def test_phase_never_overwrites_and_failure_returns_nonzero(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            rain.new_phase(out, "smoke")
            with self.assertRaises(FileExistsError):
                rain.new_phase(out, "smoke")
            with patch.object(rain, "profiles_phase", side_effect=ValueError("expected fixture failure")):
                self.assertEqual(rain.main(["--phase", "profiles", "--out", str(out)]), 1)

    def test_execution_receipt_binds_command_inputs_and_every_artifact(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(rain, "identity", return_value={"app": "frozen"}):
            folder, app = Path(temp), Path("/tmp/app")
            for name in rain.expected_artifacts("final.png"):
                (folder / name).write_bytes(name.encode())
            receipt = {"label": "fixture", "exit_code": 0, "before": {"app": "frozen"},
                       "after": {"app": "frozen"}, "command": ["rtk", "proxy", str(app), "--fixture"],
                       "artifacts": {name: {"path": str((folder / name).resolve()), "sha256": rain.sha(folder / name)}
                                     for name in rain.expected_artifacts("final.png")}}
            path = folder / "execution.json"
            rain.write_json(path, receipt)
            record = {"label": "fixture", "execution_receipt": {"path": str(path.resolve()), "sha256": rain.sha(path)}}
            self.assertEqual(rain.checked_execution(record, folder, app, ["--fixture"], "final.png"), receipt)
            with self.assertRaises(ValueError):
                rain.checked_execution(record, folder, app, ["--changed"], "final.png")
            (folder / "profile.metrics.csv").write_text("tampered")
            with self.assertRaisesRegex(ValueError, "artifact"):
                rain.checked_execution(record, folder, app, ["--fixture"], "final.png")
            rain.write_json(path, {**receipt, "exit_code": 9})
            with self.assertRaisesRegex(ValueError, "receipt"):
                rain.checked_execution(record, folder, app, ["--fixture"], "final.png")
            record["execution_receipt"]["sha256"] = rain.sha(path)
            with self.assertRaisesRegex(ValueError, "succeed"):
                rain.checked_execution(record, folder, app, ["--fixture"], "final.png")

    def test_rain_pacing_has_no_headless_readbacks_or_point_source(self):
        command = pacing.command(Path("/tmp/app"), Path("/tmp/evidence"), True)
        self.assertNotIn("--headless", command)
        self.assertNotIn("--profile-diagnostics", command)
        self.assertNotIn("--fluid25d-natural-flow-source-m3-per-s", command)
        self.assertEqual(command[command.index("--fluid25d-hillside-advance-and-continue-seconds") + 1], "3600")

    def test_pacing_reuse_binds_successful_child_and_unchanged_artifacts(self):
        with tempfile.TemporaryDirectory() as temp:
            case, app = Path(temp), Path("/tmp/app")
            names = {"child.log", *("profile" + suffix for suffix in pacing.pacing.ARTIFACT_SUFFIXES)}
            for name in names:
                (case / name).write_bytes(name.encode())
            receipt = {"command": pacing.command(app, case, False), "before": {"app": "frozen"},
                       "after": {"app": "frozen"}, "child": {"exit_code": 0},
                       "artifacts": {name: {"path": str((case / name).resolve()), "sha256": rain.sha(case / name)}
                                     for name in names}}
            path = case / "execution.json"
            rain.write_json(path, receipt)
            self.assertEqual(pacing.check_execution(app, case, False, {"app": "frozen"}), receipt)
            with self.assertRaises(ValueError):
                pacing.check_execution(app, case, True, {"app": "frozen"})
            receipt["child"]["exit_code"] = 9
            rain.write_json(path, receipt)
            with self.assertRaises(ValueError):
                pacing.check_execution(app, case, False, {"app": "frozen"})
            receipt["child"]["exit_code"] = 0
            rain.write_json(path, receipt)
            (case / "profile.metrics.csv").write_text("tampered")
            with self.assertRaisesRegex(ValueError, "artifact"):
                pacing.check_execution(app, case, False, {"app": "frozen"})

    def test_pacing_independent_clock_integral_and_continuation(self):
        frames, metrics = {}, {}
        inflow = rain.AREA * rain.RATES["main"] / Decimal(3600000)
        for index in range(8):
            physical = Decimal((index + 1) * 2)
            row = {name: Decimal(0) for name in pacing.FIELDS}
            row.update({"fixed_steps": Decimal(1), "physical_time_s": physical,
                        "last_step_source_m3_per_s": inflow,
                        "paused": Decimal(index == 0), "advance_remaining_steps": Decimal(index == 0),
                        "requested_playback_x": Decimal(8), "rain_enabled": Decimal(1),
                        "rain_applied_rate_mm_per_hour": Decimal(12),
                        "rain_cumulative_depth_m": Decimal(12) * physical / Decimal(3600000),
                        "rain_scheduled_volume_m3": inflow * physical,
                        "achieved_continuous_playback_x": Decimal(0 if index < 2 else 8)})
            frames[index] = {"delta_ms": Decimal(250)}
            metrics[index] = {(pacing.pacing.PLAYBACK_CATEGORY, name): value for name, value in row.items()}
        result = pacing.validate(frames, metrics, [], False, count=8, advance=4, minimum_continuous=2)
        self.assertEqual(result["continuous_frames"], 6)
        self.assertEqual(result["final_physical_seconds"], "16")
        for name, wrong in (("physical_time_s", Decimal(7)), ("rain_enabled", Decimal(0)),
                            ("rain_scheduled_volume_m3", Decimal(0)), ("dropped_backlog_frames", Decimal(1)),
                            ("scheduled_source_volume_m3", Decimal(1)),
                            ("achieved_continuous_playback_x", Decimal(60))):
            key = (pacing.pacing.PLAYBACK_CATEGORY, name)
            old = metrics[5][key]
            metrics[5][key] = wrong
            with self.subTest(field=name), self.assertRaises(ValueError):
                pacing.validate(frames, metrics, [], False, count=8, advance=4, minimum_continuous=2)
            metrics[5][key] = old
        with self.assertRaisesRegex(ValueError, "GPU samples"):
            pacing.validate(frames, metrics, [], True, count=8, advance=4, minimum_continuous=2)


if __name__ == "__main__":
    unittest.main()
