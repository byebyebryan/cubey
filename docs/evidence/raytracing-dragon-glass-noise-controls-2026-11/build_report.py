# /// script
# dependencies = ["pillow==12.0.0"]
# ///
"""Export only intended comparison assets to the immutable report publisher."""
import json
import re
import shutil
import statistics
from pathlib import Path

from PIL import Image

evidence = Path(__file__).resolve().parent
repo = evidence.parents[2]
root = repo / "outputs/raytracing/dragon-glass-noise-controls-20261116/final"
target = root / "report"
(target / "images").mkdir(parents=True, exist_ok=True)
data = json.loads((evidence / "analysis.json").read_text())
data["images"] = {}
for case in data["details"]:
    name = case["name"]
    denoise = case["denoised"]
    key = name + ("-oidn" if denoise else "")
    src = root / ("denoised" if denoise else "captures") / (name + ".png")
    with Image.open(src) as image:
        data["images"][key] = {}
        for roi, (x, y, w, h) in data["rois"].items():
            filename = key + "-" + roi + ".png"
            image.crop((x, y, x+w, y+h)).save(target / "images" / filename)
            data["images"][key][roi] = "images/" + filename
times = []
first = []
for row in data["denoised"]:
    log = Path(row["prefix"] + ".log").read_text()
    measured = [float(v) for v in re.findall(r"^  msec=([0-9.]+), hash=", log, re.M)]
    assert len(measured) == 3
    first.append(measured[0])
    times.extend(measured[1:])
data["timing"] = dict(oidn_first_ms_median=statistics.median(first),
                      oidn_repeated_ms_median=statistics.median(times),
                      oidn_repeated_ms_range=[min(times), max(times)],
                      oidn_process_wall_seconds_median=statistics.median(
                          r["process_wall_seconds_including_startup_io_and_three_executions"]
                          for r in data["denoised"]))
data["validation"] = json.loads((evidence / "validation.json").read_text())
(target / "comparison.json").write_text(json.dumps(data, indent=2) + "\n")
shutil.copyfile(evidence / "report-template.html", target / "index.html")
print(target)
