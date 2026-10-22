# SDF sculpture study evidence

Date: 2026-10-22

Status: in progress under the [four-subject plan](sdf-sculpture-study-plan.md).
The user requested execution with commits as work progresses. Native visual
assessment below is the assistant's; user acceptance remains separate.

## Clay audition

`tools/author_sculptures.py` writes two explicit blockouts per subject using
only existing primitives and operators. `tools/sculpture_audition.py` captures
hero, front, profile and the committed prefix before the first subtraction.
The 32 native Vulkan-validation captures passed. Receipts and images are in
`outputs/sdf/sculpture-audition/`; the manifest pins binary/shader hashes,
commands, effective source state and native geometry/build statistics. This
first audition does not run the complete ray/history qualification matrix.

| Subject | Leaves per variant | Selection | Visual finding and next revision |
| --- | ---: | --- | --- |
| Guardian | 20 | A, broad head and taller horns | Recognizable face with brow, nose, mouth and cavities. Improve horn taper, eye/mouth expression and the skull-to-neck transition; inspect the profile before ornament. |
| Vessel | 9 | A, fuller asymmetric belly | Mouth, interior, handle and three wall piercings read already. Refine opening rhythm and silhouette; confirm wall/base thickness and inspection from above. |
| Arch | 8 | A, lower crown | Front/rear cavities form a passage, but the smooth mass resembles a tooth. Add directed erosion that changes the outer silhouette and breaks its symmetry. |
| Driftwood | 12 | A, wider branch spread | Flow and fork are readable, but piecewise constant-radius capsules create swollen joins and blunt tips. Improve taper before adding grain-like grooves. |

The capsule stroke helper is deliberately retained for reproducing these
blockouts. A narrowly scoped rounded tapered segment is the shared capability
to evaluate next: horns and branches expose its need directly in native views.
No evidence here establishes a completed sculpture or a successful material
treatment.

```sh
python3 projects/sdf/tools/author_sculptures.py --stage blockout
python3 projects/sdf/tools/sculpture_audition.py \
  --output outputs/sdf/sculpture-audition-new
```

## Progress

- [x] Two clay variants and native shaping checkpoints for all four subjects.
- [ ] Shared tapered-stroke decision and generic qualification/presentation.
- [ ] Guardian modeling, construction presentation and native qualification.
- [ ] Vessel modeling, construction presentation and native qualification.
- [ ] Arch modeling, construction presentation and native qualification.
- [ ] Driftwood modeling, construction presentation and native qualification.
- [ ] Published comparison gallery, delivery review and study verdicts.
