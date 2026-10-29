// Primary-only, overlap-certified fixed relaxation. The unchanged tracer handles
// all secondary rays. Every proposed endpoint consumes the shared loop budget.
bool guardedPrimary=true;
uvec4 guardedStats=uvec4(0); // proposed, accepted, rejected, accepted cached
float guardedRadius(float d,vec3 origin,float t) {
    // Existing authored/prepared arithmetic audit plus point reconstruction.
    float pointError=32.0*1.1920928955078125e-7*(1.0+length(origin)+abs(t)+abs(d));
    return max(abs(d)-uintBitsToFloat(authoredHeader.w)-pointError,0.0);
}
Hit trace(vec3 origin,vec3 direction,float limit) {
    if(!guardedPrimary) return referenceTrace(origin,direction,limit);
    Hit hit=Hit(0,0,0u,ivec3(0),0u);
    if(regionHeader.y!=0u) return hit;
    vec2 root=rayBox(origin,direction,modelLow.xyz,modelHigh.xyz);
    float t=max(root.x,0.0),end=min(root.y,limit);
    if(t>end) return hit;
    bool speculative=false;
    float anchorT=0.0,anchorD=0.0,anchorRadius=0.0;
    vec3 anchorPoint=vec3(0);
#ifdef SDF_ADAPTIVE_RELAX
    bool previousExact=false;
    float previousT=0.0,previousD=0.0,slope=-1.0;
#endif
    for(uint i=0u;i<4096u && t<=end;++i) {
        ++hit.steps;
#ifdef SDF_WORKLOAD
        workloadWalk[workloadPhase].x++;
#endif
        vec3 p=origin+direction*t;
        float cached=SDF_STEP_BOUND(p,direction);
        bool boundOnly=cached>EPSILON;
        float d=boundOnly ? 0.0 : SDF_SOURCE_DISTANCE(p);
        if(any(isnan(p)) || any(isinf(p)) || isnan(d) || isinf(d)) {
            hit.t=t;hit.status=2;return hit;
        }
        if(speculative) {
            float radius=boundOnly ? max(cached-32.0*1.1920928955078125e-7*(1.0+length(p)),0.0)
                                   : guardedRadius(d,origin,t);
            float margin=32.0*1.1920928955078125e-7*
                (1.0+length(p)+length(anchorPoint)+anchorRadius+radius);
            bool sameSign=boundOnly || (d<0.0)==(anchorD<0.0);
            // Coverage is tested BEFORE accepting an endpoint hit or a miss.
            bool accepted=sameSign && length(p-anchorPoint)+margin<anchorRadius+radius;
            speculative=false;
            if(!accepted) {
#ifdef SDF_ADAPTIVE_RELAX
                previousExact=false;slope=-1.0;
#endif
                guardedStats.z++;
                float next=anchorT+abs(anchorD);
                if(next==anchorT) {hit.t=anchorT;hit.status=2;return hit;}
                t=next;continue;
            }
            guardedStats.y++;
            if(boundOnly) guardedStats.w++;
        }
        if(boundOnly) {
#ifdef SDF_ADAPTIVE_RELAX
            previousExact=false;slope=-1.0;
#endif
#ifdef SDF_QUERY_WORKLOAD
            if(workloadPhase==0u) queryEndRun();
#endif
            float next=t+cached;
            if(next==t) {hit.t=t;hit.status=2;return hit;}
            t=next;continue;
        }
        if(abs(d)<=EPSILON) {hit.t=t;hit.status=1;return hit;}
        float radius=guardedRadius(d,origin,t);
        float omega=float(SDF_RELAX_OMEGA);
#ifdef SDF_ADAPTIVE_RELAX
        // Only consecutive accepted exact field samples train the predictor.
        // Absolute field values retain inside-to-exit semantics.
        if(previousExact && t>previousT) {
            float measured=(abs(d)-abs(previousD))/(t-previousT);
            slope=clamp(mix(slope,measured,.5),-1.0,.9);
        }
        omega=clamp(2.0/(1.0-slope),1.0,1.8);
        previousExact=true;previousT=t;previousD=d;
#endif
        float proposal=min(t+omega*radius,end);
        if(proposal>t+abs(d)) {
            anchorT=t;anchorD=d;anchorRadius=radius;anchorPoint=p;
            speculative=true;guardedStats.x++;t=proposal;
        } else {
            float next=t+abs(d);
            if(next==t) {hit.t=t;hit.status=2;return hit;}
            t=next;
        }
    }
    if(t<=end) {hit.t=t;hit.status=2;}
    return hit;
}
