# Ocean Spectral LOD Handoff

## Status

This is an opt-in proof layered on the surface-ocean v1 checkpoint. The known-good
fallback remains `d5ac7c23`, and `ocean.spectral_lod_handoff` defaults to `0`.
Use `--ocean-spectral-lod-handoff 1` to evaluate the complete handoff.

The proof deliberately does not change cascade periods, wind, spectral domains,
clipmap topology, displacement fades, or sea-state presets. It tests whether
resolved FFT detail can retire into stable material statistics before a larger
spectral-band redesign.

## Implementation

- Per active cascade, the renderer can build complete normal and foam moment
  pyramids from `map_size / 2` down to `1x1`.
- Normal moments store mean gradient and mean squared slope. Unresolved variance
  broadens BRDF roughness while the resolved normal path remains unchanged.
- Foam moments store mean and squared persistent/current foam. A sparse-occupancy
  estimate rejects uniform low-level history before contributing at most `2.5%`
  filtered material coverage.
- As resolved detail retires, both products converge toward each cascade's
  global moments. Sampling only a footprint mip exposed the finite `88 m` and
  `57 m` tiles as repeated far-field blobs and was rejected.
- Moment dispatches are skipped when the handoff is `0` unless `slope-lod`,
  `foam-lod`, or `foam-filtered` diagnostics require them.

Debug channels:

- `slope-lod`: red is unresolved slope RMS, green is roughness delta, blue is
  handoff weight.
- `foam-lod`: red is filtered persistent occupancy, green is current occupancy,
  blue is handoff weight.

## Review

Generate a warmed motion/still matrix with:

```bash
projects/ocean/capture_spectral_handoff_review.sh outputs/ocean-spectral-handoff-review
```

The harness uses video capture and extracts the final frame. PNG capture renders
one frame regardless of `--frames`, so it is not sufficient for accumulated
foam or motion stability review.

The accepted result is intentionally restrained:

- no periodic occupancy maps or new shimmer in the high-view motion pack;
- calm, windy, and stormy retain distinct reflected-sun widths and near foam;
- filtered foam remains a subpixel energy cue rather than a cloudy surface cap;
- the on/off change is useful but modest, so the known-good default remains off.

Rejected iterations included raw slope variance at full normal strength, which
erased the sun corridor, footprint-only foam moments, which revealed repeated
tiles, and `20%` global foam coverage, which washed the sea gray after history
had accumulated.

## Performance

Measured on the RTX 5070 Ti at `1280x720`, high camera, windy sea, fixed daylight,
clouds disabled, 300 video frames with 60 warmup frames. Median GPU pass totals
are the useful comparison because occasional clock spikes distort averages.

| Path | Median GPU total | Notes |
|---|---:|---|
| Pre-pyramid `7387938d` | about `2.48 ms` | Old three-level foam filter |
| Current handoff `0` | about `2.46 ms` | Moment work skipped |
| Current handoff `1` | about `2.56 ms` | Full normal + foam pyramids and fragment handoff |

At handoff `1`, the four active C0/C1 moment passes total about `0.080 ms`
median, and the surface pass adds about `0.023 ms`. The proof therefore costs
roughly `0.10 ms`, or `4%` in this isolated no-cloud profile. Default-off
rendering has no recurring moment-pass cost.

Use video mode for repeatable profiles:

```bash
./build/dev/projects/ocean/ocean --headless --capture video --frames 300 --fps 60 \
  --width 1280 --height 720 --ocean-camera-preset high --ocean-sea-state windy \
  --ocean-spectral-lod-handoff 1 --no-clouds --time-of-day-mode manual \
  --sun-elevation 42 --sun-azimuth -20 --pause-time --no-validation \
  --profile-output ocean-spectral-handoff --profile-warmup-frames 60 \
  --output outputs/ocean-spectral-handoff-profile.mp4
```

## Next Decision

Do not tune this proof into a replacement macro-wave model. The next mechanical
step, if the opt-in result remains useful in GUI review, is explicit spectral
band ownership with overlap windows and per-band render products. The current
moment interface can survive that redesign; the overlapping C0/C1 spectra and
finite periodic domains should not be treated as the final band model.
