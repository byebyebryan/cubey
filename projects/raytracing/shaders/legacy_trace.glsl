void traceLegacy() {
    vec2 uv=(vec2(gl_LaunchIDEXT.xy)+.5)/vec2(gl_LaunchSizeEXT.xy);
    vec2 screen=uv*2-1;
    vec3 origin=camera.eye.xyz;
    vec3 direction=normalize(mat3(camera.inverseView)*vec3(screen.x*camera.eye.w*camera.light.z,-screen.y*camera.light.z,-1));
    hit.t=-1;
    traceRayEXT(scene,gl_RayFlagsOpaqueEXT,255,0,0,0,origin,.00001,direction,100,0);
    vec3 color=vec3(.035,.045,.06);
    Query query=Query(vec4(-1,0,0,0),vec4(0),vec4(0),vec4(0));
    if(hit.t>=0) {
        Instance instance=instances[hit.instance];
        uint start=instance.offsets.y+3*hit.primitive;
        Vertex a=vertices[indices[start]+instance.offsets.x];
        Vertex b=vertices[indices[start+1]+instance.offsets.x];
        Vertex c=vertices[indices[start+2]+instance.offsets.x];
        vec3 bary=vec3(1-hit.bary.x-hit.bary.y,hit.bary);
        vec3 wa=(instance.world*a.position).xyz,wb=(instance.world*b.position).xyz,wc=(instance.world*c.position).xyz;
        vec3 geometric=normalize(cross(wb-wa,wc-wa));
        geometric=faceforward(geometric,direction,geometric);
        vec3 n=normalize(mat3(instance.normalWorld)*(a.normal.xyz*bary.x+b.normal.xyz*bary.y+c.normal.xyz*bary.z));
        if(dot(n,geometric)<0) n=-n;
        vec3 p=origin+direction*hit.t,l=normalize(camera.sun.xyz),v=-direction;
        float nl=dot(geometric,l)>0 ? max(dot(n,l),0):0,nv=max(dot(n,v),.001);
        bool floor=instance.offsets.z==1u;
        vec3 base=floor ? materials[1].base.rgb : camera.base.rgb*instance.color.rgb;
        float rough=floor ? .75 : camera.base.w;
        occluded=false;
        if(camera.options.x>.5 && nl>0 && dot(geometric,l)>0) {
            occluded=true;
            float offset=max(.00002,2e-6*max(max(abs(p.x),abs(p.y)),abs(p.z)));
            traceRayEXT(scene,gl_RayFlagsOpaqueEXT|gl_RayFlagsTerminateOnFirstHitEXT|gl_RayFlagsSkipClosestHitShaderEXT,
                255,0,0,1,p+geometric*offset,0,l,100,1);
        }
        vec3 h=normalize(l+v),f=cubey_pbr_fresnel_schlick(max(dot(h,v),0),vec3(.04));
        float d=cubey_pbr_distribution_ggx(max(dot(n,h),0),rough);
        float vis=cubey_pbr_visibility_smith_ggx_correlated(nv,nl,rough);
        color=base*camera.light.y+((1-f)*base/3.141592654+f*d*vis)*camera.light.x*nl*(occluded ? 0 : 1);
        int view=int(camera.options.y+.5);
        if(view==1) color=geometric*.5+.5;
        if(view==2) color=n*.5+.5;
        if(view==3) color=vec3(hit.t/6);
        if(view==4) color=vec3(uvec3((hit.instance+1)*1664525u)*uvec3(1,17,113)&255u)/255;
        if(view==5) color=bary;
        query=Query(vec4(hit.t,float(hit.instance),float(hit.primitive),occluded?0:1),vec4(geometric,0),vec4(n,0),vec4(bary,0));
    }
    imageStore(outputImage,ivec2(gl_LaunchIDEXT.xy),vec4(color,1));
    if(camera.options.z>.5) {
      uint start=4u*(gl_LaunchIDEXT.x+gl_LaunchIDEXT.y*gl_LaunchSizeEXT.x);
      queries[start]=query.hit;queries[start+1]=query.geometric;queries[start+2]=query.shading;queries[start+3]=query.bary;
    }
}
