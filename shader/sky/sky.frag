#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive    : enable

#include "sky_common.glsl"
#include "scene.glsl"
#include "clouds.glsl"

const uvec3 SKY_DITHER_TARGET_BITS = uvec3(11, 11, 10); // hardcoded for now -> adjust in future for HDR support or 6 bit monitors

layout(binding  = 0, std140) uniform UboScene {
  SceneDesc scene;
  };
layout(binding = 1) uniform sampler2D tLUT;
layout(binding = 2) uniform sampler2D mLUT;
layout(binding = 3) uniform sampler2D skyLUT;
layout(binding = 4) uniform sampler3D fogLut;
#if defined(SEPARABLE)
layout(binding = 5) uniform sampler3D fogLutMs;
#endif

layout(binding = 6) uniform sampler2D textureDayL0;
layout(binding = 7) uniform sampler2D textureDayL1;
layout(binding = 8) uniform sampler2D textureNightL0;
layout(binding = 9) uniform sampler2D textureNightL1;

layout(binding = 10) uniform sampler2D textureBolt;

layout(location = 0) out vec4 outColor;

vec3 inverse(vec3 pos) {
  vec4 ret = scene.viewProjectLwcInv*vec4(pos,1.0);
  return (ret.xyz/ret.w)/100.f;
  }

vec3 viewVec() {
  const vec2 uv  = gl_FragCoord.xy*scene.screenResInv;
  const vec3 pos = vec3(uv*2.0-1.0, 1.0);
  vec4 ret = scene.viewProjectInv*vec4(pos,1.0);
  return normalize(ret.xyz/ret.w - scene.camPos); //TODO: LWC
  }

vec3 atmosphere(vec3 view, vec3 sunDir) {
  const vec3  viewPos = vec3(0.0, RPlanet + scene.plPosY, 0.0);
  return textureSkyLUT(skyLUT, viewPos, view, sunDir);
  }

// debug only
vec3 transmittance(vec3 pos0, vec3 pos1) {
  const int steps = 32;

  vec3  transmittance = vec3(1.0);
  vec3  dir  = pos1-pos0;
  float dist = length(dir);
  for(int i=1; i<=steps; ++i) {
    float t      = (float(i)/steps);
    float dt     = dist/steps;
    vec3  newPos = pos0 + t*dir + vec3(0,RPlanet,0);

    const ScatteringValues sc = scatteringValues(newPos, 0);
    transmittance *= exp(-dt*sc.extinction);
    }
  return transmittance;
  }

vec3 transmittanceAprox(in vec3 pos0, in vec3 pos1) {
  vec3 dir = pos1-pos0;

  const ScatteringValues sc0 = scatteringValues(pos0 + vec3(0,RPlanet,0), 0);
  const ScatteringValues sc1 = scatteringValues(pos1 + vec3(0,RPlanet,0), 0);

  vec3  extinction = sc1.extinction;//-extinction0;
  return exp(-length(dir)*sc0.extinction);
  }

vec3 sky(vec2 uv, vec3 sunDir) {
  const vec2 inPos  = (2.0*gl_FragCoord.xy)*scene.screenResInv - 1.0;

  vec3  pos  = vec3(0,RPlanet,0);
  vec3  pos1 = inverse(vec3(inPos,1.0));
  vec3  pos0 = inverse(vec3(inPos,0));

  vec3 view  = normalize(pos1);
  vec3 lum   = atmosphere(view, sunDir);
  return lum;
  }

vec3 applyClouds(vec3 skyColor) {
  const vec2 inPos = (2.0*gl_FragCoord.xy)*scene.screenResInv - 1.0;

  float night    = scene.isNight;
  vec3  plPos    = vec3(0, RPlanet + scene.plPosY, 0);
  vec3  pos1     = inverse(vec3(inPos,1.0));
  vec3  viewDir  = normalize(pos1);
  return applyClouds(skyColor, skyLUT, plPos, scene.sunDir, viewDir, night, scene.overcast, scene.lightning,
                     scene.cloudsDir.xy, scene.cloudsDir.zw,
                     textureDayL1,textureDayL0, textureNightL1,textureNightL0);
  }

// Lightning bolt: the texture is placed on the sky around the strike azimuth. The seed mirrors,
// scales and leans it, so that bolts differ.
vec3 lightningBolt(vec3 view) {
  if(scene.lightning<=0.0)
    return vec3(0);

  const float height = 0.95;  // radians of sky covered by the picture
  const float width  = 0.55;
  const float centre = 0.47;  // elevation of the middle, the bolt reaches from clouds toward the horizon

  float azimuth = atan(view.z, view.x);
  float dAz     = mod(azimuth - scene.boltAzimuth + M_PI, 2.0*M_PI) - M_PI;
  float alt     = asin(clamp(view.y, -1.0, 1.0));

  float mirror = fract(scene.boltSeed*13.7)>0.5 ? -1.0 : 1.0;
  float scale  = 0.8 + 0.5*fract(scene.boltSeed*7.3);
  float lean   = (fract(scene.boltSeed*3.1) - 0.5)*0.5;

  vec2 q   = vec2(dAz*cos(alt)/(width*scale), (alt - centre)/(height*scale));
  q.x     += q.y*lean;
  q.x     *= mirror;
  vec2 uv  = vec2(q.x + 0.5, 0.5 - q.y);
  if(uv.x<0.0 || uv.x>1.0 || uv.y<0.0 || uv.y>1.0)
    return vec3(0);
  return texture(textureBolt, uv).rgb;
  }

void main() {
  const vec2 uv     = gl_FragCoord.xy*scene.screenResInv;
  const vec3 view   = viewVec();
  const vec3 sunDir = scene.sunDir;

  // accounted in additive fog later
  vec3  maxFog = textureLod(fogLut, vec3(uv, textureSize(fogLut,0).z-1), 0).rgb;
#if defined(SEPARABLE)
  maxFog += textureLod(fogLutMs, vec3(uv, textureSize(fogLut,0).z-1), 0).rgb;
#endif
  // Sky
  vec3  lum = sky(uv, sunDir) - maxFog;
  float tr  = 1.0;
  // Clouds
  lum = applyClouds(lum);
  lum = lum * scene.GSunIntensity;

  lum *= scene.exposure;
  lum += lightningBolt(view) * (4.0*scene.lightning); // after exposure: it must stay bright
  lum += dither(gl_FragCoord.xy, SKY_DITHER_TARGET_BITS);
  outColor = vec4(lum, tr);
  }
