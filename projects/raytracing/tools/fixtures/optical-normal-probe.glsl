// Private Q1 GPU control. The private builder substitutes the existing
// dielectric probe entry point; ordinary shader/configuration are untouched.
vec3 q1Tilt(float degrees) {
    float a = radians(degrees);
    return vec3(sin(a), 0, cos(a));
}
void q1NormalProbe() {
    uint x = gl_LaunchIDEXT.x, row = gl_LaunchIDEXT.y;
    float etaTable[8] = float[8](1, 1.1, 1.5, 2, 1, 1/1.1, 1/1.5, 1/2.0);
    float tilts[8] = float[8](-60, -30, -20, -10, 0, 10, 20, 30);
    float theta = mix(-89.95, 89.95, float(x) / float(gl_LaunchSizeEXT.x-1));
    float alpha = 0, eta = 1.5;
    uint mode = 0u;
    vec3 rawS = vec3(0, 0, 1), v;
    if (row < 64u) {
        eta = etaTable[row/8u];
        alpha = tilts[row%8u]; rawS = q1Tilt(alpha); v = q1Tilt(theta);
    } else if (row < 96u) {
        float seamTilts[4] = float[4](-2, -5, -10, -20);
        eta = etaTable[(row-64u)/4u]; alpha = seamTilts[(row-64u)%4u];
        theta = 90+2*alpha+mix(-.01, .01, float(x)/float(gl_LaunchSizeEXT.x-1));
        rawS = q1Tilt(alpha); v = q1Tilt(theta); mode = 1u;
    } else if (row < 112u) {
        eta = row < 104u ? 1.5 : 1/1.5;
        uint invalid = (row-96u)%8u;
        if (invalid == 0u) rawS = vec3(0);
        if (invalid == 1u) rawS = vec3(0, 0, -1);
        if (invalid == 2u) rawS = vec3(1, 0, 0);
        if (invalid == 3u) rawS = vec3(0, 0, 1e-12);
        if (invalid == 4u) rawS = vec3(uintBitsToFloat(0x7fc00000u), 0, 1);
        if (invalid == 5u) rawS = vec3(0, 0, uintBitsToFloat(0x7f800000u));
        if (invalid == 6u) rawS = vec3(.6, 0, .8);
        if (invalid == 7u) rawS = 3*q1Tilt(-20);
        v = q1Tilt(theta); mode = 2u;
    } else {
        float iors[4] = float[4](1.1, 1.5, 2, 2.5);
        float criticalTilts[4] = float[4](0, -2, 2, 20);
        eta = 1/iors[(row-112u)/4u]; alpha = criticalTilts[(row-112u)%4u];
        float critical = sqrt(1-eta*eta);
        float c = uintBitsToFloat(floatBitsToUint(critical)+x-gl_LaunchSizeEXT.x/2u);
        float s = sqrt(max(0, 1-c*c));
        vec3 n = q1Tilt(alpha), tangent = vec3(n.z, 0, -n.x);
        v = s*tangent+c*n; theta = degrees(acos(v.z)); rawS = n; mode = 3u;
    }
    vec3 G = vec3(0, 0, 1), incoming = -v;
    RtNormalInterface b = rtGuardedNormalInterface(incoming, G, rawS, eta, true);
    RtNormalInterface base = rtGuardedNormalInterface(incoming, G, rawS, eta, false);
    vec3 reverseR = vec3(0), reverseT = vec3(0), fixedR = vec3(0), fixedT = vec3(0);
    uint reasonR = 0u, reasonT = 0u;
    if (b.fresnel > 0) {
        RtNormalInterface r = rtGuardedNormalInterface(-b.reflected, G, rawS, eta, true);
        reverseR = r.reflected; reasonR = r.reason;
        fixedR = rtNormalAbout(-b.reflected, b.normal, eta).reflected;
    }
    if (b.fresnel < 1) {
        RtNormalInterface t = rtGuardedNormalInterface(-b.transmitted, -G, -rawS, 1/eta, true);
        reverseT = t.transmitted; reasonT = t.reason;
        fixedT = eta == 1 ? -b.transmitted : rtNormalAbout(-b.transmitted, -b.normal, 1/eta).transmitted;
    }
    vec4 record[16]; for (int i=0; i<16; ++i) record[i] = vec4(0);
    record[0] = vec4(incoming, eta); record[1] = vec4(G, RT_NORMAL_GUARD);
    record[2] = vec4(rawS, float(RT_NORMAL_POLICY_VERSION));
    record[3] = vec4(b.normal, float(b.reason));
    record[4] = vec4(b.reflected, b.fresnel);
    record[5] = vec4(b.transmitted, b.tir ? 1 : 0);
    record[6] = vec4(reverseR, float(reasonR)); record[7] = vec4(reverseT, float(reasonT));
    record[8] = vec4(base.reflected, base.fresnel); record[9] = vec4(base.transmitted, base.tir ? 1 : 0);
    record[10] = vec4(dot(b.reflected, G), dot(b.transmitted, G), dot(v,b.normal), 0);
    record[11] = vec4(theta, alpha, float(mode), 0);
    record[12] = vec4(fixedR, 0); record[13] = vec4(fixedT, 0);
    // Reduced-radiance furnace: the opposite medium has radiance eta^2.
    record[14] = vec4(b.fresnel+(1-b.fresnel)/(eta*eta)*(eta*eta), 0, 0, 0);
    uint pixel = x+row*gl_LaunchSizeEXT.x;
    for (uint i=0u; i<16u; ++i) queries[pixel*16u+i] = record[i];
    imageStore(outputImage, ivec2(gl_LaunchIDEXT.xy), vec4(.5+.5*b.reflected, 1));
}
