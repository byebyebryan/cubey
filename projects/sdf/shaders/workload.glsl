// Private counters, stored once per camera ray. No atomics and no timing claims.
uint workloadPhase=0u;
float workloadBoundaryExcess=0.0;
float workloadIntervalOvershoot=0.0;
uvec4 workloadTotals[3]=uvec4[3](uvec4(0),uvec4(0),uvec4(0));
uvec4 workloadLow[3]=uvec4[3](uvec4(0),uvec4(0),uvec4(0));
uvec4 workloadHigh[3]=uvec4[3](uvec4(0),uvec4(0),uvec4(0));
uvec4 workloadWalk[3]=uvec4[3](uvec4(0),uvec4(0),uvec4(0));
#ifdef SDF_ACCELERATION
uvec4 workloadAid[3]=uvec4[3](uvec4(0),uvec4(0),uvec4(0));
#endif
void workloadProgram(uint count,bool fallback) {
    workloadTotals[workloadPhase].x++;
    workloadTotals[workloadPhase].y+=count;
    workloadTotals[workloadPhase].z=max(workloadTotals[workloadPhase].z,count);
    workloadTotals[workloadPhase].w+=fallback ? 1u : 0u;
    // Explicit components avoid nested dynamically indexed vector updates.
    if(count<=1u) workloadLow[workloadPhase].x++;
    else if(count<=4u) workloadLow[workloadPhase].y++;
    else if(count<=8u) workloadLow[workloadPhase].z++;
    else if(count<=16u) workloadLow[workloadPhase].w++;
    else if(count<=32u) workloadHigh[workloadPhase].x++;
    else if(count<=64u) workloadHigh[workloadPhase].y++;
    else if(count<=128u) workloadHigh[workloadPhase].z++;
    else workloadHigh[workloadPhase].w++;
}
