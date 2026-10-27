# Pinned donor core

`common_culling.glsl` and `LICENCE` are copied verbatim from
<https://github.com/wbrbr/LipschitzPruning> at
`b3c5fee4d951ad8942775206d111cd421d2ef31c` (MIT).

The isolated qualification adapter supplies descriptor-backed arrays instead of
buffer device addresses, widens the 16-bit storage types to 32 bits, and maps
primitive evaluation to Cubey's compiled primitives. One 64-lane workgroup
replaces subgroup allocation/broadcast in the bounded first-level run, avoiding
any additional device feature requirement. The pruning control flow,
parent propagation, complement handling and far-field rule are the donor core.
No donor assets or allocator are imported. This is an adapted compute-core run,
not a run of the donor application. Candidate exact-distance pruning must omit
the donor's conservative far-field constants.
