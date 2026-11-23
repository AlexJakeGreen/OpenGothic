#ifndef WET_GLSL
#define WET_GLSL

// Wet world during rain: darker surfaces, puddles and rain ripples. Units: centimetres, seconds.

// Integer hash: stays well distributed for large world coordinates, where a float based
// hash degenerates into visible rows.
float wetHash(vec2 p) {
  uvec2 q = uvec2(ivec2(floor(p)) + ivec2(100000));
  uint  h = q.x*1597334677u ^ q.y*3812015801u;
  h = (h ^ (h >> 16))*2246822519u;
  h = (h ^ (h >> 13))*3266489917u;
  h ^= h >> 16;
  return float(h & 0xFFFFFFu)/16777216.0;
  }

vec2 wetHash2(vec2 p) {
  return vec2(wetHash(p), wetHash(p + vec2(19.0, 7.0)));
  }

float wetNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f*f*(3.0 - 2.0*f);
  float a = wetHash(i);
  float b = wetHash(i + vec2(1,0));
  float c = wetHash(i + vec2(0,1));
  float d = wetHash(i + vec2(1,1));
  return mix(mix(a,b,f.x), mix(c,d,f.x), f.y);
  }

// Surfaces get darker and a bit more saturated: the albedo is raised to a power.
vec3 wetAlbedo(vec3 rgb, float wetness) {
  return pow(max(rgb, vec3(0.0)), vec3(1.0 + 0.3*wetness));
  }

// 0..1: how much of a puddle this point is. Only in a proper storm, only on upward facing surfaces;
// the pattern is noise in world space, so it does not move with the camera.
float puddleAmount(vec3 pos, vec3 normal, float wetness) {
  const float storm = smoothstep(0.55, 0.95, wetness);
  const float up    = smoothstep(0.95, 0.99, normal.y); // almost flat only: water on a slope would run off
  if(storm<=0.0 || up<=0.0)
    return 0.0;

  const vec2  p = pos.xz*0.0035;
  const float n = wetNoise(p)*0.6 + wetNoise(p*2.3 + vec2(11.7,3.1))*0.3 + wetNoise(p*5.1 + vec2(2.2,8.8))*0.1;
  // heavier rain makes larger puddles; the edge is soft
  const float edge = mix(0.62, 0.44, storm);
  return smoothstep(edge, edge + 0.08, n)*up*storm;
  }

// Expanding rings that appear at random places: a few layers of cells, one ring per cell and cycle.
// Returns the horizontal slope (x,z) that the rings add to a normal.
vec2 rainRipples(vec2 posCm, float timeSec) {
  vec2 slope = vec2(0.0);
  for(int layer=0; layer<3; ++layer) {
    const float cell   = 45.0 + 17.0*float(layer);
    const vec2  p      = posCm/cell + vec2(17.3*float(layer));  // layers are not aligned
    const vec2  id     = floor(p);
    const vec2  centre = id + 0.35 + 0.3*wetHash2(id + vec2(31.0*float(layer), 2.0));  // the ring stays inside its cell
    const float period = 0.9 + 0.8*wetHash(id + vec2(5.0 + float(layer), 3.0));
    const float cycleT = timeSec/period + wetHash(id + vec2(1.0, 9.0));
    const float phase  = fract(cycleT);
    // only some cells carry a ring in a given cycle, so there is no visible lattice
    if(wetHash(id + vec2(floor(cycleT), 40.0 + float(layer))) > 0.3)
      continue;

    const vec2  d      = (p - centre)*cell;
    const float dist   = max(length(d), 0.001);
    const float radius = phase*16.0;
    const float width  = 3.5;
    const float x      = (dist - radius)/width;
    const float height = exp(-x*x)*(1.0 - phase);            // ring profile fades while it grows
    slope += (d/dist) * (-2.0*x/width) * height * 1.5;        // slope of the profile
    }
  return slope;
  }

// rings are too small to resolve far away, where they would only alias into a pattern
float rippleDistanceFade(float distCm) {
  return 1.0 - smoothstep(1500.0, 5000.0, distCm);
  }

#endif
