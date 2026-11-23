#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <Tempest/SoundEffect>

#include "graphics/pfx/particlefx.h"
#include "world/objects/pfxemitter.h"
#include "world/shelter.h"

class World;

// Visual precipitation: drops, splashes and flakes around the player, built on the particle system.
// Density follows Weather intensity in steps (several emitters, switched on one by one) and
// drops are only emitted while the sky above the player is open.
class WeatherFx final {
  public:
    explicit WeatherFx(World& owner);
    ~WeatherFx();

    void tick(uint64_t dt);

  private:
    static constexpr size_t Tiers = 4;

    struct Layer final {
      std::unique_ptr<ParticleFx>     fx;
      std::array<PfxEmitter,Tiers>    em;
      };

    struct Loop final {
      Tempest::SoundEffect eff;
      bool                 playing = false;
      };

    struct Thunder final {
      Tempest::SoundEffect eff;
      float                delay = 0;   // ms until it is heard
      bool                 started = false;
      };

    void  tickSplashes(float level, const Tempest::Vec3& pos);
    void  initLayers();
    void  tickThunder(uint64_t dt, const Tempest::Vec3& pos);
    void  initSounds();
    void  tickLoop(Loop& l, float volume, const Tempest::Vec3& pos);
    void  setLayer(Layer& l, float level, const Tempest::Vec3& pos);
    static std::unique_ptr<ParticleFx> makeFx(const char* name, const char* texture, const char* blend, float aniFps = 0.f);

    World&  owner;
    Layer   rain, splash, snow;

    // splashes: small emitters moved to random spots on whatever surface is found below
    static constexpr size_t SplashPoints = 24;
    std::array<PfxEmitter,SplashPoints> splashPts;
    std::array<bool,SplashPoints>       splashOk = {};
    size_t                              splashNext = 0;
    std::minstd_rand                    rng{12345};
    Loop    rainSnd, snowSnd;
    std::vector<Thunder>     thunder;
    size_t  thunderNext = 0;
    Shelter shelter;
    float   openness = 1.f;   // smoothed, 0..1, from the shelter
    float   windTilt = 0.f;   // degrees from vertical
  };
