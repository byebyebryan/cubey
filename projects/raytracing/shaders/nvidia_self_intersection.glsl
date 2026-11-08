//
// SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

// Compute the object and world space position and normal corresponding to a triangle hit point.
// Compute a safe spawn point offset along the normal in world space to prevent self intersection of secondary rays.
void safeSpawnOffset(
    out vec3        outObjPosition, // position in object space
    out vec3        outWldPosition, // position in world space
    out vec3        outObjNormal,   // unit length surface normal in object space
    out vec3        outWldNormal,   // unit length surface normal in world space
    out float       outWldOffset,   // safe offset for spawn position in world space
    in const vec3   v0,             // spawn triangle vertex 0 in object space
    in const vec3   v1,             // spawn triangle vertex 1 in object space
    in const vec3   v2,             // spawn triangle vertex 2 in object space
    in const vec2   bary,           // spawn barycentrics
    in const mat4x3 o2w,            // spawn instance object-to-world transformation
    in const mat4x3 w2o )           // spawn instance world-to-object transformation
{
    precise vec3 edge1 = v1 - v0;
    precise vec3 edge2 = v2 - v0;

    // interpolate triangle using barycentrics.
    // add in base vertex last to reduce object space error.
    precise vec3 objPosition = v0 + fma( vec3( bary.x ), edge1, ( vec3( bary.y ) * edge2 ) );
    vec3 objNormal = cross( edge1, edge2 );

    // transform object space position.
    // add in translation last to reduce world space error.
    precise vec3 wldPosition;
    wldPosition.x = o2w[3][0] +
        fma( o2w[0][0], objPosition.x,
            fma( o2w[1][0], objPosition.y,
                ( o2w[2][0] * objPosition.z ) ) );
    wldPosition.y = o2w[3][1] +
        fma( o2w[0][1], objPosition.x,
            fma( o2w[1][1], objPosition.y,
                ( o2w[2][1] * objPosition.z ) ) );
    wldPosition.z = o2w[3][2] +
        fma( o2w[0][2], objPosition.x,
            fma( o2w[1][2], objPosition.y,
                ( o2w[2][2] * objPosition.z ) ) );

    // transform normal to world - space using
    // inverse transpose matrix
    vec3 wldNormal = transpose( mat3( w2o ) ) * objNormal;

    // normalize world space normal
    const float wldScale = inversesqrt( dot( wldNormal, wldNormal ) );
    wldNormal = wldScale * wldNormal;

    const float c0 = 5.9604644775390625E-8f;
    const float c1 = 1.788139769587360206060111522674560546875E-7f;

    const vec3  extent3 = abs( edge1 ) + abs( edge2 ) + abs( edge1 - edge2 );
    const float extent = max( max( extent3.x, extent3.y ), extent3.z );

    // bound object space error due to reconstruction and intersection
    vec3 objErr = fma( vec3( c0 ), abs( v0 ), vec3( c1 * extent ) );

    // bound world space error due to object to world transform
    const float c2 = 1.19209317972490680404007434844970703125E-7f;
    mat4x3 abs_o2w = mat4x3( abs( o2w[0] ), abs( o2w[1] ), abs( o2w[2] ), abs( o2w[3] ) );
    vec3 wldErr = fma( vec3( c1 ), mat3( abs_o2w ) * abs( objPosition ), ( c2 * abs( o2w[3] ) ) );

    // bound object space error due to world to object transform
    mat4x3 abs_w2o = mat4x3( abs( w2o[0] ), abs( w2o[1] ), abs( w2o[2] ), abs( w2o[3] ) );
    objErr = fma( vec3( c2 ), ( abs_w2o * vec4( abs( wldPosition ), 1 ) ), objErr );

    // compute world space self intersection avoidance offset
    float wldOffset = dot( wldErr, abs( wldNormal ) );
    float objOffset = dot( objErr, abs( objNormal ) );

    wldOffset = fma( wldScale, objOffset, wldOffset );

    // output safe front and back spawn points
    outObjPosition = objPosition;
    outWldPosition = wldPosition;
    outObjNormal = normalize( objNormal );
    outWldNormal = wldNormal;
    outWldOffset = wldOffset;
}

// Offset the world-space position along the world-space normal by the safe offset to obtain the safe spawn point.
vec3 safeSpawnPoint(
    in const vec3   position,
    in const vec3   normal,
    in const float  offset )
{
    precise vec3 p = fma( vec3( offset ), normal, position );
    return p;
}
