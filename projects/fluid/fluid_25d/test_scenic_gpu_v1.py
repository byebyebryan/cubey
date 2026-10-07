#!/usr/bin/env python3
"""Scenic saved-state rendering controls. Synthetic fields, not solver validation."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

import run_native_shoreline_raster_v1 as fixtures
from test_display_coverage_gpu_v1 import make_sidecar


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target",required=True,type=Path)
    parser.add_argument("--video",action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cubey-scenic-gpu-") as temp:
        root = Path(temp)
        rows = []
        for case in ("positive-film-gap","one-cell-stream","partial-dry-lake","fully-wet-lake"):
            manifest = fixtures.fixture(root/case,case)
            native = {str(p):digest(p) for p in manifest.parent.rglob("*") if p.is_file()}
            def run(label, extra=(), video=False):
                output = root/(case+"-"+label+(".mp4" if video else ".png"))
                command = ["rtk","proxy",str(args.target.resolve()),"--headless","--width","256","--height","192",
                    "--fluid25d-recording",str(manifest),"--fluid25d-recording-gpu-validation",
                    "--fluid25d-native-presentation","scenic","--output",str(output),*extra]
                result = subprocess.run(command,text=True,capture_output=True,timeout=90)
                text = result.stdout+result.stderr
                if any(s in text for s in ("no Vulkan physical devices found","vkEnumeratePhysicalDevices",
                                          "no Vulkan device with required queues and dynamic rendering found")):
                    raise RuntimeError("SKIP_NO_VULKAN")
                assert result.returncode==0 and output.is_file(),text
                assert "vulkan validation error" not in text.lower(),text
                assert "fluid_25d_recording_upload: PASS" in text and "hydraulic_dispatches=0" in text,text
                rows.append({"case":case,"control":label,"immutable_upload":"PASS","validation_errors":0})
                return output
            a = run("rest-a",["--fluid25d-recording-time-seconds","0"])
            b = run("rest-b",["--fluid25d-recording-time-seconds","2"])
            assert digest(a)==digest(b),"identical calm fields changed across saved times"
            refined = ["--fluid25d-scenic-material", "refined"]
            ra = run("refined-rest-a",[*refined,"--fluid25d-recording-time-seconds","0"])
            rb = run("refined-rest-b",[*refined,"--fluid25d-recording-time-seconds","2"])
            assert digest(ra)==digest(rb),"refined calm material changed across identical saved fields"
            run("bspline",["--fluid25d-native-bank-view","bspline-2x"])
            sidecar,_ = make_sidecar(root/(case+"-masks"),manifest)
            run("ms",["--fluid25d-native-display-coverage",str(sidecar),"--fluid25d-native-bank-view","marching-squares"])
            if case == "partial-dry-lake":
                run("refined-bspline",[*refined,"--fluid25d-native-bank-view","bspline-2x"])
                run("refined-ms",[*refined,"--fluid25d-native-display-coverage",str(sidecar),"--fluid25d-native-bank-view","marching-squares"])
            diagnostics = []
            for style in ("readable","scenic"):
                diagnostics.append(run("raw-"+style,["--fluid25d-native-presentation",style,
                    "--fluid25d-view","diagnostics","--debug-view","depth"]))
            assert digest(diagnostics[0])==digest(diagnostics[1]),"Scenic changed the raw depth map"
            if args.video and case == "fully-wet-lake":
                video = run("rest-video",["--capture","video","--frames","8","--fps","4",
                    "--fluid25d-recording-frame-interval-seconds","0.5"],True)
                result = subprocess.run(["rtk","proxy","ffmpeg","-v","error","-ignore_editlist","1","-i",str(video),
                    "-fps_mode","passthrough","-f","rawvideo","-pix_fmt","rgb24","pipe:1"],capture_output=True,timeout=30)
                assert result.returncode == 0, result.stderr.decode(errors="replace")
                decoded = result.stdout
                size = 256*192*3
                assert len(decoded)==size*8, f"expected 8 complete decoded frames, got {len(decoded)/size}"
                # Compare temporal codec drift against an independently encoded
                # static PNG. CRF18 I/P frames are not byte-identical even when
                # the input pixels are; the still controls above are lossless.
                frames = [decoded[i*size:(i+1)*size] for i in range(8)]
                control = root/"static-codec-control.mp4"
                result = subprocess.run(["rtk","proxy","ffmpeg","-v","error","-n","-loop","1","-framerate","4","-i",str(a),
                    "-frames:v","8","-c:v","libx264","-preset","veryfast","-crf","18","-g","12","-bf","0",
                    "-pix_fmt","yuv420p",str(control)],capture_output=True,timeout=30)
                assert result.returncode == 0, result.stderr.decode(errors="replace")
                result = subprocess.run(["rtk","proxy","ffmpeg","-v","error","-ignore_editlist","1","-i",str(control),
                    "-fps_mode","passthrough","-f","rawvideo","-pix_fmt","rgb24","pipe:1"],capture_output=True,timeout=30)
                assert result.returncode == 0, result.stderr.decode(errors="replace")
                reference = result.stdout
                assert len(reference)==size*8
                noise = [abs(x-y) for x,y in zip(reference[:size],reference[-size:])]
                drift = [abs(x-y) for x,y in zip(frames[0],frames[-1])]
                reference_mean = sum(noise)/size
                drift_mean = sum(drift)/size
                assert max(noise)<=8 and reference_mean<=0.1,"unexpected static-codec baseline"
                assert max(drift)<=max(noise)+2 and drift_mean<=reference_mean*2+0.02,"calm water exceeds static-codec drift"
                rows.append({"control":"calm-video-codec-relative","max_byte_drift":max(drift),
                    "mean_byte_drift":drift_mean,"static_codec_mean":reference_mean})
            assert native=={str(p):digest(p) for p in manifest.parent.rglob("*") if p.is_file()}
        print(json.dumps({"status":"PASS","scope":"synthetic GPU material/immutability controls","rows":rows}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        if str(error)=="SKIP_NO_VULKAN":
            raise SystemExit(77)
        raise
