# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Matched seed differences and energy loss; neither is an accuracy score."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

repo = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(repo / "projects/raytracing/tools"))
from compare_dragon_noise import read_pfm

evidence = Path(__file__).resolve().parent
root = repo / "outputs/raytracing/dragon-glass-noise-controls-20261116/final"
captures = json.loads((root / "captures.json").read_text())
denoised = json.loads((root / "denoised.json").read_text())
assert len(captures["captures"]) == len(denoised) == 16
records = {(c["samples"], c["clamp"], c["seed"]): c for c in captures["captures"]}
denoised = {c["name"]: c for c in denoised}
rois = {"scene": [0, 0, 1920, 1080], "head": [610, 205, 475, 335],
        "body": [710, 375, 625, 465], "base": [655, 655, 700, 285]}
weights = np.array([.2126, .7152, .0722])


def crop(a, rect):
    x, y, w, h = rect
    return a[y:y+h, x:x+w]


def load(record, denoise):
    prefix = denoised[record["name"]]["prefix"] if denoise else record["prefix"]
    hdr = read_pfm(Path(prefix + ".pfm")) if denoise else np.fromfile(
        prefix + ".bin", dtype="<f4").reshape(1080, 1920, 4)[:, :, :3]
    return hdr, np.array(Image.open(prefix + ".png"))[:, :, :3]


results, details = [], []
for samples in [128, 512]:
    raw = {seed: load(records[samples, 0, seed], False) for seed in [19, 43]}
    for limit in [0, 100, 1000, 10000]:
        for denoise in [False, True]:
            data = {seed: load(records[samples, limit, seed], denoise) for seed in [19, 43]}
            metrics = {}
            for label, rect in rois.items():
                a, b = [crop(data[s][0], rect).astype(np.float64) for s in [19, 43]]
                pa, pb = [crop(data[s][1], rect).astype(np.float64) for s in [19, 43]]
                wa, wb = pa.min(axis=2) >= 240, pb.min(axis=2) >= 240
                retention = []
                display_delta = []
                for seed in [19, 43]:
                    base = crop(raw[seed][0], rect).astype(np.float64) @ weights
                    out = crop(data[seed][0], rect).astype(np.float64) @ weights
                    retention.append(float(out.mean() / max(1e-10, base.mean())))
                    display_delta.append(float(np.abs(crop(data[seed][1], rect).astype(float)
                                                       - crop(raw[seed][1], rect)).mean()))
                metrics[label] = dict(seed_hdr_rgb_rmse=float(np.sqrt(np.mean((a-b)**2))),
                    seed_display_rgb8_rmse=float(np.sqrt(np.mean((pa-pb)**2))),
                    seed_display_rgb8_mae=float(np.abs(pa-pb).mean()),
                    white_pixels=[int(wa.sum()), int(wb.sum())],
                    white_pixels_both=int((wa & wb).sum()),
                    mean_hdr_luminance_ratio_to_matched_raw=retention,
                    display_rgb8_mae_to_matched_raw=display_delta)
            results.append(dict(samples=samples, clamp=limit, denoised=denoise, metrics=metrics))
            for seed in [19, 43]:
                record = records[samples, limit, seed]
                hdr, png = data[seed]
                points = {label: dict(hdr_luminance=float(hdr[y, x] @ weights),
                                     rgb8=png[y, x].tolist()) for label, x, y in
                          [("unstable", 818, 372), ("persistent", 738, 527)]}
                details.append(dict(name=record["name"], samples=samples, seed=seed, clamp=limit,
                                    denoised=denoise, points=points))
            print(samples, limit, denoise, "head seed RMS", round(metrics["head"]["seed_display_rgb8_rmse"], 3),
                  "HDR retention", [round(x, 3) for x in metrics["head"]["mean_hdr_luminance_ratio_to_matched_raw"]])

for samples in [128, 512]:
    for seed in [19, 43]:
        baseline = records[samples, 0, seed]["receipt"]["status_or_pixels"]
        for limit in [100, 1000, 10000]:
            assert records[samples, limit, seed]["receipt"]["status_or_pixels"] == baseline
assert all(c["receipt"]["transport_accepted"] for c in records.values())
result = dict(scope="Matched 1080p still images; two seeds, not population variance or accuracy",
              rois=rois, metrics=results, details=details,
              transport_status_identical_across_clamps=True,
              limitations=["ROI rectangles include background and are not segmentation masks",
                           "Clamping and denoising are biased; reduced seed difference is not proof of accuracy",
                           "Raw means and the earlier 8192-SPP images are noisy, not ground truth",
                           "Color-only offline OIDN; no native integration, temporal or motion qualification"],
              raw_fidelity=json.loads((root/"raw-fidelity.json").read_text()),
              captures=captures, denoised=list(denoised.values()))
(evidence / "analysis.json").write_text(json.dumps(result, indent=2) + "\n")
