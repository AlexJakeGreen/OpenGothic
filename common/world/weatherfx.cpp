#include "weatherfx.h"

#include <algorithm>
#include <cmath>

#include "world/objects/npc.h"
#include "world/world.h"
#include "game/gamesession.h"
#include "resources.h"
#include "camera.h"
#include "gothic.h"

using namespace Tempest;

// All particle numbers below are our own choice. Units: centimetres and milliseconds.
namespace {
constexpr float rainPps        = 3600.f;
constexpr float rainSpeed      = 1.0f;
constexpr float rainLife       = 1500.f;
constexpr float splashPps      = 480.f;   // all splash emitters together
constexpr float splashRadius   = 900.f;
constexpr int   splashProbes   = 2;       // new spots tried per tick
constexpr float splashLife     = 350.f;
constexpr float snowPps        = 700.f;
constexpr float snowSpeed      = 0.12f;
constexpr float snowLife       = 7000.f;
constexpr float areaRadius     = 1200.f;
constexpr float areaHeight     = 800.f;
constexpr float baseTiltDeg    = 18.f;   // rain always falls a little slanted
constexpr float maxTiltDeg     = 45.f;
constexpr float tiltPerWind    = 25.f;
constexpr float openThreshold  = 0.2f;   // below this the sky counts as covered
constexpr uint64_t loopCrossfadeMs = 1500;  // overlap of the two rain sound instances
constexpr float soundMaxDist   = 100000.f;
constexpr float rainVolume     = 1.0f;
constexpr float snowVolume     = 0.6f;
constexpr float speedOfSound   = 343.f;  // m/s
constexpr float thunderVolume  = 1.0f;
constexpr float thunderFarMeters = 3000.f;
constexpr float thunderPlaceDist = 2500.f; // cm, where the sound source is put around the player
constexpr float shelteredGain  = 0.12f;  // loudness when fully under cover
}

static Tempest::Sound loadThunderSample() {
  auto s = Resources::loadSoundBuffer("NEARTHUNDER.WAV");
  if(!s.isEmpty())
    return s;
  // data without the thunder sample: use a spell impact instead
  return Resources::loadSoundBuffer("MFX_THUNDERBALL_COLLIDE.WAV");
  }

std::unique_ptr<ParticleFx> WeatherFx::makeFx(const char* name, const char* texture, const char* blend, float aniFps) {
  zenkit::IParticleEffect src {};
  src.vis_name_s       = texture;
  src.vis_alpha_func_s = blend;
  src.vis_tex_ani_fps  = aniFps;
  src.shp_type_s       = "BOX";
  src.shp_for_s        = "OBJECT";
  src.dir_mode_s       = "DIR";
  src.dir_for_s        = "WORLD";
  src.vis_orientation_s = "NONE";
  src.vis_tex_color_start_s = "255 255 255";
  src.vis_tex_color_end_s   = "255 255 255";
  src.shp_offset_vec_s = "0 0 0";
  src.shp_dim_s        = "0 0 0";
  src.pps_fps          = 1;
  src.pps_is_looping   = 1;
  src.use_emitters_for = 0; // particles stay where they were born, they do not follow the player
  src.shp_is_volume    = 1;
  return std::make_unique<ParticleFx>(src, name);
  }

WeatherFx::WeatherFx(World& owner) : owner(owner), shelter(owner) {
  const float wind = Gothic::settingsGetF("SKY_OUTDOOR","zRainWindScale");
  windTilt = std::clamp(baseTiltDeg + wind*tiltPerWind, 0.f, maxTiltDeg);
  initLayers();
  initSounds();
  }

WeatherFx::~WeatherFx() = default;

void WeatherFx::initLayers() {
  const float perTier = 1.f/float(Tiers);

  rain.fx = makeFx("WEATHER_RAIN","SKYRAIN.TGA","ADD");
  {
    auto& p = *rain.fx;
    p.ppsValue         = rainPps*perTier;
    p.shpDim           = Vec3(areaRadius, 100.f, areaRadius);
    p.shpOffsetVec     = Vec3(0.f, areaHeight, 0.f);
    p.dirAngleElev     = -(90.f - windTilt);
    p.dirAngleElevVar  = 5.f;   // drops differ a little, as in gusty wind
    p.velAvg           = rainSpeed;
    p.lspPartAvg       = rainLife;
    p.visOrientation   = ParticleFx::Orientation::Velocity;
    // the texture holds one thin streak in the middle of a square: the quad is wide enough for it
    p.visSizeStart     = Vec2(16.f, 90.f);
    p.visSizeEndScale  = 1.f;
    p.visAlphaStart    = 0.45f;
    p.visAlphaEnd      = 0.45f;
    p.weatherBoost     = true;
    }

  splash.fx = makeFx("WEATHER_SPLASH","SKYRAINSPLASH.TGA","BLEND");
  {
    auto& p = *splash.fx;
    p.ppsValue         = splashPps/float(SplashPoints);
    p.shpType          = ParticleFx::EmitterType::Point;
    p.dirAngleElev     = 90.f;
    p.velAvg           = 0.f;
    p.lspPartAvg       = splashLife;
    p.visSizeStart     = Vec2(22.f, 22.f);   // a small crown of droplets that grows and fades
    p.visSizeEndScale  = 1.8f;
    p.visAlphaStart    = 0.9f;
    p.visAlphaEnd      = 0.f;
    p.weatherBoost     = true;
    }

  snow.fx = makeFx("WEATHER_SNOW","SNOW_A0.TGA","BLEND",14.f);
  {
    auto& p = *snow.fx;
    p.ppsValue         = snowPps*perTier;
    p.shpDim           = Vec3(areaRadius, 100.f, areaRadius);
    p.shpOffsetVec     = Vec3(0.f, areaHeight, 0.f);
    p.dirAngleElev     = -(90.f - windTilt*0.5f);
    p.dirAngleElevVar  = 10.f;
    p.velAvg           = snowSpeed;
    p.velVar           = snowSpeed*0.3f;
    p.lspPartAvg       = snowLife;
    p.visSizeStart     = Vec2(50.f, 50.f); // one small flake drifting inside a square frame (26 frames)
    p.visSizeEndScale  = 1.f;
    p.visAlphaStart    = 0.9f;
    p.visAlphaEnd      = 0.9f;
    }

  for(Layer* l : {&rain, &snow})
    for(auto& e : l->em) {
      e = PfxEmitter(owner, l->fx.get());
      e.setLooped(true);
      e.setActive(false);
      }

  for(auto& e : splashPts) {
    e = PfxEmitter(owner, splash.fx.get());
    e.setLooped(true);
    e.setActive(false);
    }
  }

void WeatherFx::tickSplashes(float level, const Vec3& pos) {
  auto* phys = owner.physic();
  if(phys==nullptr)
    return;

  // move a few emitters to new random spots; a spot must face up and be open to the sky
  for(int k=0; k<splashProbes; ++k) {
    const size_t idx = splashNext++ % SplashPoints;
    const float  a   = float(rng()%6283)/1000.f;
    const float  r   = splashRadius*std::sqrt(float(rng()%1000)/1000.f);
    const Vec3   from = pos + Vec3(std::cos(a)*r, 300.f, std::sin(a)*r);
    const auto   hit  = phys->ray(from, from-Vec3(0.f,1800.f,0.f));

    // water is a separate surface from the land collision: splashes also fall on it
    bool  ok = hit.hasCol && std::abs(hit.n.y)>0.5f;
    float surfaceY = ok ? hit.v.y : 0.f;
    const auto water = phys->waterRay(Vec3(from.x, from.y-2200.f, from.z), 0.f);
    if(water.hasCol && water.wdepth<from.y && (!hit.hasCol || water.wdepth>hit.v.y)) {
      ok       = true;
      surfaceY = water.wdepth;
      }
    if(ok) {
      const Vec3 at = Vec3(hit.hasCol ? hit.v.x : from.x, surfaceY + 3.f, hit.hasCol ? hit.v.z : from.z);
      ok = !phys->ray(at, at+Vec3(0.f,3000.f,0.f)).hasCol;
      if(ok)
        splashPts[idx].setPosition(at);
      }
    splashOk[idx] = ok;
    }

  for(size_t i=0; i<SplashPoints; ++i) {
    auto& e = splashPts[i];
    if(e.isEmpty())
      continue;
    const float tier = float(i*Tiers/SplashPoints)/float(Tiers);
    e.setActive(splashOk[i] && level>tier+0.01f);
    }
  }

void WeatherFx::initSounds() {
  auto* game = Gothic::inst().gameSession();
  if(game==nullptr)
    return;
  auto load = [game](Loop& l, std::string_view name) {
    auto raw = Resources::loadSoundBuffer(name);
    if(raw.isEmpty())
      return;
    for(auto& e : l.eff) {
      e = game->loadSound(raw);
      if(e.isEmpty())
        return;
      e.setMaxDistance(soundMaxDist);
      e.setVolume(0.f);
      }
    };
  load(rainSnd,"RAIN_01.WAV");
  load(snowSnd,"SNOW_01.WAV");
  }

void WeatherFx::tickThunder(uint64_t dt, const Vec3& pos) {
  auto* game = Gothic::inst().gameSession();

  float dist = 0;
  if(owner.weather().takeStrike(dist)) {
    // the strike is somewhere in front of the camera, so that the flash can be seen
    Vec3 front = Vec3(0.f,0.f,1.f);
    if(auto* cam = Gothic::inst().camera())
      front = cam->listenerPosition().front;
    const float base = std::atan2(front.z, front.x);
    const float a    = base + (float(rng()%1000)/1000.f - 0.5f)*1.6f;  // within about +-45 degrees
    const Vec3  dir  = Vec3(std::cos(a),0.f,std::sin(a));

    // the sky shader draws the bolt at this azimuth; the seed makes each bolt look different
    owner.weather().setBolt(a, float(rng()%10000)/10000.f);

    // the sound arrives after the flash, later when the strike is far
    auto raw = loadThunderSample();
    ++thunderNext;
    if(game!=nullptr && !raw.isEmpty()) {
      Thunder t;
      t.eff = game->loadSound(raw);
      if(!t.eff.isEmpty()) {
        const float k = std::clamp(1.f - dist/thunderFarMeters, 0.15f, 1.f);
        t.eff.setMaxDistance(soundMaxDist);
        t.eff.setPosition(pos + dir*thunderPlaceDist);
        t.eff.setVolume(thunderVolume*k*(shelteredGain*2.f+(1.f-shelteredGain*2.f)*openness));
        t.delay = dist/speedOfSound*1000.f;
        thunder.push_back(std::move(t));
        }
      }
    }

  for(size_t i=0; i<thunder.size();) {
    auto& t = thunder[i];
    if(!t.started) {
      t.delay -= float(dt);
      if(t.delay<=0.f) {
        t.eff.play();
        t.started = true;
        }
      ++i;
      continue;
      }
    if(t.eff.isFinished()) {
      thunder[i] = std::move(thunder.back());
      thunder.pop_back();
      continue;
      }
    ++i;
    }
  }

void WeatherFx::tickLoop(Loop& l, float volume, const Vec3& pos) {
  if(l.eff[0].isEmpty() || l.eff[1].isEmpty())
    return;

  if(volume<=0.001f) {
    for(int i=0; i<2; ++i)
      if(l.playing[i]) {
        l.eff[i].pause();
        l.playing[i] = false;
        }
    return;
    }

  // an instance that the sound device reports as finished (or never started) is not playing
  for(int i=0; i<2; ++i)
    if(l.playing[i] && l.eff[i].isFinished())
      l.playing[i] = false;

  uint64_t length = l.eff[0].timeLength();
  uint64_t fade   = std::min<uint64_t>(loopCrossfadeMs, length/3);

  // start the other instance when the first one nears its end; start one at all if none plays
  if(!l.playing[0] && !l.playing[1]) {
    l.eff[0].play();
    l.playing[0] = true;
    }
  for(int i=0; i<2; ++i) {
    const int other = 1-i;
    if(l.playing[i] && !l.playing[other] && length>0 && l.eff[i].currentTime()+fade>=length) {
      l.eff[other].play();
      l.playing[other] = true;
      }
    }

  for(int i=0; i<2; ++i) {
    if(!l.playing[i])
      continue;
    float k = 1.f;
    if(fade>0) {
      const uint64_t t = l.eff[i].currentTime();
      k = std::min(1.f, float(t)/float(fade));              // fade in
      if(length>t)
        k = std::min(k, float(length-t)/float(fade));       // fade out
      }
    l.eff[i].setPosition(pos);
    l.eff[i].setVolume(volume*k);
    }
  }

void WeatherFx::setLayer(Layer& l, float level, const Vec3& pos) {
  for(size_t i=0; i<Tiers; ++i) {
    auto& e = l.em[i];
    if(e.isEmpty())
      continue;
    e.setPosition(pos);
    // tier i is on while the level is above its share of the range
    e.setActive(level > float(i)/float(Tiers) + 0.01f);
    }
  }

// A global wind that changes slowly: its heading drifts, and it blows in gusts. Both rain and
// snow lean with it; the strength scale comes from the INI (our own choice of periods).
void WeatherFx::tickWind(uint64_t dt) {
  windTime += double(dt)/1000.0;
  const float heading = 70.f*std::sin(float(windTime/97.0)) + 40.f*std::sin(float(windTime/41.0) + 1.3f);
  const float gust    = 0.75f + 0.25f*std::sin(float(windTime/13.0));
  const float tilt    = std::clamp(windTilt*gust, 0.f, maxTiltDeg);

  // only touch the shared effects when the change is noticeable
  if(std::abs(heading-windHeading)<1.f && std::abs(tilt-windGustTilt)<0.5f)
    return;
  windHeading  = heading;
  windGustTilt = tilt;
  rain.fx->dirAngleHead = heading;
  rain.fx->dirAngleElev = -(90.f - tilt);
  snow.fx->dirAngleHead = heading;
  snow.fx->dirAngleElev = -(90.f - tilt*0.5f);
  }

void WeatherFx::tick(uint64_t dt) {
  tickWind(dt);
  auto* pl = owner.player();
  if(pl==nullptr)
    return;
  const Vec3 pos = pl->position();

  const auto& w = owner.weather();
  shelter.tick(dt,pos);
  openness = shelter.openness();
  const float open = openness>openThreshold ? openness : 0.f;

  const float rainLevel = w.rainIntensity()*open;
  const float snowLevel = w.snowIntensity()*open;

  // wetness and the light darkening are weaker when the player is under cover
  owner.weather().setOpenness(openness);

  // sound follows intensity and is much quieter under cover
  const float gain = shelteredGain + (1.f-shelteredGain)*openness;
  tickLoop(rainSnd, w.rainIntensity()*gain*rainVolume, pos);
  tickLoop(snowSnd, w.snowIntensity()*gain*snowVolume, pos);

  tickThunder(dt,pos);

  setLayer(rain,   rainLevel, pos);
  tickSplashes(rainLevel, pos);
  setLayer(snow,   snowLevel, pos);
  }
