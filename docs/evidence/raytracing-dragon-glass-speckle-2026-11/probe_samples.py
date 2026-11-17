"""Private full-logical-extent, single-pixel sample series. No live renderer edits.

Keep the ordinary camera/RNG sequence, skip unrelated pixels, and log only
primary samples, their total RGB, and solar contributions at sky terminals.
The private isolated mode runs the entire sample prefix, unlike the ordinary
one-sample numerical replay. A parity check against ordinary HDR is required.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path("outputs/raytracing/dragon-glass-speckle-20261116").resolve()
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--stage", choices=("prepare", "run"), required=True)
args = parser.parse_args()
source, build = ROOT/"sample-source", ROOT/"sample-build"
if args.stage == "prepare":
    shutil.copytree("projects/raytracing", source)
    renderer = source/"raytracing_renderer.cpp"
    text = renderer.read_text()
    assert text.count("c.journal_isolate ? 1U :") == 1
    renderer.write_text(text.replace("c.journal_isolate ? 1U :", "c.journal_isolate ? c.samples :"))
    material = source/"shaders/material_workshop.glsl"
    text = material.read_text()
    assert text.count("completed=rayJournal.control.z;") == 1
    text = text.replace("completed=rayJournal.control.z;", "// Preserve the ordinary full-prefix sequence.")
    old = "jActive=jPixel&&(jFind?rayJournal.state.w==0u:completed==rayJournal.control.z);"
    assert text.count(old) == 1
    text = text.replace(old, "jActive=jPixel;")
    helper = """
void rtSpeckleTerminal(vec3 direction,vec3 weight,float pdf,float mis) {
    float sine=sin(workshop.solar.x);
    vec3 solar=pdf>0?weight*environment.directColor.rgb/(CUBEY_PBR_PI*sine*sine)*workshop.solar.z*mis:vec3(0);
    jRecord(70u,vec4(direction,pdf),vec4(solar,mis),vec4(weight,0),vec4(0),vec4(0),vec4(0),vec4(0));
}
"""
    text = text.replace("void traceWorkshop() {", helper+"\nvoid traceWorkshop() {")
    old = "color+=weight*transportSky(direction,priorSolarPdf,solarMisWeight);"
    assert text.count(old) == 1
    text = text.replace(old, old+"\n            rtSpeckleTerminal(direction,weight,priorSolarPdf,solarMisWeight);")
    old = "color+=weight*(depth==0?textureLod(visibleBackground,uv,0).rgb*environment.options.w:transportSky(direction,priorSolarPdf,solarMisWeight));"
    assert text.count(old) == 1
    text = text.replace(old, old+"\n            if(depth>0)rtSpeckleTerminal(direction,weight,priorSolarPdf,solarMisWeight);")
    material.write_text(text)
    journal = source/"shaders/ray_journal.glsl"
    text = journal.read_text().replace("if(!jActive)return;", "if(!jActive || (kind!=1u && kind!=2u && kind!=70u))return;")
    text = text.replace("rayJournal.packets[first+7u]=floatBitsToUint(g);", "rayJournal.packets[first+7u]=uvec4(jSequence,0,0,0);")
    journal.write_text(text)
    command = [sys.executable, "-B", "projects/raytracing/tools/build_ray_replay.py", "--journal", "--source", str(source), "--output", str(build)]
    p = subprocess.run(command, capture_output=True, text=True)
    (ROOT/"sample-build.log").write_text(p.stdout+p.stderr)
    assert p.returncode == 0, p.stdout+p.stderr
    print("private sample-series build prepared", flush=True)
else:
    out = ROOT/"sample-probes"
    out.mkdir(exist_ok=True)
    for name, x, y, seed in [("unstable-s19",818,372,19), ("unstable-s43",818,372,43), ("persistent-s19",738,527,19)]:
        command = [str(build/"raytracing"), "--config", "projects/raytracing/presets/optics/dragon-glass.json", "--headless", "--frames", "1", "--require-validation", "--width", "1920", "--height", "1080", "--samples", "4096", "--seed", str(seed), "--journal-x", str(x), "--journal-y", str(y), "--journal-sample", "0", "--journal-isolate", "--ray-journal", str(out/(name+".journal.json")), "--output", str(out/(name+".png")), "--metrics", str(out/(name+".metrics.json"))]
        assert not (out/(name+".journal.json")).exists()
        print("probing", name, flush=True)
        p = subprocess.run(command, capture_output=True, text=True, env=dict(os.environ,VK_LAYER_VALIDATE_SYNC="1"), timeout=240)
        log = p.stdout+p.stderr
        (out/(name+".log")).write_text(log)
        assert p.returncode == 0, log
        assert not any(t in log.lower() for t in ("vuid-", "hazard detected", "vulkan validation error")), log
        print("recorded", name, flush=True)
