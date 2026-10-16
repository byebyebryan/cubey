# Planet — follow the terminator

Paused on 2026-10-15. This unexecuted shot proposal is retained as historical
study material. Publication roles and missing controls below describe the old
plan, not active showcase work. Reopening requires a new visual target.

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
  ([Planet study README](README.md)).
  The current headless phase rate is fixed in application code.
- **Bounded missing controls:** Expose only the shot's starting phase, phase
  rate, disk coverage, and optional bounded camera delta through a project
  recipe. Do not broaden the orbital executable into surface terrain or LOD.
