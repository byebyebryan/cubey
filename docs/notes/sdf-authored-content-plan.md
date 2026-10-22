# SDF authored content plan

Date: 2026-10-21

Status: implementation in progress after the [procedural prototype closeout](sdf-procedural-prototype-closeout.md).
The [authored content evidence](sdf-authored-content-evidence.md) records native
checkpoints and visual decisions.
The user selected a small stylized robot or mechanical toy as the first subject.
Appearance uses plain materials and, optionally, procedural or noise patterns.
Image textures and UV mapping are outside this experiment.

## First piece

Author one recognizable tabletop robot, with its design judged through native
renders before expanding the renderer. The first piece tests whether scripted
authoring by the assistant can produce an appealing object. A good technical
implementation alone does not establish that result.

The proposed design is a squat survey robot: a broad rounded head, one recessed
optical sensor, a compact body, short articulated limbs and an asymmetric
antenna or small backpack. Distinct proportions, purposeful gaps and a clear
pose should carry the design. These details are a starting brief to revise at
the blockout checkpoint.

Start with three or four materials: warm ivory housing, a muted teal accent,
dark rubber joints and a restrained amber sensor. Use opaque surfaces and
roughness differences. The sensor can use a small amount of emission without
requiring glass. Detailed surface patterns follow only if the plain version
already reads well.

Aim for 20–40 primitive leaves, with an initial implementation ceiling of 64
leaves and 128 compiled instructions. These are proposed bounds for this asset,
not extensions to the existing 65,536-edit recipe limit. Fit the robot within
the current workspace with room around its silhouette. Keep the shared
atmosphere, hard shadows and existing camera/capture foundation.

## Delivery sequence

| Stage | Work and reviewable output | Acceptance checkpoint |
| --- | --- | --- |
| A. Gray blockout | Make two or three proportion variants using existing boxes and spheres. Capture front, side, rear and three-quarter views in a neutral material. | Choose a readable silhouette and pose before adding detail or new renderer features. Revise or change the design if none works. |
| B. Authored scene foundation | Add named parts, explicit grouped composition, deterministic scene export and a bounded CPU/GPU evaluator. Prove it first on a few small fixtures. | A cut within one group cannot affect another part; exported/reloaded scenes preserve structure and geometry; native rays agree with the CPU reference. |
| C. Plain materials | Add material assignments, cut-surface rules, surface-hit material evaluation and material diagnostics. Apply the small robot palette using hard material boundaries first. | Housing, accents, joints and sensor remain distinct as the camera moves. Changing material values does not rebuild distance bricks. |
| D. Shape vocabulary | Add capsules and capped cylinders as needed, then one conservative smooth-union operator and its material-blending rules. Refine the selected blockout using only the additions it needs. | Joints, sockets and rounded transitions work from all selected views, including grazing and shadow rays. |
| E. Authored detail | Refine openings, panel divisions, feet, pose and asymmetry. Optionally add one subtle procedural treatment after comparing plain and patterned captures. | The object is appealing in a full-frame view; noise and detail support the design rather than conceal a weak blockout. |
| F. Presentation and qualification | Produce a hero still, the fixed inspection views and a short turntable. Record the complete scene, settings, runtime artifacts and costs. | The captures demonstrate the authored result, preserve correctness and state the performance envelope. User visual review determines whether this first content attempt succeeds. |

Stage A can use the current renderer. Stage C produces a colored robot before
the additional primitives and smooth operators in Stage D. Keep each foundation
addition reviewable, and revisit the first piece at every checkpoint.

## Authoring and scene representation

Use a small external Python authoring helper to express dimensions, part
placement, symmetry and repeated details. It emits an explicit versioned scene
file; Python is a build-time authoring tool, not an embedded runtime dependency.
The C++ application owns validation, geometry evaluation and loading.

An authored scene contains named nodes and a material table. Primitive leaves
carry their shape parameters, transforms and material assignment. Groups form
local constructive solid geometry (CSG) expressions, then join the larger
assembly. For example, a cut in the eye housing is resolved within that part
before the housing joins the head. A global list of cuts would make that
relationship dependent on unrelated assembly order.

Initially support rigid transforms and positive uniform scale. Generate
symmetry and repetition as explicit copies in the authoring helper. Arbitrary
nonuniform scaling, deformations and a general node editor can wait.

Keep the existing version-1 box recipes, generator hashes and presets readable
without rewriting them. The authored format gets an explicit new version and
separate validation; it must retain names, composition, dimensions, material
assignments and procedural parameters on export/reload. Record geometry and
appearance fingerprints separately so material edits do not invalidate the
distance cache. Rebuild from the explicit scene, without requiring its Python
generator to be present.

## Geometry and cached field construction

Keep rounded boxes and exact spheres. Capsules and capped cylinders supply the
small additional vocabulary for limbs, joints, antennae and sockets. Hard union
and subtraction remain available. Add smooth union when refining connected
shapes; define its radius in workspace units. Intersection, smooth subtraction,
tori, curve sweeps, displacement and arbitrary noise geometry wait for a
specific content need.

The current builder filters sequential fill/remove edits in batches. Its
per-brick edit references describe a construction batch; they are not a saved
index of every shape that can supply a final material. Groups and smooth
operators therefore need an explicit composite evaluator rather than new
operation tags in the existing hard-Boolean filter.

Compile the small authored expression into a bounded GPU program, mirrored by
a CPU evaluator. The first implementation may rebuild the authored field in
full when geometry changes. It can classify cells by evaluating the complete
root expression at the cell center and using a conservative distance bound,
then sample the retained bricks with the same expression. Establish the
1-Lipschitz bound for the selected primitives, transforms and smooth-union
formula; include sampling/rounding margins. Apply workspace clipping with
defined semantics and check shapes near the workspace boundary.

This reuses the existing hierarchy/brick representation, surface tracer,
render graph and post pass. Preserve the sequential batch builder for the
existing recipes. Authored assembly is not automatically a prefix of the old
edit list: the first presentation uses a static object and orbit. Construction
choreography can follow once the object succeeds.

Select implementation references and record their licenses before importing
primitive/operator code. The [2025 Lipschitz pruning work](https://wbrbr.org/publications/LipschitzPruning/)
is a design reference for composite expressions and conservative spatial
pruning. Porting that complete optimization is not a prerequisite for this
bounded scene.

## Materials and procedural appearance

Separate surface appearance from distance queries. Initially resolve material
properties by evaluating the authored scene at the primary surface hit, while
primary and shadow traversal continue to use the distance cache. Profile the
cost on this small scene before adding a material volume or a spatial index.
The existing large atlas already consumes most of its 2 GiB field budget;
adding full color channels to both atlases is not a default choice.

Define the material rules explicitly:

- Hard union selects the material associated with the winning branch, with a
  deterministic tie rule for coincident surfaces.
- Subtraction defaults to the retained body's material. An explicit cut
  material may color newly exposed surfaces without changing the outer body.
- Smooth union supports either a sharp material boundary or blending of
  material parameters using the operator's weights. Geometry smoothing and
  material blending are independent choices. Nested blends must evaluate the
  full expression rather than assuming only two materials can contribute.
- Named groups define the local coordinates used by a material. Coordinates
  follow the part when it moves; cuts evaluate the selected material at the
  newly exposed position.

The first material record needs linear base color, roughness and emission.
Use the shared PBR helpers and existing atmosphere lighting. The initial
palette uses dielectrics; metallic surfaces follow only with environment
specular lighting and a native appearance comparison. Glass, transmission,
subsurface scattering and GI are outside the first piece.

Procedural appearance is optional and follows the plain-material checkpoint.
Reuse [Cubey's procedural noise helpers](../../shaders/cubey/procedural/noise.glsl)
where their domain and filtering fit. Start with bounded color or roughness
variation, with explicit seed, scale and strength in object-local coordinates.
Keep a plain-material switch for comparison. Geometric displacement and normal
perturbation would introduce additional tracing or shading work and can wait.

Behavioral references include [MudBun's brush materials and dye operation](https://longbunnylabs.com/mudbun/)
and [Womp's material blending and cut-material option](https://womp.com/blogs/the-ultimate-guide-to-goop-liquid-3d-modeling-thats-changing-everything/).
These describe useful authoring semantics; they are not selected code donors.

## Native checks and review boundaries

Use focused fixtures for each new contract: capsule/cylinder distances,
group-local cuts, smooth blends beyond the unblended surfaces, CPU/GPU program
agreement and finite-grid hit bounds. Cover thin gaps, axis-parallel rays,
inside-solid exits and secondary-ray exhaustion. Continue explicit capacity
failure and completed-field publication within the current allocation budget.

Add material-ID or part-ID, base-color and roughness views before tuning the
robot's final lighting. Check body-versus-cut material, group isolation,
coincident ties and nested blends. Check that part-local procedural patterns
follow transforms and that procedural appearance leaves geometry unchanged.
Material edits must preserve the distance-cache fingerprint and build count.

Save/reload the authored scene and reproduce its fixed views. Existing recipe
tests and relevant native batch/traversal checks remain regression gates when
their shared code changes. Report sampled-image differences separately from
geometry agreement. Record geometry build wall time, steady tracing/shading
time, material evaluation cost and field allocation separately; an offline
turntable alone does not establish interactive editing performance.

If the gray robot or plain-material version is unconvincing, revise its
proportions, pose and palette before adding texture or rendering effects. The
assistant's authoring ability is evaluated through these native images. The
final claim is one successful authored piece only if the visual review supports
it.

## Later content routes

Specialized generation remains a separate candidate. It can supply a reference
design or an external asset for comparison, but importing generated geometry
and transferring its appearance need their own path. Additional analytic
primitives alone do not provide that import. No model installation, training,
mesh-to-SDF conversion or automatic primitive fitting is required for this
first scripted robot.
