# Showcase Storyboard and Capture Plan

Status: design authority. This document defines what the public visual
showcase should communicate and the capture work that follows. It does not
claim that the storyboard shots, media, or capture automation exist yet.

The showcase is a set of small project stories whose render inputs and capture
recipes can be reproduced. It is not a single merged reel and it is not a
request to turn Cubey into a cinematic editor. A shot is successful when a
reader can understand the project's distinctive visual idea from a short, calm
clip at README size.

## Scope

The first public-facing set contains these eleven active projects:

| Project | Public role | Story family |
| --- | --- | --- |
| `atmosphere` | Root-gallery candidate; hero eligibility deferred | Condition-led |
| `ocean` | Root-gallery candidate; hero eligibility deferred | Condition-led |
| `planet` | Root-gallery candidate; hero eligibility deferred | Condition-led |
| `terrain` | Root-gallery candidate; hero eligibility deferred | Camera-led |
| `gltf_viewer` | Root-gallery candidate; hero eligibility deferred | Camera-led |
| `smoke_2d` | Supporting project gallery; root inclusion after editorial review | Simulation-led |
| `water_2d` | Supporting project gallery; root inclusion after editorial review | Simulation-led |
| `water_3d` | Root-gallery candidate; hero eligibility deferred | Simulation-led |
| `fire_3d` | Supporting project gallery; root inclusion after editorial review | Simulation-led |
| `explosion_3d` | Root-gallery candidate; hero eligibility deferred | Simulation-led |
| `fractal_2d` | Supporting project gallery; root inclusion after editorial review | Camera-led |

“Root-gallery candidate” describes editorial eligibility, not a final ordering
or hero decision. The hero is selected only after the rendered clips are
reviewed together. Every project may retain a project-local clip even when it
does not enter the first root gallery.

`pbr_furnace` is explicitly excluded from this public set. It is an internal
white-furnace PBR validation target and remains useful for technical checks;
its runtime and documentation references must not be removed merely because
it is not a demo.

The following remain outside this showcase set without a runtime or repository
status change:

- `cloud_ref`, which is a retained cloud reference study;
- `examples/`, which are focused cube and foundation demonstrations rather
  than the project gallery;
- retained terrain studies under `studies/terrain/reference` and
  `studies/terrain/shadertoy`;
- `fluid_25d`, which is a design-only direction;
- archived or retired applications and studies.

The active/reference inventory remains documented in the [root
README](../README.md). Technical validation documentation may continue to
mention `pbr_furnace`.

## Suite visual language

The gallery should feel like one body of work without making every project
perform the same turntable.

- Consistency comes from restrained pacing, clean presentation, a common
  aspect ratio, and deliberate endings—not from identical orbit paths.
- Each clip has one dominant source of motion: environment/condition,
  camera, simulation, asset animation, or a deliberate shader traversal.
  Secondary motion should support that source rather than compete with it.
- The opening frame is already initialized and readable. Atlas generation,
  asset loading, simulation warm-up, and placeholder textures are outside the
  published clip.
- Camera motion is slow, bounded, and eased. No default full 360-degree orbit
  is accepted as a finished shot without an editorial reason.
- Condition-led clips keep the camera sufficiently stable that the viewer can
  read the changing light or environment. Simulation-led clips favor stable
  observational cameras. Camera-led clips use a purposeful partial reveal or
  orbit.
- No UI, debug view, profiler, cursor, reference marker, or diagnostic overlay
  appears in final media. Diagnostic captures can remain in ignored review
  packs.
- A clip makes one product claim. It does not enumerate every option or prove
  every subsystem in one pass.
- The result must be actual Cubey renderer output. Editorial trimming, a
  same-camera dissolve between fixed-step/reproducible renderer segments, and
  a poster frame are acceptable presentation steps; synthetic imagery and
  misleading compositing are not.
- The final test is reader-scale legibility: the subject, visual hierarchy,
  and payoff must survive a roughly 400–600 pixel README rendering.
- Final masters are provenance-aware. A capture should be reproducible from its
  source revision, effective configuration, command, and asset provenance; the
  selected committed artifact has a recorded SHA-256. This does not claim that
  regenerating an encoded MP4 produces byte-identical output.

The shared capture contract should cover timing, warm-up, frame cadence,
output, and bounded camera primitives only. Project-owned configuration remains
responsible for scene state: sea state, solar phase, terrain placement, fluid
scenario, pyro impulse, asset animation, and fractal destination. This follows
the ownership boundary in [Configuration V2](architecture/configuration.md)
and [Host and Engine](architecture/host-engine.md); a generic cinematic system
is not a showcase requirement.

## Shot card format

Each project has a shot card below. The card is the creative contract that
precedes implementation. It intentionally distinguishes the desired shot from
the current source-supported baseline.

Every card records:

- intended takeaway and cinematic verb/story;
- opening, development, payoff, and ending beats;
- camera behavior;
- condition and configuration intent;
- duration and transition direction;
- publication role without naming a hero;
- current feasibility based on source and project documentation; and
- bounded controls needed to realize the shot.

Durations are editorial targets, not new host defaults. “Continuous” means the
runtime should preferably produce the transition in one fixed-step/reproducible
session.
“Same-camera dissolve” is the fallback when a runtime transition would add an
unjustified subsystem or produce an unreadable intermediate state.

## Storyboards

### Atmosphere — let the sky turn (`atmosphere`)

- **Takeaway / cinematic verb:** Reveal the atmosphere's complete lighting
  range as night gives way to day.
- **Opening:** A dark, readable sky with stars and a restrained moon near the
  horizon; the camera is already at the chosen surface altitude and framing.
- **Development:** Solar time advances through first light. The horizon warms,
  stars recede, and clouds begin to catch directional light.
- **Payoff:** Dawn resolves into a clear blue sky with the sun and cloud layer
  composing together.
- **Ending:** Hold the settled daylight frame long enough to read the final
  cloud and aerial-perspective relationship.
- **Camera:** Fixed low-horizon composition with no orbit. A small authored yaw
  or pitch offset is acceptable at setup, but camera motion is not the story.
- **Condition/config intent:** Use the solar-clock path, a stable day/location,
  surface Cloud V1 settings, and a published exposure policy. Time of day is
  the moving condition; do not also vary cloud weather aggressively.
- **Duration/transitions:** 10–12 seconds, continuous time progression with
  eased exposure behavior. No cut is needed.
- **Publication role:** Root-gallery candidate; hero eligibility deferred.
- **Current feasibility:** The project has documented headless MP4 capture, a
  solar-clock time-of-day path, camera offsets, and shared cloud composition
  ([Atmosphere README](../projects/atmosphere/README.md)). Headless video time
  advances from the configured base time when solar playback is enabled.
- **Bounded missing controls:** A showcase recipe must own the exact starting
  time, duration, camera offsets, and cloud state. If capture cannot express
  that recipe from the existing typed facade, add only those project-owned
  bindings; do not add generic timeline editing.

### Ocean — one sea, changing light (`ocean`)

- **Takeaway / cinematic verb:** Compare the same ocean under a changing day
  and sea state, showing lighting, waves, foam, and reflection as one system.
- **Opening:** Calm swell before sunrise from a low/mid horizon viewpoint.
- **Development:** Dawn becomes daylight while the wave condition moves from
  Calm toward Windy; the horizon remains the visual anchor.
- **Payoff:** A Stormy state arrives under low warm light, making whitecaps,
  roughness, self-shadow, and cloud reflection legible.
- **Ending:** Resolve into a moonlit residual swell and hold the horizon rather
  than ending on a camera turn.
- **Camera:** Fixed low/mid horizon camera. At most, use an extremely subtle
  lateral drift or a 5–10 degree bounded arc across the whole clip if a static
  frame feels lifeless. Do not use a full orbit merely for suite consistency.
- **Condition/config intent:** Keep seeds, map quality, cascades, environment,
  and camera fixed. Change solar time and Calm/Windy/Stormy sea-state intent in
  a controlled sequence. Prefer a continuous runtime transition; if spectral
  preset changes cannot transition cleanly without a new wave-morphing system,
  render same-camera fixed-step/reproducible segments and use restrained editorial
  dissolves.
- **Duration/transitions:** 12–14 seconds: Calm dawn, Windy daylight, Stormy
  sunset, moonlit resolve. Continuous preferred; same-camera dissolve is the
  explicit fallback.
- **Publication role:** Root-gallery candidate; hero eligibility deferred.
- **Current feasibility:** Ocean already exposes Calm/Windy/Stormy presets,
  named camera presets, optional orbit spin, atmosphere/cloud lighting, and
  headless video timing ([Ocean README](../projects/ocean/README.md)). A zero
  orbit speed gives the desired stable baseline, but headless capture currently
  advances elapsed time rather than authoring a sequence of sea states.
- **Bounded missing controls:** Add a project-owned capture recipe for time and
  sea-state beats, plus a deterministic transition or segment boundary. Keep
  spectrum ownership in Ocean; do not move wave presets into the host timeline.

### Planet — follow the terminator (`planet`)

- **Takeaway / cinematic verb:** Show the deterministic orbital globe as the
  terminator moves across its surface, with atmosphere, night sky, and phase
  doing the explanatory work.
- **Opening:** A whole-disk globe in a readable lit/terminator composition,
  with enough surrounding sky to establish scale.
- **Development:** The phase sweeps from a broad lit face toward the
  terminator; land/ice/albedo structure remains readable without becoming a
  surface flyover.
- **Payoff:** A crescent or night-side view reveals the glow, cloud veil, and
  celestial composition.
- **Ending:** Settle on the selected crescent/terminator frame and hold it.
- **Camera:** Mostly locked whole-disk framing. A small bounded orbital arc is
  optional only if it clarifies the sphere; phase is the dominant motion.
- **Condition/config intent:** Use one deterministic terrain seed, standard
  surface quality, configured disk coverage, and the `lit` → `terminator` →
  `crescent` semantic range. Keep planet camera mode orbital; surface mode is
  deliberately out of scope.
- **Duration/transitions:** 9–11 seconds, continuous phase progression with a
  gentle settle. Do not cut between unrelated globe seeds.
- **Publication role:** Root-gallery candidate; hero eligibility deferred.
- **Current feasibility:** The orbital product has deterministic surface
  generation, `lit`/`terminator`/`crescent`/`night` views, disk coverage, an
  orbit controller, and headless video phase advancement
  ([Planet README](../projects/planet/README.md)).
  The current headless phase rate is fixed in application code.
- **Bounded missing controls:** Expose only the shot's starting phase, phase
  rate, disk coverage, and optional bounded camera delta through a project
  recipe. Do not broaden the orbital executable into surface terrain or LOD.

### Terrain — reveal the range (`terrain`)

- **Takeaway / cinematic verb:** Reveal the scale and material depth of the
  external-raster far-backdrop product.
- **Opening:** A foreground ridge or near slope occupies the lower frame with
  a readable mountain silhouette behind it.
- **Development:** The camera rises and moves laterally in a bounded arc,
  exposing the continuous backdrop sectors, shadowing, and atmospheric depth.
- **Payoff:** The full mountain composition and mineral-led filtered-detail
  surface become legible together.
- **Ending:** Settle on the wide range; leave enough stillness to inspect the
  far field and cloud/light relationship.
- **Camera:** Authored partial reveal arc around the selected terrain placement,
  with bounded yaw/elevation/radius and easing. A complete turntable is not the
  story.
- **Condition/config intent:** Use the canonical external heightfield, selected
  placement, filtered-detail material, qualified foreground height, directional
  shadows, and one stable low-sun/golden-hour condition. Avoid changing source
  placement mid-clip.
- **Duration/transitions:** 10–12 seconds, one continuous camera move with a
  short settle. No condition cut is needed.
- **Publication role:** Root-gallery candidate; hero eligibility deferred.
- **Current feasibility:** Terrain has deterministic external source placement,
  cached continuous geometry, shared atmosphere/cloud lighting, configurable
  initial azimuth/elevation/radius, and headless video capture. The current
  headless video path deliberately sets automatic rotation to a full orbit over
  the requested duration ([Terrain README](../projects/terrain/README.md)); the
  tracked [terrain evidence manifest](evidence/terrain-backdrop-foundation/manifest.json)
  establishes the existing provenance style.
- **Bounded missing controls:** Replace the capture-only full-orbit assumption
  with a project-owned bounded arc and explicit target/easing. Keep placement,
  surface detail, shadows, and source provenance in Terrain config.

### glTF Viewer — make the asset breathe (`gltf_viewer`)

- **Takeaway / cinematic verb:** Show a curated animated asset's silhouette,
  deformation, materials, and lighting as one coherent viewer experience.
- **Opening:** A clean three-quarter frame establishes the complete asset and
  its ground/environment relationship.
- **Development:** The chosen glTF animation begins while the camera makes a
  slow partial orbit, revealing a second silhouette and material response.
- **Payoff:** The most expressive pose aligns with the strongest three-quarter
  view and environment light.
- **Ending:** The animation and camera settle on that pose without looping at a
  distracting seam.
- **Camera:** Controlled partial orbit around the imported scene bounds. The
  camera should frame the asset, not expose viewer UI or a diagnostic pass.
- **Condition/config intent:** One redistributable animated glTF/GLB with
  recorded license and checksum; one animation index and speed; stable
  atmosphere/cloud or HDR environment. Terrain or ocean backdrop is optional
  but must not compete with the asset.
- **Duration/transitions:** 8–10 seconds, continuous animation plus eased
  camera arc. If the animation is shorter, select a clean loop segment rather
  than cutting between assets.
- **Publication role:** Root-gallery candidate; hero eligibility deferred.
- **Current feasibility:** The viewer has typed animation index/speed/paused
  options, scene-bound orbit camera setup, shared environment integration, and
  an optional eased `--capture-video-orbit-degrees` move with a
  scene-relative `--capture-camera-distance-scale`. Omitting the authored move
  preserves the historical automatic orbit; zero degrees gives a fixed shot.
  The asset remains an input requirement rather than a bundled showcase choice
  ([viewer config](../projects/gltf_viewer/gltf_viewer_config.h)).
- **Bounded missing controls:** Select and record one legal showcase asset and
  lock its animation segment, initial pose, arc extent, and ending hold. Keep
  those choices in the shot recipe; a generic animation timeline is not needed.

### Smoke 2D — structure from motion (`smoke_2d`)

- **Takeaway / cinematic verb:** Let colored dye and velocity form recognizable
  vortices, demonstrating the incompressible compute pipeline.
- **Opening:** A balanced field with all injectors initialized and enough empty
  space to read the motion.
- **Development:** Procedural injectors feed contrasting colors; tendrils bend,
  mix, and stretch under advection and vorticity.
- **Payoff:** A mature vortex composition shows structure rather than a noisy
  cloud of color.
- **Ending:** Hold the mature field briefly before fade makes the composition
  unreadable.
- **Camera:** Completely fixed fullscreen observation. The field itself is the
  motion.
- **Condition/config intent:** Use dye view, deterministic grid and injector
  count, stable pressure solver/iterations, and deliberate injector radius,
  force, propulsion, and decay. Do not mix in velocity/debug views.
- **Duration/transitions:** 6–8 seconds after warm-up, continuous simulation
  with no cuts.
- **Publication role:** Supporting project gallery; root inclusion after
  editorial review.
- **Current feasibility:** Smoke has deterministic headless PNG/MP4 capture,
  fixed simulation timing, configurable moving injectors and solver/render
  settings ([Smoke 2D README](../projects/fluid/smoke_2d/README.md)).
- **Bounded missing controls:** Add a capture recipe for warm-up, injector
  reset state, and stop frame if the existing startup options cannot reproduce
  the chosen composition. The injector construction is already deterministic;
  do not extract a reusable fluid or timeline system from this shot.

### Water 2D — make impact readable (`water_2d`)

- **Takeaway / cinematic verb:** Show a free-surface dam break colliding with
  an obstacle and producing splash, foam, and recoil.
- **Opening:** A raised water slab and obstacle are clearly separated in a
  side-on tank view.
- **Development:** The slab releases and accelerates toward the circular
  obstacle; the surface remains readable as a coherent volume.
- **Payoff:** Collision produces a high, asymmetric splash and visible foam,
  then the flow folds around the obstacle.
- **Ending:** Hold the rebound/settling shape rather than cutting during a
  sparse-particle artifact.
- **Camera:** Fixed side view with stable tank framing. The 2D simulation is
  the camera subject; no orbit concept applies.
- **Condition/config intent:** Prefer the `ObstacleSplash` scenario, APIC
  transfer, shaded surface view, and a deterministic fixed timestep. Keep
  particle/grid quality high enough for a clean silhouette.
- **Duration/transitions:** 7–9 seconds from initialized slab, continuous
  simulation. No editorial condition transition.
- **Publication role:** Supporting project gallery; root inclusion after
  editorial review.
- **Current feasibility:** Water 2D has APIC/PIC-FLIP, named reset scenarios
  including obstacle splash, fixed headless timing, and headless output. The
  project config currently binds transfer and feature toggles, while scenario
  selection is applied in the runtime/UI path
  ([Water 2D README](../projects/fluid/water_2d/README.md)).
- **Bounded missing controls:** Bind the selected scenario (or a capture-owned
  equivalent) through the project facade so headless capture can select
  `ObstacleSplash` without UI interaction. Keep obstacle and particle policy
  in Water 2D; no host camera work is needed.

### Water 3D — collapse and rebound (`water_3d`)

- **Takeaway / cinematic verb:** Show a 3D liquid volume leaving its dam,
  crossing the long tank, colliding, and resolving into whitewater.
- **Opening:** The left-biased dam-break volume is poised in the long tank;
  the water surface and surrounding environment are already initialized.
- **Development:** The bulk flow crosses the tank and develops a readable
  crest, with the stable three-quarter view preserving depth.
- **Payoff:** Impact, screen-space surface reconstruction, foam, and secondary
  whitewater all read in one frame.
- **Ending:** Let the rebound and reflection settle without switching to a
  diagnostic view.
- **Camera:** Stable three-quarter observational camera. A very small bounded
  dolly is optional; no automatic orbit is required for the story.
- **Condition/config intent:** Default long showcase tank, APIC, wave driver
  only when it supports the dam-break read, whitewater enabled, deterministic
  fixed timestep, and a stable procedural atmosphere or explicitly recorded
  HDR environment. Optional rain/hose/drain are off unless a later card is
  authored around them.
- **Duration/transitions:** 8–10 seconds from reset, continuous simulation with
  a short ending hold.
- **Publication role:** Root-gallery candidate; hero eligibility deferred.
- **Current feasibility:** Water 3D has the long showcase tank, deterministic
  headless fixed-step driver, screen-space surface/foam/whitewater renderer,
  optional wave/hose/drain/rain controls, and project-owned capture framing
  ([Water 3D README](../projects/fluid/water_3d/README.md)). An optional
  `--capture-video-orbit-degrees` authors an eased bounded arc, with zero
  degrees selecting the stable three-quarter shot; omission preserves the
  historical automatic video orbit. `--capture-camera-distance` controls the
  absolute tank framing without changing windowed defaults.
- **Bounded missing controls:** Lock the selected fixed or restrained-orbit
  recipe and ending hold through visual review. No shared camera mode or host
  timeline work is required.

### Fire 3D — flow around form (`fire_3d`)

- **Takeaway / cinematic verb:** Show a sustained pyro plume dividing around a
  solid obstacle, with volumetric fire, smoke, and shadow structure visible.
- **Opening:** A quiet volume and obstacle establish scale before ignition is
  fully developed.
- **Development:** The plume rises and wraps around the obstacle; the flame
  core and darker smoke separate visually.
- **Payoff:** A mature plume shows turbulence, rim/scatter, and volume shadows
  against the environment.
- **Ending:** Hold the mature flow and stop before density decay makes it
  visually thin.
- **Camera:** Very slow 10–15 degree bounded arc around the volume, or a locked
  three-quarter view if the arc competes with the flame. The plume is the
  dominant motion.
- **Condition/config intent:** Fire mode, one source, obstacle enabled, stable
  source/fire model and atmosphere lighting. Use the final smoke view, not
  density or velocity diagnostics.
- **Duration/transitions:** 7–9 seconds after a short warm-up, continuous
  simulation with a gentle camera move and ending hold.
- **Publication role:** Supporting project gallery; root inclusion after
  editorial review.
- **Current feasibility:** Fire uses the shared dense 3D pyro solver, exposes
  source/fire/obstacle/render settings, and has a deterministic headless driver
  with project-owned camera distance and eased bounded-orbit controls
  ([Fire 3D README](../projects/fluid/fire_3d/README.md)). Zero capture-orbit
  degrees gives the accepted fixed view; omission preserves the historical
  automatic video orbit for compatibility.
- **Bounded missing controls:** Record and lock warm-up/source timing,
  environment state, and the ending hold. Keep pyro source and rendering policy
  project-owned.

### Explosion 3D — impulse to aftermath (`explosion_3d`)

- **Takeaway / cinematic verb:** Make one hot impulse legible from flash through
  expanding shell to buoyant smoke aftermath.
- **Opening:** A quiet, centered volume creates anticipation without exposing
  initialization.
- **Development:** One configured impulse ignites, expands, and throws a bright
  shell through the volume.
- **Payoff:** The fireball transitions into a readable smoke plume with light
  and shadow variation.
- **Ending:** Hold the aftermath as it rises; avoid a second impulse stealing
  the narrative.
- **Camera:** Locked or nearly locked three-quarter view. A restrained 5–10
  degree arc is optional only if it improves depth; the impulse remains the
  dominant motion.
- **Condition/config intent:** Explosion mode with one intentional interval
  inside the clip, explicit duration/boost/source parameters, obstacle disabled
  unless a separate interaction story is approved, and stable atmosphere/HDR
  lighting.
- **Duration/transitions:** 6–8 seconds, continuous one-impulse lifecycle
  followed by a short aftermath hold.
- **Publication role:** Root-gallery candidate; hero eligibility deferred.
- **Current feasibility:** Explosion shares the pyro solver, headless driver,
  capture camera controls, and interval/duration/boost/source/obstacle/
  environment options with Fire
  ([Explosion 3D README](../projects/fluid/explosion_3d/README.md)). It can use a
  fixed or eased bounded capture orbit without changing its normal automatic
  video-orbit default.
- **Bounded missing controls:** Author one-shot interval/start timing and the
  ending hold. Do not add a generic event sequencer; the existing explosion
  parameters are sufficient for the first story.

### Fractal 2D — descend into structure (`fractal_2d`)

- **Takeaway / cinematic verb:** Start with a recognizable Mandelbrot-style
  whole and descend into one boundary's hidden detail.
- **Opening:** The complete set is framed with the familiar cardioid and major
  lobes visible.
- **Development:** A smooth directed zoom and slight pan approach a selected
  boundary feature.
- **Payoff:** Fine filament/detail fills the frame while retaining a clear
  relationship to the starting shape.
- **Ending:** Settle on the detail for inspection; do not continue zooming into
  numerical noise.
- **Camera:** A deterministic 2D camera traversal in center/scale space. This
  is not an orbit and should not inherit a 3D camera abstraction.
- **Condition/config intent:** Fixed shader parameters and a documented target
  center/scale path. The chosen endpoint must remain visually stable at the
  final resolution.
- **Duration/transitions:** 6–8 seconds, logarithmic/eased zoom with no cuts.
- **Publication role:** Supporting project gallery; root inclusion after
  editorial review.
- **Current feasibility:** The project has headless output and a reusable
  `Camera2D`/pan-zoom controller, but its startup facade currently owns only
  common host options; the default center and scale are constructed in the app
  ([Fractal view](../projects/fractal_2d/fractal_2d_view.h)).
- **Bounded missing controls:** Add project-owned start/end center and scale
  plus an eased capture traversal. Keep the shader and 2D camera local; no
  generic cinematic path is implied.

## Media and provenance contract

The publication model is intentionally hybrid:

1. Auditions and diagnostic packs are written below an ignored path such as
   `outputs/showcase/`. They may include contact sheets, storyboards, profile
   data, and low-resolution draft videos, but are not repository artifacts.
2. At a fixed source revision, the selected masters are encoded as H.264 MP4
   files. The matching poster PNG, effective project configuration, capture
   command, source revision, asset/license record, and the selected artifact's
   SHA-256 belong in the repository. A later regeneration should be visually
   and capture-recipe reproducible, but is not assumed to reproduce those MP4
   bytes exactly.
3. The exact committed MP4 bytes are uploaded through GitHub's Markdown
   attachment flow so the README can render a native player. The resulting
   attachment URL is recorded next to the committed relative reference; the
   uploaded file must be byte-identical to that reference.
4. README and project pages link the player and the committed reference. A
   future reader can inspect or regenerate the source even though GitHub's
   inline player uses an attachment URL.

There is no merged “hero reel.” The root README may show several independent
players, and project READMEs may show their own supporting clips. YouTube or
another external host is optional future long-form documentation, not a
prerequisite for the first gallery.

The provenance shape should follow the compact, explicit precedent in the
[terrain backdrop evidence manifest](evidence/terrain-backdrop-foundation/manifest.json):
record identity and hashes rather than relying on filenames or an untracked
capture session.

## Gated action plan

The implementation should move through these gates. Each gate produces a
reviewable artifact before the next cohort begins.

### 1. Creative lock

Turn the cards above into a small machine-readable inventory/shot-card record.
Resolve any remaining questions about beat timing, camera intent, condition
sequence, and publication role. Do not implement a timeline until every
project has an approved story and the suite has been checked for repetitive
camera language.

Exit gate: eleven approved cards; exclusions remain explicit; no hero is named.

### 2. Minimal shared capture contract

Specify only shared host concerns:

- warm-up before frame zero;
- deterministic start frame, duration, FPS, and fixed-step timing;
- output resolution/codec/path;
- bounded camera primitives where a real repeated contract exists; and
- manifest/poster/storyboard metadata.

Project-owned hooks express condition and scene timelines. Shared code must not
own Ocean sea states, Planet phase semantics, Water scenarios, Pyro impulses,
asset animation choices, or Fractal destinations.

Exit gate: the contract can express a fixed-camera condition clip, a bounded
camera reveal, and a stable simulation observation without introducing a
generic cinematic editor.

### 3. Ocean vertical slice

Use Ocean to validate the whole design because it separates a stable camera
from changing lighting and sea conditions. Produce ignored auditions and a
provenance manifest for the fixed-camera Calm → Windy → Stormy concept. Prefer
continuous runtime transitions; prove the same-camera dissolve fallback only
if spectral preset transitions are not justified.

Exit gate: one fixed-step/reproducible Ocean story can be captured without UI
and the result can be reviewed as a storyboard, poster, and short video.

### 4. Implement cohorts

Move through cohorts that share creative shape, not merely source directory:

- **Condition-led:** Atmosphere, Ocean, Planet.
- **Camera-led:** Terrain, glTF Viewer, Fractal 2D.
- **Simulation-led:** Smoke 2D, Water 2D, Water 3D, Fire 3D, Explosion 3D.

Each project gets a project-owned shot recipe and a draft capture. Missing
controls stay bounded to the card; a new shared abstraction requires a
separate architecture review.

Exit gate: every card is reproducible in draft form, with no capture-only
hardcoded behavior contradicting the approved story.

### 5. Suite editorial gate

Review the drafts together at README size. Check opening legibility, payoff,
dead time, repeated arcs, color/pacing balance, condition readability, and
whether motion adds information. Select the hero and root-gallery order only
here, after rendered evidence exists.

Exit gate: one approved final cut per project, an approved root-gallery set,
and a hero chosen from the actual rendered candidates.

### 6. Final masters

Render the selected clips at one source revision with the agreed resolution,
FPS, H.264 settings, posters, exact effective configs, commands, asset
provenance, and hashes. Keep auditions and diagnostics ignored. Commit the
masters and provenance in reviewable groups.

Exit gate: each selected file passes codec/dimension/duration/hash checks and
can be regenerated from the recorded recipe.

### 7. GitHub attachment publication and verification

Upload each exact committed master through GitHub Markdown, record the native
player URL, update the root/project README references, and verify the rendered
players plus committed-file links. Confirm attachment bytes against the
recorded SHA-256 and leave technical/internal projects out of the public
gallery.

Exit gate: README players render, relative references resolve, the provenance
manifest is complete, and local Git state contains only reviewed publication
changes.

## Design boundary

This document is the creative and capture-planning checkpoint. It does not
authorize implementation of camera paths, timeline APIs, project config
fields, capture scripts, media files, README players, uploads, commits, or
pushes. Those are follow-on slices after the cards and gates are accepted.
