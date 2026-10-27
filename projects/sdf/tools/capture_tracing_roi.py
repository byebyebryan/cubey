#!/usr/bin/env python3
"""Render diagnostic pixels through the shared native GPU query tracer.

Orthographic ROI preserves sub-grid world scale; pixels encode native hit
normals. This is query-harness evidence, separate from native PBR captures.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import zlib
from tracing_comparison import H


def png(path, size, pixels):
    def chunk(name, data):
        return struct.pack("!I", len(data))+name+data+struct.pack("!I", zlib.crc32(name+data)&0xffffffff)
    raw = b"".join(b"\0"+bytes(pixels[y*size*3:(y+1)*size*3]) for y in range(size))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR", struct.pack("!2I5B", size, size, 8, 2, 0, 0, 0))+
                     chunk(b"IDAT", zlib.compress(raw))+chunk(b"IEND", b""))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--binary", type=Path, default=Path("build/release/projects/sdf/sdf"))
    args = p.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    fixtures = json.loads(Path("projects/sdf/comparison/fixtures.json").read_text())
    selected = [next(c for c in fixtures["cases"] if c["name"] == name) for name in ("subgrid-sphere", "thin-stick-0.5h")]
    size = 128
    cases = []
    for c in selected:
        center = [.5*H]*3 if c["name"] == "subgrid-sphere" else [0, 0, 0]
        queries = []
        for y in range(size):
            for x in range(size):
                queries.append(dict(kind="ray", origin=[center[0]+((x+.5)/size-.5)*3*H,
                    center[1]+(.5-(y+.5)/size)*3*H, center[2]+.01], direction=[0, 0, -1]))
        cases.append(dict(name=c["name"], scene=c["scene"], queries=queries))
    input_file = args.output/"input.json"
    input_file.write_text(json.dumps(dict(format="cubey.sdf.tracing-study", version=1, cases=cases))+"\n")
    summary = []
    for mode, csg in [("sampled", "full"), ("analytic", "full"), ("hybrid", "full"), ("analytic", "pruned"), ("hybrid", "pruned")]:
        name = f"{mode}-{csg}"; result = args.output/f"{name}.json"
        command = [str(args.binary), "--headless", "--headless-render-only", "--require-validation", "--frames", "1",
                   "--width", "32", "--height", "32", "--surface-trace", mode, "--csg-evaluation", csg,
                   "--comparison-input", str(input_file), "--comparison-output", str(result)]
        run = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        (args.output/f"{name}.log").write_text(run.stdout)
        if run.returncode:
            raise RuntimeError(run.stdout[-1000:])
        data = json.loads(result.read_text())
        for c in data["cases"]:
            pixels = []; hits = 0; exhausted = 0
            for r in c["results"]:
                if r["status"] == 1:
                    hits += 1; pixels.extend(max(0, min(255, round((v*.5+.5)*255))) for v in r["normal"])
                else:
                    exhausted += r["status"] == 2; pixels.extend([255, 0, 255] if r["status"] == 2 else [7, 11, 18])
            file = args.output/f'{c["name"]}-{name}.png'; png(file, size, pixels)
            summary.append(dict(fixture=c["name"], mode=mode, csg=csg, hits=hits, exhausted=exhausted,
                                size=size, viewport_world=3*H, file=str(file)))
    (args.output/"roi.json").write_text(json.dumps(summary, indent=2)+"\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
