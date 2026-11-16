// Independent outgoing-hemisphere quadrature of the actual GLSL policy.
// 512x128 invocations, each integrates 8x8 cells: 4096 phi x 1024 cosine.
// One exterior unit-radiance beam: center 60 degrees, radius 15 degrees.
void q1NormalProbe() {
    vec3 G = vec3(0, 0, 1);
    float alpha = radians(20.), angle = radians(60.);
    vec3 rawS = vec3(sin(alpha), 0, cos(alpha));
    vec3 beam = vec3(sin(angle), 0, cos(angle));
    float threshold = cos(radians(15.));
    vec4 moments = vec4(0);
    for (uint j=0u; j<64u; ++j) {
        uint ix = gl_LaunchIDEXT.x*8u+j%8u;
        uint iy = gl_LaunchIDEXT.y*8u+j/8u;
        float z = (float(iy)+.5)/float(gl_LaunchSizeEXT.y*8u);
        float phi = 2*CUBEY_PBR_PI*(float(ix)+.5)/float(gl_LaunchSizeEXT.x*8u);
        float sine = sqrt(1-z*z);
        vec3 v = vec3(sine*cos(phi), sine*sin(phi), z);
        RtNormalInterface r = rtGuardedNormalInterface(-v, G, rawS, 1.5, true);
        RtNormalInterface t = rtGuardedNormalInterface(v, -G, -rawS, 1/1.5, true);
        RtNormalInterface gr = rtGuardedNormalInterface(-v, G, rawS, 1.5, false);
        RtNormalInterface gt = rtGuardedNormalInterface(v, -G, -rawS, 1/1.5, false);
        moments += z*vec4(r.fresnel*(dot(r.reflected,beam)>=threshold?1:0),
                           (1-t.fresnel)*2.25*(dot(t.transmitted,beam)>=threshold?1:0),
                           gr.fresnel*(dot(gr.reflected,beam)>=threshold?1:0),
                           (1-gt.fresnel)*2.25*(dot(gt.transmitted,beam)>=threshold?1:0));
    }
    vec4 record[16]; for (int i=0; i<16; ++i) record[i] = vec4(0);
    record[0] = moments/64;
    record[1] = vec4(alpha, angle, threshold, RT_NORMAL_GUARD);
    uint pixel = gl_LaunchIDEXT.x+gl_LaunchIDEXT.y*gl_LaunchSizeEXT.x;
    for (uint i=0u; i<16u; ++i) queries[pixel*16u+i] = record[i];
    imageStore(outputImage, ivec2(gl_LaunchIDEXT.xy), vec4(record[0].xyz, 1));
}
