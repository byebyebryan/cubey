"""Synthetic native bank switching/cache/window checks, not a solver test."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from convert_synxflow_recording_v1 import convert_case
from test_convert_synxflow_recording_v1 import _make_case, _write_asc
from test_display_coverage_gpu_v1 import make_sidecar

MODES = ("reference", "bspline-2x", "marching-squares")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target",required=True,type=Path)
    parser.add_argument("--video",action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cubey-bank-comparison-gpu-") as temporary:
        root = Path(temporary)
        case = _make_case(root)
        _write_asc(case/"native/output/h_0.asc",[.08]*4)
        recording = convert_case(case,root/"recording")
        coverage,document = make_sidecar(root/"masks",recording)
        document["frames"] = document["frames"][:2]
        coverage.write_text(json.dumps(document))
        base = ["rtk","proxy",str(args.target.resolve()),"--headless","--width","128","--height","96",
                "--fluid25d-recording",str(recording),"--fluid25d-native-presentation","readable",
                "--fluid25d-native-surface-highlights","off","--fluid25d-recording-gpu-validation"]
        comparison = ["--fluid25d-native-display-coverage",str(coverage),"--fluid25d-bank-comparison"]
        frozen = {str(p):sha(p) for p in recording.parent.rglob("*") if p.is_file()}

        def run(label,extra=(),success=True,include_comparison=True,video=False):
            output = root/(label+(".mp4" if video else ".png"))
            command = base + (comparison if include_comparison else []) + list(extra) + ["--output",str(output)]
            result = subprocess.run(command,text=True,capture_output=True,timeout=90)
            text = result.stdout+result.stderr
            print(json.dumps({"test":label,"exit_code":result.returncode,"command":command}))
            if success and any(s in text for s in ("no Vulkan physical devices found","vkEnumeratePhysicalDevices",
                    "no Vulkan device with required queues and dynamic rendering found")):
                raise RuntimeError("SKIP_NO_VULKAN")
            assert "vulkan validation error" not in text.lower(),text
            if success:
                assert result.returncode==0 and output.is_file(),text
                assert "fluid_25d_recording_upload: PASS" in text and "hydraulic_dispatches=0" in text,text
                if include_comparison:
                    assert "bank_cache: READY frames=2 bytes=200" in text,text
                    assert "display_coverage_load:" not in text,"comparison reloaded a validated cached mask"
            else:
                assert result.returncode!=0,text
            return output,text

        reference,_ = run("ordinary",include_comparison=False)
        current,_ = run("comparison-reference",["--fluid25d-native-bank-view","reference"])
        assert sha(reference)==sha(current),"comparison reference changed static appearance"
        run("comparison-bspline",["--fluid25d-native-bank-view","bspline-2x"])
        masked,_ = run("comparison-ms",["--fluid25d-native-bank-view","marching-squares"])
        assert sha(reference)!=sha(masked),"coverage option did not affect filled water"
        for view in ("terrain","depth","surface","flow","direction","wet-dry"):
            images = []
            for mode in MODES:
                image,text = run(view+"-"+mode,["--fluid25d-native-bank-view",mode,
                    "--fluid25d-view","diagnostics","--debug-view",view])
                images.append(sha(image))
                assert "bank_mask_upload:" not in text,"raw 2D diagnostics uploaded an unused mask"
            assert len(set(images))==1,"bank modes changed raw native map: "+view
        run("ordinary-outside-masks",["--fluid25d-native-display-coverage",str(coverage),
            "--fluid25d-native-bank-view","reference","--fluid25d-recording-time-seconds","2"],include_comparison=False)
        run("comparison-outside-window",["--fluid25d-recording-time-seconds","2"],success=False)
        run("legacy-mask-outside-window",["--fluid25d-native-display-coverage",str(coverage),
            "--fluid25d-recording-time-seconds","2"],include_comparison=False,success=False)
        run("unavailable-ms",["--fluid25d-native-bank-view","marching-squares"],include_comparison=False,success=False)
        run("cycle-without-video",["--fluid25d-bank-comparison-cycle-frames","2"],success=False)
        for label,change in (
            ("bad-later-digest",lambda d:d["frames"][1].update(sha256="0"*64)),
            ("unsigned-subdivision-wrap",lambda d:d.update(subdivision=4294967300)),
            ("signed-subdivision-wrap",lambda d:d.update(subdivision=-4294967292)),
        ):
            bad = copy.deepcopy(document)
            change(bad)
            path = coverage.parent/(label+".json")
            path.write_text(json.dumps(bad))
            _,text = run(label,["--fluid25d-native-display-coverage",str(path),"--fluid25d-bank-comparison"],
                include_comparison=False,success=False)
            assert "display coverage" in text.lower(),text
        full,full_document = make_sidecar(root/"gap-masks",recording)
        full_document["frames"] = [full_document["frames"][0],full_document["frames"][2]]
        full.write_text(json.dumps(full_document))
        run("sparse-window",["--fluid25d-native-display-coverage",str(full),"--fluid25d-bank-comparison"],
            include_comparison=False,success=False)
        if args.video:
            video_args = ["--capture","video","--frames","24","--fps","12",
                          "--fluid25d-bank-comparison-cycle-frames","2"]
            _,held = run("held-mode-cycles",video_args+["--fluid25d-recording-frame-interval-seconds",".01"],video=True)
            rows = re.findall(r"bank_view: mode=(\S+) requested_s=(\S+) saved_s=(\S+) camera=(\S+) paused=(\S+) rate=(\S+) cue_reset=(\S+) quiver_reset=(\S+) marker_reset=(\S+) generation=(\S+);",held)
            assert len(rows)==12 and set(r[0] for r in rows)==set(MODES),held
            assert len(set(r[2:6]+r[9:] for r in rows))==1,"mode switching changed saved state/camera/pause/rate/generation"
            assert all(abs(float(r[1])-i*.02)<1e-7 for i,r in enumerate(rows)),"mode switching changed requested playback timing"
            # An inactive quiver can keep its initial pending-reset bit set;
            # mode switches must preserve it, not force every history bit low.
            assert len(set(r[6:9] for r in rows[1:]))==1 and all(r[6]=="0" and r[8]=="0" for r in rows[1:]),"mode switching reset cue histories"
            assert set(re.findall(r"saved_s=(\S+)",held))=={"0.000000000"},held
            _,ending = run("bounded-end",video_args+["--fluid25d-recording-frame-interval-seconds",".25"],video=True)
            captures = re.findall(r"bank_capture: output_frame=(\d+) mode=(\S+) requested_unbounded_s=(\S+) presented_s=(\S+) saved_s=(\S+) end=(\d+) looped=(\d+) cache_hit=(\d+)",ending)
            assert len(captures)==24 and all(float(r[4])<2 for r in captures),ending
            assert all(r[5]=="1" for r in captures[8:]),"comparison did not stop at baked boundary"
            _,looped = run("bounded-loop",video_args+["--fluid25d-recording-frame-interval-seconds",".25",
                "--fluid25d-bank-comparison-loop"],video=True)
            captures = re.findall(r"bank_capture: output_frame=(\d+) mode=(\S+) requested_unbounded_s=(\S+) presented_s=(\S+) saved_s=(\S+) end=(\d+) looped=(\d+) cache_hit=(\d+)",looped)
            assert len(captures)==24 and all(float(r[4])<2 for r in captures),looped
            assert [float(captures[i][3]) for i in (0,8,16)]==[0.,0.,0.],"explicit loop lost recorded timing"
            assert all(r[7]=="1" for r in captures),"cached window was not resident"
        assert frozen=={str(p):sha(p) for p in recording.parent.rglob("*") if p.is_file()},"native input files changed"
    print("bank comparison GPU/cache/clock/diagnostics: PASS")


if __name__=="__main__":
    try:
        main()
    except RuntimeError as error:
        if str(error)=="SKIP_NO_VULKAN":
            print("SKIP: no usable Vulkan device")
            raise SystemExit(77)
        raise
