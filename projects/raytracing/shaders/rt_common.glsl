// RT dispatch follows Khronos Vulkan-Samples ray_tracing_basic.
// Copyright (c) 2019-2026 Sascha Willems. SPDX-License-Identifier: Apache-2.0
#ifdef CUBEY_RT_RAY_JOURNAL
#define RT_JOURNAL_FIELD uint journalHitKind;
#define RT_JOURNAL_ARG(value) ,value
#define RT_PATH_FIELDS uint journalBranch,journalParent;
#define RT_PATH_ARGS(branch,parent) ,branch,parent
#else
#define RT_JOURNAL_FIELD
#define RT_JOURNAL_ARG(value)
#define RT_PATH_FIELDS
#define RT_PATH_ARGS(branch,parent)
#endif
struct Hit { vec2 bary; float t; uint instance; uint primitive; RT_JOURNAL_FIELD };
struct Vertex { vec4 position; vec4 normal; };
struct Instance { mat4 world; mat4 normalWorld; uvec4 offsets; vec4 color; };
layout(set=0,binding=0) uniform accelerationStructureEXT scene;
layout(std140,set=0,binding=2) uniform Camera {
    mat4 inverseView; vec4 eye; vec4 sun; vec4 light; vec4 base; vec4 options;
} camera;
layout(std430,set=0,binding=3) readonly buffer Vertices { Vertex vertices[]; };
layout(std430,set=0,binding=4) readonly buffer Indices { uint indices[]; };
layout(std430,set=0,binding=5) readonly buffer Instances { Instance instances[]; };
