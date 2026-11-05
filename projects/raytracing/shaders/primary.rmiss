#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#include "rt_common.glsl"
layout(location=0) rayPayloadInEXT Hit hit;
void main() { hit.t=-1; }
