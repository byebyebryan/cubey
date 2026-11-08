# Dragon optical preparation study

2026-11-08. G0's **fidelity gate failed**. Original asset and renderer preserved;
no glass or optical admission. See the
[result and next boundary](../../notes/raytracing-dragon-glass-g0-results.md).

[Interactive native comparison and geometry report](http://starship:8787/reports/de7a5539faaf5e8b71dda23e38edabd320ec2182adea9164fecd7f220aeb1d6f/)
was browser-checked. All 33 published files match the prepared bytes over HTTP
from the source host; the existing shared report server was reused.

Compact records:

- [18 initial method/parameter variants](trial-summary.json).
- [Initial patch serialized audit](patch-manifest.json).
- [Main-shell alternative serialized audit](main-shell-manifest.json).
- [Seven-region distance probes for the initial patch](per-region-probes.json).
- [MeshFix distance probes](meshfix-distance-probe.json).
- [Initial-patch native comparison](native-comparison-patch.json).
- [Main-shell native comparison](native-comparison-main.json).

Distance maxima here are sample witnesses, not Hausdorff upper bounds. Both
comparison candidates exceed the proposed 0.1 mm budget and remain rejected.

## Reproduce the two concrete candidates

From the RT worktree, with the original pinned GLB already prepared:

```sh
rtk proxy uv run --no-project --no-build docs/evidence/raytracing-dragon-optical-preparation-2026-11/study.py --output outputs/raytracing/g0-patch-reproduction
rtk proxy uv run --no-project --no-build docs/evidence/raytracing-dragon-optical-preparation-2026-11/study.py --keep-main-shell --output outputs/raytracing/g0-main-shell-reproduction
rtk proxy uv run --no-project --python 3.11 --with numpy==2.4.6 --with cgal==5.6.post202402071826 --no-build python docs/evidence/raytracing-dragon-optical-preparation-2026-11/test_study.py -v
```

[study.py](study.py) pins Python 3.11, CGAL 5.6.post202402071826 and NumPy 2.4.6.
It rejects an unexpected source checksum and reused output paths, preserves
unaffected source corner order and attributes, and audits the **reimported
serialized float32 GLB**. It never writes a model cache or an admitted asset.
The default trial retains the original shading normals; the explicit main-shell
alternative assigns geometric normals only to new patch corners. Component
filtering can overwrite CGAL's internal vertex IDs, so source identity is
recovered from exact unchanged coordinates before face extraction.

The [eight controls](test_study.py) cover a closed tetrahedron, disconnected
closed components, an open surface, inconsistent winding, a pinched vertex,
float32 collapse, crossing-solid positive/negative predicates and source identity
after component filtering. No CGAL dependency is added to the renderer or build.

All experimental inputs, additional prototype scripts, GLBs and raw captures
remain under ignored `outputs/raytracing/dragon-optical-preparation-20261108/`.
`native-captures.json` and `main-captures.json` contain the capture commands;
`starting-state.json` records the protected starting hashes. The report bundle
under that directory contains only explicitly prepared report assets.

Methods: [CGAL hole filling](https://doc.cgal.org/5.6/Polygon_mesh_processing/group__PMP__hole__filling__grp.html),
[MeshFix API](https://pymeshfix.pyvista.org/_autosummary/pymeshfix.PyTMesh.html),
[MeshLib repair example](https://github.com/MeshInspector/MeshLib/blob/master/python_scripts/mesh_repair.py).
