// Private diagnostic builds only. One selected invocation/sample owns the log.
#ifdef CUBEY_RT_RAY_JOURNAL
layout(std430,set=0,binding=19) buffer RayJournal {
    uvec4 control; // x, y, logical sample, mode bits: isolated=1, retain first failure=2
    uvec4 state;   // capacity, attempted count, overflow, first failure record + 1
    uvec4 packets[]; // eight uvec4 words (128 bytes) per packet
} rayJournal;
bool jActive=false;
uint jBranch=0u,jDepth=0u,jSequence=0u,jNextBranch=1u;
int jMedium=-1;
void jRecord(uint kind,vec4 a,vec4 b,vec4 c,vec4 d,vec4 e,vec4 f,vec4 g) {
    if(!jActive)return;
    uint index=rayJournal.state.y++;
    if(((kind>=100u&&kind<200u)||(kind==2u&&(uint(a.w)&~8u)!=0u))&&rayJournal.state.w==0u)rayJournal.state.w=index+1u;
    if(index>=rayJournal.state.x){rayJournal.state.z=1u;return;}
    uint first=index*8u;
    rayJournal.packets[first]=uvec4(kind,jBranch,jDepth,uint(jMedium));
    rayJournal.packets[first+1u]=floatBitsToUint(a);
    rayJournal.packets[first+2u]=floatBitsToUint(b);
    rayJournal.packets[first+3u]=floatBitsToUint(c);
    rayJournal.packets[first+4u]=floatBitsToUint(d);
    rayJournal.packets[first+5u]=floatBitsToUint(e);
    rayJournal.packets[first+6u]=floatBitsToUint(f);
    rayJournal.packets[first+7u]=floatBitsToUint(g);
}
#endif
