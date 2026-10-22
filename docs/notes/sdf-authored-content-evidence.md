# SDF authored content evidence

Date: 2026-10-21

Implementation follows the [authored content plan](sdf-authored-content-plan.md).
This record distinguishes native checks and assistant visual selection from
the user's assessment of the resulting content.

## Robot blockout

`projects/sdf/tools/robot_blockout.py` authors three original silhouettes using
the existing rounded-box/sphere recipe format. Twelve native 960x960 views
cover front, side, rear and three-quarter compositions. Each requires Vulkan
validation; the three hero views also pass the CPU/GPU ray oracle. Receipts
and explicit recipes are under `outputs/sdf/robot-blockout/`.

The assistant selected the medium survey proportions for further work. The
tall version has a less compact body/head relationship; the broad version has
separated leg attachments that would need adjustment. The selected blockout
is recognizable as a toy robot but still needs material separation, an authored
pose and more deliberate detail. This selection is not user visual acceptance.

The new `--clay` option supplies a neutral base color for shape inspection.
Existing scene defaults and recipe geometry remain unchanged. The blockout
renders use hard self-shadows, the shared atmosphere and no ground.
