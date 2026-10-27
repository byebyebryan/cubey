#!/usr/bin/env python3
"""Bounded study action log; complete source snapshots, independent of caches.

Saved authored reveal histories keep their existing format. This disposable
driver models transactions/undo for qualification, not a persistent editor API.
"""
from copy import deepcopy
import json
from pathlib import Path
import sys
from tracing_comparison import document, sphere


class History:
    def __init__(self, scene):
        self.states = [deepcopy(scene)]
        self.cursor = 0

    @property
    def scene(self):
        return deepcopy(self.states[self.cursor])

    def commit(self, scene):
        self.states = self.states[:self.cursor+1] + [deepcopy(scene)]
        self.cursor += 1

    def undo(self):
        if not self.cursor:
            raise ValueError("no undo")
        self.cursor -= 1

    def redo(self):
        if self.cursor+1 == len(self.states):
            raise ValueError("no redo")
        self.cursor += 1


def build():
    initial = document(dict(name="original-group", type="group", children=[sphere("hidden-A", .1)]))
    h = History(initial)
    rows = []

    def record(label, unchanged=False, scene=None, fault=None):
        row = dict(label=label, scene=deepcopy(scene or h.scene), visible_scene=h.scene,
                   geometry_unchanged=unchanged)
        if fault:
            row.update(fault=fault, expect_failure=True)
        rows.append(row)

    record("initial source", True)
    cover = h.scene
    cover["root"] = dict(name="cover", type="union", children=[cover["root"], sphere("B", .3)])
    h.commit(cover); record("cover A with B")
    h.undo(); assert h.scene == initial; record("undo cover restores A")
    h.redo(); assert h.scene == cover; record("redo cover")
    h.undo(); record("undo before branch")
    branch = h.scene
    branch["root"] = dict(name="branch", type="union", children=[branch["root"], sphere("C", .12, [.25, 0, 0])])
    h.commit(branch); record("new action abandons redo branch")
    try:
        h.redo()
        raise AssertionError("abandoned redo was accepted")
    except ValueError:
        record("redo unavailable", True)
    mirrored = h.scene
    mirrored["root"] = dict(name="mirrored-action", type="union", children=[mirrored["root"],
        sphere("left", .05, [-.2, .2, 0]), sphere("right", .05, [.2, .2, 0])])
    h.commit(mirrored); record("atomic mirrored stroke")
    h.undo(); assert h.scene == branch; record("undo both mirrored sides")
    transformed = h.scene
    group = transformed["root"]["children"][0]
    group.update(position=[0, .1, 0], scale=1.2, rotation_xyzw=[0, .2, 0, (1-.2**2)**.5])
    h.commit(transformed); record("transform earlier named group")
    smooth = h.scene
    smooth["root"] = dict(name="fused", type="smooth-union", radius=.03,
                          children=[smooth["root"], sphere("smooth-ball", .12, [0, -.1, 0])])
    h.commit(smooth); record("smooth fusion")
    changed = h.scene; changed["root"]["radius"] = .07
    h.commit(changed); record("change smooth radius")
    h.undo(); assert h.scene == smooth; record("undo smooth radius")
    appearance = h.scene; appearance["materials"][0]["base_color"] = [.2, .8, .3]
    h.commit(appearance); record("appearance color", True)
    domain = h.scene
    domain["materials"][0]["material_from_object"] = [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, .1, 0, 0, 1]
    h.commit(domain); record("appearance domain", True)
    cut = h.scene
    cut["root"] = dict(name="cut", type="subtract", children=[cut["root"], sphere("bore", .05, [0, .1, 0])])
    h.commit(cut); record("add inherited-material cut")
    owner = h.scene; owner["root"]["cut_material"] = "blue"
    h.commit(owner); record("change cut owner", True)
    retry = h.scene; retry["root"]["children"][1]["position"] = [.03, .1, 0]
    record("region boundary failure retains completed snapshot", True, retry, "region")
    record("field boundary failure retains completed snapshot", True, retry, "field")
    h.commit(retry); record("explicit deterministic retry")
    return initial, dict(format="cubey.sdf.action-study", version=1, actions=rows)


def main():
    destination = Path(sys.argv[1]); destination.mkdir(parents=True, exist_ok=True)
    initial, actions = build()
    (destination/"initial.json").write_text(json.dumps(initial, indent=2)+"\n")
    (destination/"actions.json").write_text(json.dumps(actions, indent=2)+"\n")
    print(json.dumps(dict(actions=len(actions["actions"]), output=str(destination))))


if __name__ == "__main__":
    main()
