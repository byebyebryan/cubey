# Surface-gradient subset

The Fluid 2.5D terrain helper adapts only `TspaceNormalToDerivative`,
`SurfgradFromTriplanarProjection`, `SurfgradFromVolumeGradient`, and
`ResolveNormalFromSurfaceGradient` from [mmikk's standalone demo](https://github.com/mmikk/surfgrad-bump-standalone-demo),
commit `02e93b76f0d6f71dd6fc86cc0a522988d97b0398`,
`surfgradDemo/surfgrad_framework.h`.

Changes: stateless GLSL functions, generated RG normal reconstruction, a finite
zero-normal guard, no green-channel flip (our procedural RG is `-dH/d(u,v)`),
explicit strength, and Cubey's existing fourth-power projection weights.
There is no parallax mapping, texture-space tracing, or upstream renderer port.

MIT License

Copyright (c) 2020 mmikk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
