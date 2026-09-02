# Cubey showcase media

This directory is the committed media package for the four approved root
highlights, in editorial order: Ocean, glTF + Terrain, Water 3D, and Fire 3D.
Each poster is generated from its corresponding committed MP4. Click a poster
to open the committed MP4 fallback:

| Highlight | Poster / committed MP4 |
| --- | --- |
| Ocean | [![Ocean](ocean.png)](ocean.mp4) |
| glTF + Terrain | [![glTF + Terrain](gltf-terrain.png)](gltf-terrain.mp4) |
| Water 3D | [![Water 3D](water-3d.png)](water-3d.mp4) |
| Fire 3D | [![Fire 3D](fire-3d.png)](fire-3d.mp4) |

The four MP4s are silent, fast-start H.264 High 1280x720 publications at exact
60 FPS. `manifest.json` records the byte hashes, poster timestamps, capture
recipes, source revisions, capture-time dirty-diff provenance, and licensing.

The higher-quality capture masters and audition comparisons remain ignored
under `outputs/showcase/audition-2/`; they are not copied into this package.
There is no merged hero reel.

## GitHub attachment publication

The exact committed MP4s were uploaded in editorial order through GitHub's
Markdown attachment flow and anchored by closed [issue #1](https://github.com/byebyebryan/cubey/issues/1),
with source commit `e21fdd86ba83049d0d187caf3973b1124163e576`. The issue is
closed with reason `completed`; the same mapping is recorded in `manifest.json`.
GitHub's native-player URLs are the README presentation layer. The committed
MP4 and poster links below remain the inspectable, hashable fallback artifacts.

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
