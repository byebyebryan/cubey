#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#include "rt_common.glsl"
layout(location=0) rayPayloadInEXT Hit hit;
hitAttributeEXT vec2 bary;
void main() {
    hit=Hit(bary,gl_HitTEXT,gl_InstanceCustomIndexEXT,gl_PrimitiveID);
}
