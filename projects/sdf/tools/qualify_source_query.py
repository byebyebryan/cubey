#!/usr/bin/env python3
"""Additional source-query lighting gates and serial matched GPU timings."""
import argparse
import csv
import copy
import json
import math
from pathlib import Path
import statistics
import struct
import subprocess
from acceleration_loop import gpu_state, save
from benchmark_tracing import percentile
from investigate_guardian import ROOT, digest, invoke, metadata
from qualify_shader_selection import Study, route_flags, ROUTES
from check_tracing_images import pixels
from tracing_comparison import check


def edges(args):
    study = Study(args)
    common = ["--width", "320", "--height", "180", "--authored-scene",
              "projects/sdf/scenes/material-study/sphere-plain.json", *route_flags(ROUTES[-1])]
    for elevation, azimuth in ((0, 0), (0, 90), (.00001, 180), (-.00001, 270), (85, 0)):
        for shadows in (0, 1, 2):
            study.pair(f"sun-{elevation}-{azimuth}-shadows-{shadows}", common +
                       ["--sun-elevation", str(elevation), "--sun-azimuth", str(azimuth),
                        "--shadows", str(shadows)], 2)
    for label, flags in (("ao-ground-soft", ["--ao", "--ground", "--shadows", "2"]),
                         ("clay-ao-ground-soft", ["--clay", "--ao", "--ground", "--shadows", "2"]),
                         ("bump-disabled", ["--no-bump"]),
                         ("procedural-disabled", ["--no-procedural"])):
        study.pair(label, common + flags, 2)
    # Keep the admitted >=0.6 orbit distance. A clipped enclosing sphere lets
    # the actual camera start just outside/inside a domain face at that distance.
    enclosing = json.loads((ROOT / "projects/sdf/scenes/material-study/sphere-plain.json").read_text())
    enclosing["root"]["radius"] = .8
    enclosing.pop("geometry_hash", None)
    enclosing.pop("appearance_hash", None)
    path = args.output / "clipped-enclosing-sphere.json"
    save(path, enclosing)
    for label, z in (("near-surface", .4991), ("inside", .4989)):
        study.pair(label, common + ["--authored-scene", str(path), "--distance", ".6",
                    "--pitch", "0", "--yaw", str(math.acos(z / .6)), "--target-y", "0"], 2)
    study.receipt["complete"] = True
    save(args.output / "qualification.json", study.receipt)


def timing(args):
    q = json.loads((args.qualification / "qualification.json").read_text())
    if not q.get("complete") or q["binary_sha256"] != digest(args.binary):
        raise RuntimeError("timing requires this binary's completed qualification")
    receipt = dict(metadata(args.binary), runner_sha256=digest(Path(__file__)),
                   frames=180, warmup=30, modes=args.modes, runs=[])
    cases = [(scene, preset, view, offset, mode) for scene, preset in
             (("guardian", "sculpture-guardian-limestone"), ("robot", "survey-robot-fieldkit"))
             for view, offset in (("preset", 0), ("side", math.pi / 2), ("rear", math.pi))
             for mode in args.modes]
    for repeat in range(1, 4):
        for scene, preset, view, offset, mode in cases if repeat % 2 else reversed(cases):
            prefix = args.output / f"{scene}-{view}-{mode}-r{repeat}"
            config = json.loads((ROOT / f"projects/sdf/presets/{preset}.json").read_text())
            command = [str(args.binary), "--headless", "--headless-render-only", "--require-validation",
                       "--supersample", "1", "--config", f"projects/sdf/presets/{preset}.json",
                       "--width", "1920", "--height", "1080", "--surface-trace", "analytic",
                       "--csg-evaluation", "pruned", "--region-axis", "64",
                       "--acceleration-variant", "root-far", "--source-optimization", mode,
                       "--yaw", str(config["yaw"] + offset), "--frames", "180",
                       "--profile-warmup-frames", "30", "--profile-output", str(prefix)]
            before = gpu_state()
            wall = invoke(command, prefix, timeout=180)
            after = gpu_state()
            groups = {}
            for row in csv.DictReader(prefix.with_suffix(".passes.csv").open()):
                if row["kind"] == "gpu":
                    groups.setdefault(row["label"], []).append(float(row["duration_ms"]))
            if len(groups.get("sdf.whole_scene", [])) != 150:
                raise RuntimeError("incomplete GPU timestamp tail")
            metrics = list(csv.DictReader(prefix.with_suffix(".metrics.csv").open()))
            selection = {r["name"]: float(r["value"]) for r in metrics if r["category"] == "sdf.shader_selection"}
            eligible = scene == "guardian"
            if selection["route"] != (2 if eligible else 1) or selection["pipeline_count"] != 3:
                raise RuntimeError("timing bound an unexpected source pipeline")
            if selection["shadow_optimized"] != int(eligible and mode in ("shadow", "automatic", "combined")):
                raise RuntimeError("timing shadow mode is not active as requested")
            if "cones_optimized" in selection and selection["cones_optimized"] != int(eligible and mode in ("prepared-cones", "combined")):
                raise RuntimeError("timing cone mode is not active as requested")
            resources = {r["name"]: float(r["value"]) for r in metrics if r["category"] == "sdf.prepared_cones"}
            receipt["runs"].append(dict(scene=scene, view=view, mode=mode, repeat=repeat, command=command,
                gpu_before=before, gpu_after=after, wall_seconds=wall, selection=selection, prepared_resources=resources,
                passes={k: dict(samples=len(v), median_ms=statistics.median(v), p95_ms=percentile(v, .95))
                        for k, v in groups.items()}))
            save(args.output / "timing.json", receipt)
            print(prefix.name, round(receipt["runs"][-1]["passes"]["sdf.whole_scene"]["p95_ms"], 4), flush=True)
    receipt["complete"] = True
    save(args.output / "timing.json", receipt)


def cones(args):
    candidate_mode = args.source_optimization if args.source_optimization in ("prepared-cones", "combined") else "prepared-cones"
    args.source_optimization = candidate_mode
    study = Study(args)
    study.receipt["candidate_mode"] = candidate_mode
    common = route_flags(ROUTES[-1])
    manifest = ROOT / "projects/sdf/comparison/fixtures.json"
    result = args.output / "prepared-fixtures.json"
    study.run("prepared-fixtures", ["--width", "32", "--height", "32", *common,
              "--comparison-input", str(manifest), "--comparison-output", str(result)], render_only=True)
    query_receipt = json.loads(result.read_text())
    if not query_receipt.get("prepared_cones") or not query_receipt["query_shader"].startswith("prepared_"):
        raise RuntimeError("frozen cone checks did not select the prepared query kernel")
    if query_receipt["query_shader_sha256"] != digest(args.binary.parent / "shaders" / query_receipt["query_shader"]):
        raise RuntimeError("prepared query shader identity mismatch")
    score = check(json.loads(manifest.read_text()), query_receipt)
    if not score["passed"]:
        raise RuntimeError("prepared frozen independent oracle failed")
    study.receipt["fixtures"] = score
    study.receipt["image_comparisons"] = []
    for scene, preset in (("guardian", "sculpture-guardian-limestone"), ("robot", "survey-robot-fieldkit")):
        config = json.loads((ROOT / f"projects/sdf/presets/{preset}.json").read_text())
        for view, offset in (("preset", 0), ("side", math.pi / 2), ("rear", math.pi)):
            flags = ["--config", f"projects/sdf/presets/{preset}.json", "--supersample", "1",
                     "--width", "1920", "--height", "1080", *common, "--yaw", str(config["yaw"] + offset)]
            args.source_optimization = "reference"
            a = study.run(scene + "-" + view + "-reference", flags)
            args.source_optimization = candidate_mode
            b = study.run(scene + "-" + view + "-prepared", flags)
            wa, ha, ca, ra = pixels(args.output / (a["name"] + ".png"))
            wb, hb, cb, rb = pixels(args.output / (b["name"] + ".png"))
            if (wa, ha, ca) != (wb, hb, cb):
                raise RuntimeError("prepared image dimensions differ")
            errors = [max(abs(ar[i+j] - br[i+j]) for j in range(3))
                      for ar, br in zip(ra, rb) for i in range(0, len(ar), ca)]
            changed = sum(e != 0 for e in errors)
            study.receipt["image_comparisons"].append(dict(scene=scene, view=view,
                changed_pixels=changed, max_channel_error=max(errors),
                mean_max_channel_error=statistics.mean(errors), reference_sha256=a["image_sha256"],
                prepared_sha256=b["image_sha256"]))
            if scene == "robot" and changed:
                raise RuntimeError("multiple-material fallback changed pixels")
            print(scene, view, "prepared pixels changed", changed, "maximum", max(errors), flush=True)
            save(args.output / "qualification.json", study.receipt)
    # Source indices move when a cone subtree is reordered; geometry and material
    # changes stress the actual complete candidate/descriptor publication path.
    initial = json.loads((ROOT / "projects/sdf/scenes/material-study/sphere-plain.json").read_text())
    initial.pop("history", None)
    initial["root"] = dict(type="round-cone", name="initial-cone", bottom_radius=.1,
                           top_radius=.04, half_length=.16, material="body")
    edited = copy.deepcopy(initial)
    edited["root"] = dict(type="union", name="two-cones", children=[
        dict(type="round-cone", name="left", bottom_radius=.06, top_radius=.025,
             half_length=.13, position=[-.12, 0, 0], material="body"),
        dict(type="round-cone", name="right", bottom_radius=.08, top_radius=.03,
             half_length=.16, position=[.12, 0, 0], material="body")])
    reordered = copy.deepcopy(edited)
    reordered["root"]["children"].reverse()
    appeared = copy.deepcopy(reordered)
    appeared["materials"][0]["base_color"] = [.2, .5, .3]
    two_materials = copy.deepcopy(appeared)
    two_materials["materials"].append(dict(name="unused", base_color=[.1,.2,.3], roughness=.7))
    actions = []
    def add(label, scene, visible=None, fault=None, unchanged=False, render=None, route="lit-single-material"):
        row = dict(label=label, scene=copy.deepcopy(scene), visible_scene=copy.deepcopy(scene if visible is None else visible),
                   geometry_unchanged=unchanged, expect_shader=route)
        if fault: row.update(fault=fault, expect_failure=True)
        if render: row["render"] = render
        actions.append(row)
    add("initial", initial, unchanged=True)
    for fault in ("cone-prepare", "cone-upload", "appearance-upload", "descriptors", "activate"):
        add("fail-"+fault, edited, visible=initial, fault=fault, unchanged=True)
    add("retry-geometry", edited)
    add("reorder-source-indices", reordered)
    add("appearance-only", appeared, unchanged=True)
    add("register-second-material", two_materials, unchanged=True, route="lit-general")
    add("undo-registration", appeared, unchanged=True)
    add("debug-fallback", appeared, unchanged=True, render=dict(debug=1), route="general")
    add("return-to-lit", appeared, unchanged=True, render=dict(debug=0))
    add("undo-geometry", initial)
    add("redo-geometry", reordered)
    save(args.output / "cone-initial.json", initial)
    replay_rows = {}
    for mode in ("reference", candidate_mode):
        manifest_actions = copy.deepcopy(actions)
        if mode == "reference":
            # The original path has no new cone preparation or descriptor stage.
            # Its retained early region fault supplies the matching visible state.
            for row in manifest_actions:
                if "fault" in row: row["fault"] = "region"
        path = args.output / (mode + "-cone-actions.json")
        save(path, dict(format="cubey.sdf.action-study", version=1, actions=manifest_actions, capture_frames=True))
        result = args.output / (mode + "-cone-replay.json")
        args.source_optimization = mode
        study.run(mode + "-cone-replay", [*common, "--width", "320", "--height", "180",
                  "--authored-scene", str(args.output / "cone-initial.json"), "--comparison-actions", str(path),
                  "--comparison-output", str(result)], frames=len(actions), render_only=True)
        replay_rows[mode] = json.loads(result.read_text())["actions"]
    candidate = replay_rows[candidate_mode]
    study.receipt["action_image_comparisons"] = []
    fresh_images = {}
    debug = 0
    for a, b in zip(replay_rows["reference"], candidate):
        _, _, channels, reference_pixels = pixels(Path(a["frame_png"]))
        _, _, _, candidate_pixels = pixels(Path(b["frame_png"]))
        errors = [max(abs(ar[i+j]-br[i+j]) for j in range(3)) for ar, br in
                  zip(reference_pixels, candidate_pixels) for i in range(0,len(ar),channels)]
        study.receipt["action_image_comparisons"].append(dict(label=b["label"],
            changed_pixels=sum(e!=0 for e in errors), max_channel_error=max(errors)))
        action = actions[b["frame"]]
        debug = action.get("render", {}).get("debug", debug)
        key = (json.dumps(action["visible_scene"], sort_keys=True), debug)
        if key not in fresh_images:
            label = "fresh-cone-state-"+str(len(fresh_images))
            path = args.output / (label+".json")
            save(path, action["visible_scene"])
            args.source_optimization = candidate_mode
            fresh = study.run(label, [*common,"--width","320","--height","180",
                "--authored-scene",str(path),"--debug",str(debug)])
            fresh_images[key] = fresh["image_sha256"]
        if digest(Path(b["frame_png"])) != fresh_images[key]:
            raise RuntimeError("retained cone generation differs from fresh native state: "+b["label"])
        if b["shader_pipeline_count"] != 3:
            raise RuntimeError("cone replay compiled another pipeline")
    first_image = digest(Path(candidate[0]["frame_png"]))
    if any(digest(Path(row["frame_png"])) != first_image for row in candidate if row["failed"]):
        raise RuntimeError("failed cone publication changed retained pixels")
    if any(r["cone_build_delta"] for r in candidate if r["label"].startswith("fail-") or r["label"] in
           ("initial", "appearance-only", "register-second-material", "undo-registration", "debug-fallback", "return-to-lit")):
        raise RuntimeError("cone appearance/failure replaced published geometry cache")
    if any(r["cone_build_delta"] != 1 for r in candidate if r["label"] in
           ("retry-geometry", "reorder-source-indices", "undo-geometry", "redo-geometry")):
        raise RuntimeError("cone geometry/reorder/undo failed to replace cache")
    study.receipt["action_frames"] = len(actions)
    study.receipt["fresh_native_states"] = len(fresh_images)
    study.receipt["complete"] = True
    save(args.output / "qualification.json", study.receipt)


def publication(args):
    study = Study(args)
    initial = json.loads((ROOT / "projects/sdf/scenes/sculpture-guardian-limestone.json").read_text())
    edited = copy.deepcopy(initial)
    def edit_cone(node):
        if node.get("type") == "round-cone":
            node["bottom_radius"] *= 1.03
            return True
        return any(edit_cone(child) for child in node.get("children", []))
    if not edit_cone(edited["root"]):
        raise RuntimeError("Guardian publication fixture found no cone")
    for key in ("geometry_hash", "appearance_hash"):
        edited.pop(key, None)
    appearance = copy.deepcopy(edited)
    appearance["materials"][0]["base_color"] = [.3, .4, .5]
    appearance.pop("appearance_hash", None)
    save(args.output / "guardian-initial.json", initial)
    actions = [dict(label="initial", scene=initial, visible_scene=initial, geometry_unchanged=True),
               dict(label="failed-full-cone-upload", scene=edited, visible_scene=initial, fault="cone-upload", expect_failure=True, geometry_unchanged=True),
               dict(label="retry-full-geometry", scene=edited, visible_scene=edited),
               dict(label="full-appearance-only", scene=appearance, visible_scene=appearance, geometry_unchanged=True),
               dict(label="undo-full-geometry", scene=initial, visible_scene=initial)]
    study.receipt["publication"] = []
    for mode in ("reference", "shadow", "prepared-cones", "combined"):
        manifest = copy.deepcopy(actions)
        if mode in ("reference", "shadow"):
            manifest[1]["fault"] = "region"
        path = args.output / (mode+"-actions.json")
        save(path, dict(format="cubey.sdf.action-study", version=1, actions=manifest, capture_frames=True))
        result = args.output / (mode+"-replay.json")
        args.source_optimization = mode
        study.run(mode+"-guardian-publication", [*route_flags(ROUTES[-1]), "--width", "320", "--height", "180",
                  "--authored-scene", str(args.output / "guardian-initial.json"), "--comparison-actions", str(path),
                  "--comparison-output", str(result)], frames=len(actions), render_only=True)
        rows = json.loads(result.read_text())["actions"]
        if digest(Path(rows[0]["frame_png"])) != digest(Path(rows[1]["frame_png"])) or digest(Path(rows[0]["frame_png"])) != digest(Path(rows[-1]["frame_png"])):
            raise RuntimeError("full Guardian failure/undo did not retain/restore pixels")
        study.receipt["publication"].append(dict(mode=mode, actions=rows))
        save(args.output / "qualification.json", study.receipt)
    study.receipt["complete"] = True
    save(args.output / "qualification.json", study.receipt)


def contracts(args):
    study = Study(args)
    frozen = json.loads((ROOT / "projects/sdf/comparison/fixtures.json").read_text())
    tolerance = frozen["contracts"]["regular_depth_tolerance"]
    epsilon = frozen["contracts"]["epsilon"]
    cases = []
    def ray(origin, direction, depth, normal=None, tangent_radius=None):
        q = dict(kind="ray", origin=origin, direction=direction, expected_t=depth,
                 depth_tolerance=tolerance, classification="regular")
        if normal is not None: q["expected_normal"] = normal
        if tangent_radius is not None:
            q.update(classification="tangent", depth_tolerance=math.sqrt(2*tangent_radius*epsilon+epsilon*epsilon)+4e-6)
        return q
    def add(name, root, queries):
        scene = dict(format="cubey.sdf.scene", version=2, name=name,
                     materials=[dict(name="body", base_color=[.5,.4,.3], roughness=.6)],
                     root=dict(name=name, type="round-cone", material="body", **root))
        cases.append(dict(name=name, scene=scene, queries=queries))
    add("equal-radii-capsule", dict(bottom_radius=.1, top_radius=.1, half_length=.15),
        [ray([0,0,.7],[0,0,-1],.6,[0,0,1]), ray([0,0,0],[1,0,0],.1,[1,0,0]),
         ray([0,.7,0],[0,-1,0],.45,[0,1,0]), ray([.1,0,.7],[0,0,-1],.7,tangent_radius=.1),
         dict(kind="point", position=[0,0,.11], expected_distance=.01)])
    add("domain-clip-and-inside", dict(bottom_radius=.8,top_radius=.8,half_length=.15),
        [ray([0,0,.7],[0,0,-1],.201,[0,0,1]),ray([0,0,0],[0,0,1],.499,[0,0,1]),
         dict(kind="point",position=[.51,0,0],expected_distance=.011)])
    axis=[-math.sqrt(.5),math.sqrt(.5),0]
    center=[.1,-.05,0]
    plus=lambda a,b:[x+y for x,y in zip(a,b)]
    times=lambda a,k:[x*k for x in a]
    add("rotated-scaled-capsule",dict(bottom_radius=.1,top_radius=.1,half_length=.15,
        scale=.6,position=center,rotation_xyzw=[0,0,math.sin(math.pi/8),math.cos(math.pi/8)]),
        [ray(plus(center,times(axis,.7)),times(axis,-1),.55,axis),
         ray(plus(center,[0,0,.7]),[0,0,-1],.64,[0,0,1]),ray(center,axis,.15,axis)])
    add("zero-top-radius",dict(bottom_radius=.1,top_radius=0,half_length=.2),
        [ray([0,.7,0],[0,-1,0],.5),dict(kind="point",position=[0,.21,0],expected_distance=.01)])
    add("zero-bottom-radius",dict(bottom_radius=0,top_radius=.1,half_length=.2),
        [ray([0,.7,0],[0,-1,0],.4,[0,1,0]),ray([0,-.7,0],[0,1,0],.5)])
    bits=struct.unpack("I",struct.pack("f",.2))[0]
    for offset in (0,1,2):
        height=struct.unpack("f",struct.pack("I",bits+offset))[0]
        top=max(-height/2+.3,height/2+.1)
        bottom=min(-height/2-.3,height/2-.1)
        add("containing-boundary-ulp-"+str(offset),dict(bottom_radius=.3,top_radius=.1,half_length=height/2),
            [ray([0,.7,0],[0,-1,0],.7-top,[0,1,0]),ray([0,-.7,0],[0,1,0],.7+bottom,[0,-1,0])])
    manifest=dict(format=frozen["format"],version=frozen["version"],contracts=frozen["contracts"],cases=cases)
    path=args.output/"supplemental-contracts.json"
    save(path,manifest)
    study.receipt["supplemental_contracts"] = []
    for mode in ("reference","shadow","prepared-cones","combined"):
        args.source_optimization=mode
        result=args.output/(mode+"-contracts.json")
        study.run(mode+"-contracts",[*route_flags(ROUTES[-1]),"--width","32","--height","32",
                  "--comparison-input",str(path),"--comparison-output",str(result)],render_only=True)
        score=check(manifest,json.loads(result.read_text()))
        if not score["passed"]: raise RuntimeError("supplemental cone contract failed: "+json.dumps(score))
        study.receipt["supplemental_contracts"].append(dict(mode=mode,score=score))
    # Check actual CLI rejection without relaxing any existing root-far guards.
    base=[str(args.binary),"--headless","--headless-render-only","--frames","1",
          "--surface-trace","analytic","--csg-evaluation","pruned","--region-axis","64",
          "--acceleration-variant","root-far","--authored-scene","projects/sdf/scenes/material-study/sphere-plain.json"]
    study.receipt["rejections"] = []
    for mode in ("prepared-cones","combined"):
        for label,flags in (("windowed",[]),("hybrid",["--surface-trace","hybrid"]),
                            ("general",["--source-shaders","general"]),("lighting",["--lighting","material-study"]),
                            ("axis",["--region-axis","16"]),("motion",["--motion","orbit"])):
            command=[x for x in base if x!="--headless"] if label=="windowed" else base.copy()
            if label=="windowed": command.remove("--headless-render-only")
            command += ["--source-optimization",mode,*flags]
            run=subprocess.run(command,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            if run.returncode==0 or "requires" not in run.stdout:
                raise RuntimeError("unexpected optimization admission: "+mode+"/"+label+": "+run.stdout)
            study.receipt["rejections"].append(dict(mode=mode,label=label,command=command,exit=run.returncode,output=run.stdout))
    study.receipt["complete"]=True
    save(args.output/"qualification.json",study.receipt)


def history(args):
    study=Study(args)
    scene=dict(format="cubey.sdf.scene",version=2,name="Cone history contract",
        materials=[dict(name="body",base_color=[.5,.4,.3],roughness=.6)],
        root=dict(type="union",name="cone-history-root",children=[
            dict(type="round-cone",name="left",bottom_radius=.08,top_radius=.03,half_length=.15,position=[-.12,0,0],material="body"),
            dict(type="round-cone",name="right",bottom_radius=.06,top_radius=.02,half_length=.12,position=[.12,0,0],material="body")]),
        history=[dict(node="left",label="Left cone",seconds=1),dict(node="right",label="Right cone",seconds=1)])
    branch=copy.deepcopy(scene)
    branch["root"]["children"][0]["bottom_radius"] = .09
    appearance=copy.deepcopy(branch)
    appearance["materials"][0]["base_color"] = [.2,.5,.4]
    rows=[("empty",scene,0), ("first",scene,1), ("second",scene,2), ("undo",scene,1),
          ("redo",scene,2), ("branch-first",branch,1), ("branch-appearance",appearance,1),
          ("failed-prefix",appearance,2), ("retry-prefix",appearance,2)]
    save(args.output/"initial.json",scene)
    study.receipt["history"] = []
    for mode in ("reference","shadow","prepared-cones","combined"):
        actions=[]
        for label,source,prefix in rows:
            action=dict(label=label,scene=source,history_prefix=prefix,visible_scene=source,
                visible_history_prefix=prefix,geometry_unchanged=label in ("branch-appearance","failed-prefix"))
            if label=="failed-prefix":
                action.update(visible_history_prefix=1,expect_failure=True,
                              fault="cone-upload" if mode in ("prepared-cones","combined") else "region")
            actions.append(action)
        path=args.output/(mode+"-history-actions.json")
        save(path,dict(format="cubey.sdf.action-study",version=1,actions=actions,capture_frames=True))
        result=args.output/(mode+"-history-replay.json")
        args.source_optimization=mode
        study.run(mode+"-authored-history",[*route_flags(ROUTES[-1]),"--width","320","--height","180",
            "--authored-scene",str(args.output/"initial.json"),"--comparison-actions",str(path),
            "--comparison-output",str(result)],frames=len(actions),render_only=True)
        observed=json.loads(result.read_text())["actions"]
        for a,b in ((1,3),(2,4),(6,7)):
            if digest(Path(observed[a]["frame_png"]))!=digest(Path(observed[b]["frame_png"])):
                raise RuntimeError("authored history failed exact undo/redo/retention: "+mode)
        if mode in ("prepared-cones","combined"):
            expected=[1,1,1,1,1,1,0,0,1]
            if [r["cone_build_delta"] for r in observed]!=expected:
                raise RuntimeError("authored history prepared-cache generations do not follow geometry")
        study.receipt["history"].append(dict(mode=mode,actions=observed))
    study.receipt["complete"]=True
    save(args.output/"qualification.json",study.receipt)


def lighting(args):
    study=Study(args)
    scene=dict(format="cubey.sdf.scene",version=2,name="Prepared cone lighting",
        materials=[dict(name="body",base_color=[.5,.4,.3],roughness=.6,
            procedural=dict(type="marble",seed=9,scale=12,color_strength=.7,
                secondary_color=[.1,.2,.4],bump_strength=.0007))],
        root=dict(type="round-cone",name="cone",bottom_radius=.1,top_radius=.04,
                  half_length=.16,material="body"))
    path=args.output/"lighting-cone.json"
    save(path,scene)
    study.receipt["lighting_images"] = []
    for label,flags in (("ao-ground-soft",["--ao","--ground","--shadows","2"]),
            ("clay-ao-ground-soft",["--clay","--ao","--ground","--shadows","2"]),
            ("bump-off",["--no-bump"]),("procedural-off",["--no-procedural"]),
            ("grazing-sun",["--sun-elevation","0","--sun-azimuth","180"]),
            ("raster-occluder",["--occluder"])):
        captures={}
        for mode in ("reference","prepared-cones","combined"):
            args.source_optimization=mode
            captures[mode]=study.run(label+"-"+mode,[*route_flags(ROUTES[-1]),"--width","320","--height","180",
                "--authored-scene",str(path),*flags])
        if captures["prepared-cones"]["image_sha256"]!=captures["combined"]["image_sha256"]:
            raise RuntimeError("shadow culling changed prepared lighting pixels: "+label)
        _,_,channels,a=pixels(args.output/(captures["reference"]["name"]+".png"))
        _,_,_,b=pixels(args.output/(captures["combined"]["name"]+".png"))
        errors=[max(abs(ar[i+j]-br[i+j]) for j in range(3)) for ar,br in zip(a,b) for i in range(0,len(ar),channels)]
        study.receipt["lighting_images"].append(dict(label=label,changed_pixels=sum(e!=0 for e in errors),
            max_channel_error=max(errors),prepared_combined_identical=True))
    study.receipt["complete"]=True
    save(args.output/"qualification.json",study.receipt)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase", choices=["edges", "timing", "cones", "combined", "publication", "contracts", "history", "lighting"])
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--qualification", type=Path)
    p.add_argument("--modes", nargs="+", default=["reference", "shadow"])
    p.add_argument("--source-optimization", default="shadow")
    p.add_argument("--resume", action="store_true", help="Reuse hash-verified cone captures; action and numerical jobs rerun")
    p.add_argument("--binary", type=Path, default=ROOT / "build/release/projects/sdf/sdf")
    args = p.parse_args()
    args.binary = args.binary.resolve()
    args.output = args.output.resolve()
    if args.resume and args.phase not in ("cones", "combined"):
        raise RuntimeError("only cone qualification supports resume")
    args.output.mkdir(parents=True, exist_ok=args.resume)
    if args.phase == "combined":
        args.source_optimization = "combined"
    {"edges": edges, "timing": timing, "cones": cones, "combined": cones, "publication": publication, "contracts": contracts, "history": history, "lighting": lighting}[args.phase](args)


if __name__ == "__main__":
    main()
