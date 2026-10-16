# Cloud studies

[TerrainEngine cloud reference](terrainengine/README.md) is a maintenance-only
local/surface baseline. It keeps its own source-derived shaders and diagnostics
for explicit comparisons; `CUBEY_BUILD_CLOUD_STUDIES` is off by default.

Production clouds are a shared atmosphere/environment subsystem, demonstrated
through [Atmosphere](../../projects/atmosphere/README.md), not a separate `cloud_ref`
project. New cloud work belongs in that shared path. See the
[cloud architecture](../../docs/architecture/cloud-rendering.md) for ownership,
consumer contracts, and the deferred aerial/orbit boundary.

The 2026-10-15 reclassification moved `projects/cloud_ref` here without changing
its C++ or shader algorithms. Source attribution, reference identifiers, and
historical output names remain intact; former source/tool paths are indexed in
the [relocation map](../relocations.json).
