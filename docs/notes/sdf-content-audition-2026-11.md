# Three authored SDF content directions

Date: 2026-11-03

Status: retained first audition. The user found these subjects uninteresting in
their current state and requested more detailed architecture or a diorama;
see the [architectural drafts](sdf-diorama-drafts-2026-11.md).
The direction is authored graphics experiments, with scripted scenes and focused
presentation controls. A general desktop modeler or renewed VR target is not
part of this audition.

Compare the nine selected native stills in the
[content audition](http://starship:8787/reports/798838517a2a99b40238b58c8c7834508d9429570f919b7a0647c76aac1d9311/).
The view buttons switch captured images; the page does not run the renderer.
Scenes and presets are downloadable from each card. The
[report receipt](../evidence/sdf-content-audition-report-2026-11.json) distinguishes
verified delivery from the unavailable browser acceptance check.

## Candidates and recommendation

| Subject | Graphics question | Intact / section leaves | Saved actions | Suggested next step |
| --- | --- | --- | --- | --- |
| Hollow spiral shell | Tapered sweeps, a continuous hollow channel and a coiled silhouette. | 258 / 259 | 6 / 7 | Strongest sculptural candidate: refine the mouth and wall profile, then explore coil growth and a section reveal. |
| Oculus pavilion | Architectural mass, arched passages, an oculus and a separate narrow tower. | 20 / 21 | 7 / 8 | Architectural contrast: improve proportions and the interior focal point before a construction film. |
| Cutaway manifold | Housing bores and a quarter-section preserve the independent shaft and rotor. | 34 / 35 | 6 / 7 | Clearest technical-demo candidate: develop a casting, boring, section and assembly sequence. |

Choose the shell for a visual hero piece, or the manifold for a
construction/cutaway explanation. These are recommendations for the user's
review, not accepted final art. The shell is stylized; the manifold is fictional
and carries no engineering or physics claim.

## Native findings and limits

Six scene variants load through the existing authored-scene path. Each subject
has an intact hero view, section hero view and section side view at 960×720,
Cached, SS1, with native Vulkan validation required. The selected nine images
are six shell/pavilion images from `round2` and three corrected manifold images
from `round3`. The executable and all 220 retained SPV hashes remained unchanged.
Unique leaf names and exact once-only coverage by grouped history were checked.

- The first pavilion, despite having only 20 source leaves, exceeded the fixed
  cache frontier (`272196 > 262144`). The selected model uses 0.82 workspace
  scale. That fits the existing allocation; it does not solve the capacity
  limitation or establish that primitive count predicts cache cost.
- The first manifold's oversized shaft chamber removed part of the neck. The
  final chamber is shorter and lower, preserving the neck connection. Earlier
  manifold images are retained but excluded from the selected views.
- The shell uses 128 segments for each outer/inner sweep. Increasing that from
  64 improved its curvature; small facets remain. Materials are simple colors
  and roughness to concentrate the first review on form.

Named history is saved for future construction experiments. This pass rendered
only final static states: construction playback, incremental editing, frame
rate, animated sections, material realism and a general authoring UI were not
qualified. Existing Robot, Guardian, vessel, arch and driftwood assets are unchanged.

## Sources and reproduction

The deterministic [authoring script](../../projects/sdf/tools/author_content_audition.py)
writes the six committed scenes and presets. Run from the repository root:

```sh
rtk proxy python3 -B projects/sdf/tools/author_content_audition.py
rtk proxy build/release/projects/sdf/sdf --config projects/sdf/presets/content-audition-2026-11/spiral-shell-intact.json --render-mode cached
```

A fresh packet needs a new output directory:

```sh
rtk proxy python3 -B projects/sdf/tools/author_content_audition.py --capture-dir outputs/sdf/content-audition-fresh
```

This runs nine captures serially, retaining exact copied inputs, commands,
logs, images, generator and executable/shader fingerprints. The existing frozen
public executable is the capture dependency; compilation was not part of this
task. Rebuilding later is a new runtime identity.

The dated [report builder](../../projects/sdf/tools/content_audition_report.py)
uses the explicitly selected retained rounds and needs a new export folder:

```sh
rtk proxy python3 -B projects/sdf/tools/content_audition_report.py --output outputs/sdf/content-audition-20261103/report-rebuilt
```

The [evidence summary](../evidence/sdf-content-audition-2026-11/summary.json) and
[retention manifest](../evidence/sdf-content-audition-2026-11/retention.json)
pin the selected views and all three capture rounds, including the failed
pavilion attempt and superseded manifold. No cleanup was performed. Native
images were inspected directly. Report source/asset checks and HTTP delivery
passed; browser interaction acceptance remains unavailable because no browser
was connected during the final handoff.
