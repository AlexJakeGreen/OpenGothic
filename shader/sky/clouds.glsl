#ifndef CLOUDS_GLSL
#define CLOUDS_GLSL

vec4 clouds(vec3 at, float nightPhase, float overcast, float lodBias, vec3 highlight,
            vec2 dxy0, vec2 dxy1,
            in sampler2D dayL1,   in sampler2D dayL0,
            in sampler2D nightL1, in sampler2D nightL0,
            in sampler2D rainL) {
  vec3  cloudsAt = normalize(at);
  vec2  texc     = 2000.0*vec2(atan(cloudsAt.z,cloudsAt.y), atan(cloudsAt.x,cloudsAt.y));

#if defined(SKY_LOD)
  vec4  cloudDL1 = textureLod(dayL1,   texc*0.3 + dxy1, SKY_LOD);
  vec4  cloudDL0 = textureLod(dayL0,   texc*0.3 + dxy0, SKY_LOD);
  vec4  cloudNL1 = textureLod(nightL1, texc*0.3 + dxy1, SKY_LOD);
  vec4  cloudNL0 = textureLod(nightL0, texc*0.6 + vec2(0.5), SKY_LOD); // stars
  vec4  cloudRL1 = textureLod(rainL,   texc*0.3 + dxy1, SKY_LOD);
  vec4  cloudRL0 = textureLod(rainL,   texc*0.3 + dxy0, SKY_LOD);
#else
  // lodBias blurs the repeating texture toward the horizon, where it would show moire
  vec4  cloudDL1 = texture(dayL1,   texc*0.3 + dxy1, lodBias);
  vec4  cloudDL0 = texture(dayL0,   texc*0.3 + dxy0, lodBias);
  vec4  cloudNL1 = texture(nightL1, texc*0.3 + dxy1);
  vec4  cloudNL0 = texture(nightL0, texc*0.6 + vec2(0.5)); // stars
  vec4  cloudRL1 = texture(rainL,   texc*0.3 + dxy1, lodBias);
  vec4  cloudRL0 = texture(rainL,   texc*0.3 + dxy0, lodBias);
#endif

  cloudDL0.a   = cloudDL0.a*0.2;
  cloudDL1.a   = cloudDL1.a*0.2;
  cloudNL0.a   = cloudNL0.a*1.0; // stars
  cloudNL1.a   = cloudNL1.a*0.1;
  cloudRL0.a   = cloudRL0.a*0.2;
  cloudRL1.a   = cloudRL1.a*0.2;

  // rain clouds fade in over the normal ones as the sky closes
  vec4 day       = mix((cloudDL0+cloudDL1)*0.5, (cloudRL0+cloudRL1)*0.5, overcast);
  vec4 night     = (cloudNL0+cloudNL1)*0.5;

  // Clouds (LDR textures from original game) - need to adjust
  day.rgb   = srgbDecode(day.rgb);
  night.rgb = srgbDecode(night.rgb);

  day.rgb   = day.rgb  *highlight*5.0;
  night.rgb = night.rgb*0.000014;

  // under a rain cover the clouds stay at night too and the stars (and night layers) are hidden
  day  .a   = day  .a*(1.0 - nightPhase*(1.0-overcast));
  night.a   = night.a*(1.0-overcast);

  vec4 color = mixClr(day,night);
  // color.rgb += hday;

  return color;
  }

vec3 applyClouds(vec3 skyColor, in sampler2D skyLUT, vec3 plPos, vec3 sunDir, vec3 view, float nightPhase, float overcast, float lightning,
                 vec2 dxy0, vec2 dxy1,
                 in sampler2D dayL1,   in sampler2D dayL0,
                 in sampler2D nightL1, in sampler2D nightL0,
                 in sampler2D rainL) {
  float L = rayIntersect(plPos, view, RClouds);
  // TODO: http://killzone.dl.playstation.net/killzone/horizonzerodawn/presentations/Siggraph15_Schneider_Real-Time_Volumetric_Cloudscapes_of_Horizon_Zero_Dawn.pdf
  // fake cloud scattering inspired by Henyey-Greenstein model
  vec3 lum  = vec3(0);
  lum += textureSkyLUT(skyLUT, plPos, vec3( view.x, view.y*0.0, view.z), sunDir);
  lum += textureSkyLUT(skyLUT, plPos, vec3(-view.x, view.y*0.0, view.z), sunDir);
  lum += textureSkyLUT(skyLUT, plPos, vec3(-view.x, view.y*0.0,-view.z), sunDir);
  lum += textureSkyLUT(skyLUT, plPos, vec3( view.x, view.y*0.0,-view.z), sunDir);
  //return lum;

  float lodBias = 6.0*(1.0 - smoothstep(0.02, 0.4, view.y)); // blur grows toward the horizon
  vec4 cloud = clouds((plPos + view*L)*0.01, nightPhase, overcast, lodBias, lum,
                      dxy0, dxy1,
                      dayL1,dayL0, nightL1,nightL0, rainL);
  // overcast: grey the clear sky, make the clouds denser and darker
  float luma = dot(skyColor, vec3(0.2126, 0.7152, 0.0722));
  skyColor   = mix(skyColor, vec3(luma)*0.05, overcast);
  cloud.a    = min(cloud.a*(1.0 + 4.0*overcast), 1.0);
  cloud.rgb *= 1.0 - 0.85*overcast;
  // lightning flash brightens the whole sky for a moment
  return (skyColor + cloud.rgb * cloud.a) * (1.0 + 20.0*lightning);
  // return mix(skyColor, cloud.rgb, cloud.a);
  }

#endif
