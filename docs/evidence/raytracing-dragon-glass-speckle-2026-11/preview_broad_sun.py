# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Private scene-lighting alternative: 4x shared solar angular radius.

The shared atmosphere and traced solar emitter use the same radius. Direct
illumination strength is retained; traced solar radiance is normalized by the
existing 1/(pi*sin(radius)^2) factor. This changes the light's appearance.
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path
sys.path.insert(0,str(Path("projects/raytracing/tools").resolve()))
from measure_dragon_glass import capture

root=Path("outputs/raytracing/dragon-glass-speckle-20261116").resolve()
source,build=root/"broad-sun-source",root/"broad-sun-build"
shutil.copytree("projects/raytracing",source)
env=source/"raytracing_environment.cpp"
text=env.read_text()
old="render::AtmosphereEnvironmentConfig e;"
assert text.count(old)==1
env.write_text(text.replace(old,old+"\n    e.sun_angular_radius *= 4.F; // private lighting comparison only"))
cmd=[sys.executable,"-B","projects/raytracing/tools/build_ray_replay.py","--source",str(source),"--output",str(build)]
p=subprocess.run(cmd,capture_output=True,text=True)
(root/"broad-sun-build.log").write_text(p.stdout+p.stderr)
assert p.returncode==0,p.stdout+p.stderr
records=[]
for samples in (128,512):
    for seed in (19,43):
        name=f"broad-sun-{samples}-s{seed}"
        print("capturing",name,flush=True)
        receipt=capture(build/"raytracing",root/"captures",name,1920,1080,samples,seed,diagnostic_continue=True)
        records.append(dict(samples=samples,seed=seed,prefix=str(root/"captures"/name),receipt=receipt))
(Path(__file__).parent/"broad-sun.json").write_text(json.dumps(dict(scope="private lighting alternative; not a new default",radius_multiplier=4,solar_angular_radius_radians=.004675*4,normal_policy="same guarded smooth sharp glass",captures=records),indent=2)+"\n")
