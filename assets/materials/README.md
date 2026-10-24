# Material recipes

Standalone `cubey.material.recipe` content shared by the mesh and SDF material
workshops. The original seven V1 looks retain their accepted values and hashes;
five V2 studies add distinct surface structure and response. The shared
[`catalog.json`](catalog.json) owns selection metadata and validated recipe paths.

| ID | Look |
| --- | --- |
| plain | Neutral gray |
| limestone | Rough limestone |
| marble | Subtle Balanced marble |
| clay | Dry granular terracotta |
| ceramic | Ivory ceramic |
| bronze | Satin bronze |
| glass | Tinted glass |
| wood | Solid hardwood rings, long grain and end grain |
| rusted-steel | Dielectric oxide patches over reflective steel |
| kraft-cardboard | Matte directional kraft fibers and pulp flecks |
| woven-cloth | Plain weave with restrained grazing sheen |
| frosted-glass | Etched surface with filtered reflection/transmission |

All colors are linear. Domain matrices are column-major. See the
[shared contract](../../docs/architecture/material-recipes.md) for units,
validation, filtering, live publication and geometry-specific optical limits.
Wood/rust use solid coordinates; cloth/paper use explicit material X/Y axes on
oriented swatches. These are stylized procedural studies. Corrugated construction,
loose fibers, arbitrary cloth charts and complete rough refraction are deferred.
The [library evidence](../../docs/notes/material-library-evidence.md) records scope
and native qualification.
