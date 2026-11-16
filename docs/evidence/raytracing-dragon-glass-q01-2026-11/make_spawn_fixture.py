"""Build a private shader fixture from the production reconstruction/spawn code.

This is a two-boundary diagnostic, not a camera/sample replay. It supplies the
recorded source barycentrics and outgoing directions, then traces the actual
certified dragon from the resulting spawn. No production files are modified.
"""

import argparse
import json
import shutil
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    evidence = Path(__file__).parent
    cases = []
    for name in ("seed7-isolated", "seed19-medium"):
        journal = json.loads((evidence / "journals" / (name + ".json")).read_text())
        end = journal["first_failure_record_plus_one"] - 1
        branch = journal["records"][end][0][1]
        records = [p for p in journal["records"][:end] if p[0][1] == branch]
        surface = next(p for p in reversed(records) if p[0][0] == 11)
        spawn = next(p for p in reversed(records) if p[0][0] == 14)
        cases.append((surface[7][1], surface[4][1:3], spawn[2][:3]))
    shutil.copytree(
        args.source, args.output, ignore=shutil.ignore_patterns("__pycache__")
    )
    path = args.output / "shaders/trace.rgen"
    shader = path.read_text()
    start = shader.index(" if(hit.t<0)return s;\n Instance inst=")
    end = shader.index("\nSurface surfaceRay(", start)
    body = shader[start:end]
    extra = " RT_JOURNAL_ARG(0xffffffffu)" if "RT_JOURNAL_ARG" in shader else ""
    initial = (
        "Surface s=Surface(vec3(0),vec3(0),vec3(0),vec3(0),-1,-1,0u,0u,0,vec3(0)"
        + extra
        + ");\n"
    )
    helper = (
        "Surface fixtureSurfaceFromHit(vec3 origin,vec3 direction){\n" + initial + body
    )
    shader = (
        shader[:start]
        + " return fixtureSurfaceFromHit(origin,direction);\n}\n"
        + shader[end:]
    )
    shader = shader.replace(
        "Surface surfaceRayMedium(", helper + "\nSurface surfaceRayMedium(", 1
    )
    rows, directions = [], []
    for primitive, bary, direction in cases:
        rows.append(f"uvec3({primitive}u,{bary[0]}u,{bary[1]}u)")
        directions.append("uvec3(" + ",".join(f"{x}u" for x in direction) + ")")
    fixture = """
void fixtureMain(){
 uint pixel=gl_LaunchIDEXT.x;
 uvec3 data[2]=uvec3[2](DATA);
 uvec3 directions[2]=uvec3[2](DIRECTIONS);
 vec3 outgoing=uintBitsToFloat(directions[pixel]);
 hit=Hit(uintBitsToFloat(data[pixel].yz),1,0u,data[pixel].x);
 Surface source=fixtureSurfaceFromHit(vec3(0),outgoing);
 vec3 spawned=offsetOrigin(source,outgoing);
 Surface result=surfaceRayMedium(spawned,outgoing,2u,0);
 for(uint i=0u;i<16u;++i)queries[pixel*16u+i]=vec4(0);
 queries[pixel*16u]=vec4(source.p,source.spawnOffset);
 queries[pixel*16u+1u]=vec4(source.outward,dot(outgoing,source.outward));
 queries[pixel*16u+2u]=vec4(source.spawnNormal,source.spawnOffset);
 queries[pixel*16u+3u]=vec4(spawned,length(spawned-source.p));
 queries[pixel*16u+4u]=vec4(outgoing,0);
 queries[pixel*16u+5u]=vec4(result.p,result.t);
 queries[pixel*16u+6u]=vec4(float(source.primitive),float(result.primitive),float(result.id),float(status));
 queries[pixel*16u+7u]=vec4(result.outward,0);
 imageStore(outputImage,ivec2(gl_LaunchIDEXT.xy),result.id==0?vec4(0,1,0,1):vec4(1,0,0,1));
}
""".replace("DATA", ",".join(rows)).replace("DIRECTIONS", ",".join(directions))
    # Replace only the entry point; every reconstruction/spawn expression above
    # is copied verbatim from this exact source, rather than a reference rewrite.
    assert shader.count("void main(){") == 1
    shader = shader.replace("void main(){", fixture + "\nvoid unusedCameraMain(){", 1)
    shader += "\nvoid main(){fixtureMain();}\n"
    path.write_text(shader)
    (args.output / "fixture-inputs.json").write_text(json.dumps(cases, indent=2) + "\n")


if __name__ == "__main__":
    main()
