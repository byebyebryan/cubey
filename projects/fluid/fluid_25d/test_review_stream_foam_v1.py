import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import review_stream_foam_v1 as study


class StreamFoamReviewTests(unittest.TestCase):
    def test_ablations_change_one_factor(self):
        values = study.VARIANTS
        self.assertEqual(values["current"], study.white.VARIANTS["network"])
        self.assertEqual(values["dim"], {**values["current"], "water_stream_foam_brightness": .4})
        self.assertEqual(values["sparse"], {**values["current"], "water_stream_foam_patchiness": 1})
        self.assertEqual(values["combined"], {**values["sparse"], "water_stream_foam_brightness": .4})
        self.assertEqual(values["subtle"], {**study.BASE, "water_stream_foam_strength": .3,
                                          "water_stream_foam_patchiness": 1,
                                          "water_stream_foam_brightness": .55})
        for variant in values.values():
            for key, value in study.BASE.items():
                self.assertEqual(variant[key], value)
            self.assertFalse(any("rain" in key or "solver" in key for key in variant))

    def test_capture_reuses_and_restores_runner_state(self):
        old_variants, old_sources = study.white.VARIANTS, study.white.sources

        def inspect(args):
            self.assertIs(study.white.VARIANTS, study.VARIANTS)
            self.assertIn(str(Path(study.__file__).resolve().relative_to(study.white.ref.ROOT)),
                          study.white.sources())
            raise RuntimeError("probe")

        with patch.object(study.white, "run", side_effect=inspect):
            with self.assertRaisesRegex(RuntimeError, "probe"):
                study.run(SimpleNamespace())
        self.assertIs(study.white.VARIANTS, old_variants)
        self.assertIs(study.white.sources, old_sources)

    def test_two_cycle_capture_clock_is_independent_of_recording_time(self):
        args = study.white.command(study.white.review.SCENES[0], Path("x.mp4"), Path("x.json"),
                                   frames=480, fps=30, advancing=False)
        self.assertEqual(args[args.index("--frames") + 1], "480")
        self.assertEqual(float(args[args.index("--fluid25d-recording-frame-interval-seconds") + 1]), 1 / 30)
        advanced = study.white.command(study.white.review.SCENES[0], Path("x.mp4"), Path("x.json"),
                                       frames=480, fps=30, advancing=True)
        self.assertEqual(advanced[advanced.index("--fluid25d-recording-frame-interval-seconds") + 1], "10")


if __name__ == "__main__":
    unittest.main()
