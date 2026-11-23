#include "weatherfx.h"

#include <algorithm>
#include <cmath>

#include "world/objects/npc.h"
#include "world/world.h"
#include "game/gamesession.h"
#include "resources.h"
#include "gothic.h"

using namespace Tempest;

// All particle numbers below are our own choice. Units: centimetres and milliseconds.
namespace {
constexpr float rainPps        = 1600.f;
constexpr float rainSpeed      = 1.0f;
constexpr float rainLife       = 1500.f;
constexpr float splashPps      = 500.f;
constexpr float splashLife     = 300.f;
constexpr float snowPps        = 700.f;
constexpr float snowSpeed      = 0.12f;
constexpr float snowLife       = 7000.f;
constexpr float areaRadius     = 1200.f;
constexpr float areaHeight     = 800.f;
constexpr float maxTiltDeg     = 40.f;
constexpr float tiltPerWind    = 25.f;
constexpr float openThreshold  = 0.2f;   // below this the sky counts as covered
constexpr float soundMaxDist   = 100000.f;
constexpr float rainVolume     = 0.8f;
constexpr float snowVolume     = 0.6f;
constexpr float shelteredGain  = 0.12f;  // loudness when fully under cover
}

std::unique_ptr<ParticleFx> WeatherFx::makeFx(const char* name, const char* texture) {
  zenkit::IParticleEffect src {};
  src.vis_name_s       = texture;
  src.vis_alpha_func_s = "BLEND";
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
  src.use_emitters_for = 1;
  src.shp_is_volume    = 1;
  return std::make_unique<ParticleFx>(src, name);
  }

WeatherFx::WeatherFx(World& owner) : owner(owner) {
  const float wind = Gothic::settingsGetF("SKY_OUTDOOR","zRainWindScale");
  windTilt = std::clamp(wind*tiltPerWind, 0.f, maxTiltDeg);
  initLayers();
  initSounds();
  }

WeatherFx::~WeatherFx() = default;

void WeatherFx::initLayers() {
  const float perTier = 1.f/float(Tiers);

  rain.fx = makeFx("WEATHER_RAIN","SKYRAIN.TGA");
  {
    auto& p = *rain.fx;
    p.ppsValue         = rainPps*perTier;
    p.shpDim           = Vec3(areaRadius, 100.f, areaRadius);
    p.shpOffsetVec     = Vec3(0.f, areaHeight, 0.f);
    p.dirAngleElev     = -(90.f - windTilt);
    p.velAvg           = rainSpeed;
    p.lspPartAvg       = rainLife;
    p.visOrientation   = ParticleFx::Orientation::Velocity;
    p.visSizeStart     = Vec2(2.f, 45.f);
    p.visSizeEndScale  = 1.f;
    p.visAlphaStart    = 0.55f;
    p.visAlphaEnd      = 0.55f;
    }

  splash.fx = makeFx("WEATHER_SPLASH","SKYRAINSPLASH.TGA");
  {
    auto& p = *splash.fx;
    p.ppsValue         = splashPps*perTier;
    p.shpDim           = Vec3(areaRadius, 0.f, areaRadius);
    p.dirAngleElev     = 90.f;
    p.velAvg           = 0.f;
    p.lspPartAvg       = splashLife;
    p.visSizeStart     = Vec2(18.f, 18.f);
    p.visSizeEndScale  = 2.f;
    p.visAlphaStart    = 0.7f;
    p.visAlphaEnd      = 0.f;
    }

  snow.fx = makeFx("WEATHER_SNOW","SNOW.TGA");
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
    p.visSizeStart     = Vec2(7.f, 7.f);
    p.visSizeEndScale  = 1.f;
    p.visAlphaStart    = 0.9f;
    p.visAlphaEnd      = 0.9f;
    }

  for(Layer* l : {&rain, &splash, &snow})
    for(auto& e : l->em) {
      e = PfxEmitter(owner, l->fx.get());
      e.setLooped(true);
      e.setActive(false);
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
    l.eff = game->loadSound(raw);
    if(l.eff.isEmpty())
      return;
    l.eff.setMaxDistance(soundMaxDist);
    l.eff.setVolume(0.f);
    };
  load(rainSnd,"RAIN_01.WAV");
  load(snowSnd,"SNOW_01.WAV");
  }

void WeatherFx::tickLoop(Loop& l, float volume, const Vec3& pos) {
  if(l.eff.isEmpty())
    return;
  if(volume<=0.001f) {
    if(l.playing) {
      l.eff.pause();
      l.playing = false;
      }
    return;
    }
  l.eff.setPosition(pos);
  l.eff.setVolume(volume);
  if(!l.playing || l.eff.isFinished()) { // the sample is restarted by hand to loop
    l.eff.play();
    l.playing = true;
    }
  }

float WeatherFx::skyOpenness(const Vec3& pos) const {
  auto* phys = owner.physic();
  if(phys==nullptr)
    return 1.f;
  // a few rays straight up around the player: shelter means most of them hit something
  const float r = 120.f;
  const Vec3  offs[5] = { {0,0,0}, {r,0,0}, {-r,0,0}, {0,0,r}, {0,0,-r} };
  int open = 0;
  for(auto& o : offs) {
    const Vec3 from = pos + o + Vec3(0.f, 150.f, 0.f);
    const Vec3 to   = from + Vec3(0.f, 3000.f, 0.f);
    if(!phys->ray(from,to).hasCol)
      ++open;
    }
  return float(open)/float(std::size(offs));
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

void WeatherFx::tick(uint64_t dt) {
  auto* pl = owner.player();
  if(pl==nullptr)
    return;
  const Vec3 pos = pl->position();

  const auto& w = owner.weather();
  const float target = skyOpenness(pos);
  openness += (target-openness)*std::min(1.f, float(dt)/400.f);
  const float open = openness>openThreshold ? openness : 0.f;

  const float rainLevel = w.rainIntensity()*open;
  const float snowLevel = w.snowIntensity()*open;

  // splashes sit on the ground below the player
  Vec3 ground = pos;
  if(auto* phys = owner.physic()) {
    auto r = phys->ray(pos+Vec3(0,100,0), pos-Vec3(0,2000,0));
    if(r.hasCol)
      ground = r.v;
    }

  // sound follows intensity and is much quieter under cover
  const float gain = shelteredGain + (1.f-shelteredGain)*openness;
  tickLoop(rainSnd, w.rainIntensity()*gain*rainVolume, pos);
  tickLoop(snowSnd, w.snowIntensity()*gain*snowVolume, pos);

  setLayer(rain,   rainLevel, pos);
  setLayer(splash, rainLevel, ground);
  setLayer(snow,   snowLevel, pos);
  }
