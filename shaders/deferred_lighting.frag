#version 450
#extension GL_EXT_nonuniform_qualifier:enable
#extension GL_GOOGLE_include_directive:require
//#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
// Only needs the UBO/light structs, not the PBR helpers (it has its own oct decode).
#include "ubo.glsl"

layout(location=0)in vec2 fragUV;
layout(location=0)out vec4 outColor;

layout(set=0,binding=0)uniform sampler2D gDepth;
layout(set=0,binding=1)uniform sampler2D gNormal;
layout(set=0,binding=2)uniform sampler2D gAlbedo;
layout(set=0,binding=3)uniform sampler2D gEmission;
layout(set=0,binding=6)uniform sampler2D samplerColor[];

// GlobalUBO (binding 4) comes from ubo.glsl via pbr.glsl.

layout(push_constant)uniform Push{
    float time;
    uint debugMode;// 0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic 6=bloom 7=raw emission
    uint bloomHandle;// bindless handle for the bloom result
    float bloomIntensity;
    uint radiosityHandle;// bindless handle for the HDR radiosity buffer
    uint smaaEdgeHandle; // bindless handle for the SMAA edges target (debug mode 9)
}push;

vec3 reconstructWorldPos(vec2 uv,float depth){
    vec4 clip=vec4(uv*2.-1.,depth,1.);
    vec4 world=ubo.invViewProjMatrix*clip;
    return world.xyz/world.w;
}

vec2 signNotZero(vec2 v){
    return vec2(v.x>=0.?1.:-1.,v.y>=0.?1.:-1.);
}
vec3 octDecode(vec2 p){
    p=p*2.-1.;
    vec3 n=vec3(p,1.-abs(p.x)-abs(p.y));
    if(n.z<0.)n.xy=(1.-abs(n.yx))*signNotZero(n.xy);
    return normalize(n);
}

vec3 PBRNeutralToneMapping(vec3 color){
    const float startCompression=.8-.04;
    const float desaturation=.15;
    
    float x=min(color.r,min(color.g,color.b));
    float offset=x<.08?x-6.25*x*x:.04;
    color-=offset;
    
    float peak=max(color.r,max(color.g,color.b));
    if(peak<startCompression)return color;
    
    const float d=1.-startCompression;
    float newPeak=1.-d*d/(peak+d-startCompression);
    color*=newPeak/peak;
    
    float g=1.-1./(desaturation*(peak-newPeak)+1.);
    return mix(color,newPeak*vec3(1,1,1),g);
}

vec3 ReinhardToneMapping(vec3 color)
{
    return color/(color+vec3(1.));
}

void main(){
    // r_drawmode 9: SMAA edge render output (R = west edge, G = north edge). Full-screen, so
    // it runs before the background early-out below.
    if(push.debugMode==9u){
        vec2 e=(push.smaaEdgeHandle!=0u)?texture(samplerColor[push.smaaEdgeHandle],fragUV).rg:vec2(0.);
        outColor=vec4(e,0.,1.);
        return;
    }

    vec4 albedoData=texture(gAlbedo,fragUV);
    
    vec3 bloom=(push.bloomHandle!=0u)
    ?texture(samplerColor[push.bloomHandle],fragUV).rgb*push.bloomIntensity
    :vec3(0.);
    
    if(albedoData.w<.5){
        outColor=vec4(.01+bloom,1.);
        return;
    }
    
    vec4 normData=texture(gNormal,fragUV);
    vec3 N=octDecode(normData.rg);
    float roughness=normData.b;
    float metallic=normData.a;
    vec3 albedo=albedoData.xyz;
    
    if(push.debugMode==1u){outColor=vec4(albedo,1.);return;}
    if(push.debugMode==2u){outColor=vec4(N*.5+.5,1.);return;}
    if(push.debugMode==3u){
        float depth = texture(gDepth, fragUV).r;
        vec3 fragPos = reconstructWorldPos(fragUV, depth);
        float minWorldBound = -40.0;
        float maxWorldBound = 40.0;

        // 2. Map the float coordinates from [-100, 100] down to [0.0, 1.0]
        vec3 visualPos = (fragPos - minWorldBound) / (maxWorldBound - minWorldBound);
        visualPos = clamp(visualPos, 0.0, 1.0);
        outColor = vec4(visualPos, 1.0);
        return;
    }
    if(push.debugMode==4u){outColor=vec4(vec3(roughness),1.);return;}
    if(push.debugMode==5u){outColor=vec4(vec3(metallic),1.);return;}
    if(push.debugMode==6u){outColor=vec4(bloom,1.);return;}
    if(push.debugMode==7u){outColor=vec4(texture(gEmission,fragUV).rgb,1.);return;}
    if(push.debugMode==8u){
        // Raw radiosity buffer (HDR, pre-tonemap)
        vec3 raw=(push.radiosityHandle!=0u)
        ?texture(samplerColor[push.radiosityHandle],fragUV).rgb
        :vec3(0.);
        outColor=vec4(raw*.1,1.);// scale down so HDR values are visible
        return;
    }
    
    vec3 color=(push.radiosityHandle!=0u)
    ?texture(samplerColor[push.radiosityHandle],fragUV).rgb
    :albedo*.05;
    color+=bloom;
    //color = ReinhardToneMapping(color);
    color = PBRNeutralToneMapping(color);
    color = pow(color,vec3(1./2.2)); //gamma
    outColor=vec4(color,1.);
}
