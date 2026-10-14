# Cubey showcase media

This directory keeps four published highlights and the packaged Fluid 2.5D
candidate reproducible and independent of GitHub's presentation layer. The
published root highlights use native video players;
these committed MP4s, generated posters, and the manifest are the durable,
hashable source of truth.

The published gallery order is Ocean, glTF + Terrain, Water 3D, and Fire 3D.
Fluid 2.5D is the fifth packaged candidate, with owner creative review and native
attachment publication still pending. Each poster
is extracted from its corresponding committed MP4; click one to open the clip:

| Highlight | Poster / committed MP4 |
| --- | --- |
| Ocean | [![Ocean](ocean.png)](ocean.mp4) |
| glTF + Terrain | [![glTF + Terrain](gltf-terrain.png)](gltf-terrain.mp4) |
| Water 3D | [![Water 3D](water-3d.png)](water-3d.mp4) |
| Fire 3D | [![Fire 3D](fire-3d.png)](fire-3d.mp4) |
| Fluid 2.5D (packaged, not uploaded) | [![Fluid 2.5D](fluid-25d.png)](fluid-25d.mp4) |

Fluid's supporting [36-second project film](fluid-25d-story.mp4) shows rain,
stream formation, collection and retreat. Its [complete recipe](fluid-25d-recipe.json)
records native state cadence, timelapse intervals and provenance. Both films
render real saved states; they are not live-FPS demonstrations or interpolated
hydraulic fields. Existing four media files and attachment receipts are unchanged.

The highlights are silent, fast-start H.264 High publications at 1280x720 and
exactly 60 FPS. `manifest.json` records their byte hashes, poster timestamps,
capture recipes, source revisions, capture-time dirty-diff provenance,
attachment URLs, and licensing.

Higher-quality capture masters, comparisons, and diagnostic media remain in
the ignored `outputs/showcase/audition-2/` workspace. They are editing evidence,
not part of the repository package.

## GitHub attachment publication

The exact committed MP4s were uploaded through GitHub's Markdown attachment
flow and anchored by closed [issue #1](https://github.com/byebyebryan/cubey/issues/1)
at source commit `e21fdd86ba83049d0d187caf3973b1124163e576`. The same mapping
is recorded in `manifest.json`. GitHub serves those attachments to the native
players; the repository copies below remain independently inspectable.

| Highlight | Native player attachment | Committed MP4 | Poster fallback |
| --- | --- | --- | --- |
| Ocean | https://github.com/user-attachments/assets/dc923c55-1061-402f-92ad-09b7da2e7208 | [MP4](ocean.mp4) | [Poster](ocean.png) |
| glTF + Terrain | https://github.com/user-attachments/assets/a6119257-6810-440d-b910-e247274fbd02 | [MP4](gltf-terrain.mp4) | [Poster](gltf-terrain.png) |
| Water 3D | https://github.com/user-attachments/assets/d1267204-7857-40d8-a259-4205605a5a75 | [MP4](water-3d.mp4) | [Poster](water-3d.png) |
| Fire 3D | https://github.com/user-attachments/assets/50052e2e-21f0-46fe-be94-9d8a93a657d0 | [MP4](fire-3d.mp4) | [Poster](fire-3d.png) |

Cubey source and the Ocean, Water 3D, and Fire 3D generated media are MIT
licensed. The glTF + Terrain MP4 and poster incorporate the Khronos Damaged
Helmet asset and are a media-only `CC-BY-NC-4.0` exception. They must be
attributed to ctxwing (2018 rebuild/conversion) and theblueturtle_ (2016
earlier model); see the upstream
[Damaged Helmet source](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet).
This exception does not relicense Cubey source, and the glTF media is not
permissively reusable.
