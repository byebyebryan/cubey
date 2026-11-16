# Glass dragon smooth-default validation

2026-11-16 integration of the appearance selected by the user. Raw captures and
native screenshots live under
`outputs/raytracing/dragon-glass-smooth-default-20261116/`.

The [validation receipt](validation.json) records the RT regression suite,
saved normal-policy roundtrip, asset-bound glass controls, paired captures and
their status flags. The ordinary app and config tests were freshly rebuilt.
All 20 RT CTests and nine documentation CTests passed. The glass qualifier's
16 captures, IOR-1/bending/tint controls, independent support material, saved
view/recipe roundtrip and seven rejection controls passed.

The new smooth 960x540/128-SPP and 1920x1080/4096-SPP PNGs are byte-identical to
the corresponding reviewed private smooth preview. The geometric pilot's mean
absolute difference is .000271 RGB8 codes, p99 zero, maximum three. The latter
is not a claim of universal geometric byte identity.

The full-HD smooth seed-19 capture retains one cumulative event-budget pixel
and 207 zero-weight cutoff flags. There are no medium/query/shadow/stack flags;
the zero-weight flags are permitted by the existing acceptance policy. The
event-budget pixel is reported as a diagnostic limit, not silently accepted by
the strict transport gate. The production budgets and cutoff are unchanged.

The owned-Xvfb native run visibly switched smooth -> geometric -> smooth.
Its metrics recorded history epochs 1/2/3 and a final smooth setting, with clean
Vulkan synchronization validation. This is a native window/control check, not
a physical-display or temporal-quality claim.

The separate SDF worktree is outside this change. Q1's analytical limitations
remain in the linked integration note; no physical-accuracy promotion is claimed.
