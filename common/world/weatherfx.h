#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "graphics/pfx/particlefx.h"
#include "world/objects/pfxemitter.h"

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

    void  initLayers();
    void  setLayer(Layer& l, float level, const Tempest::Vec3& pos);
    float skyOpenness(const Tempest::Vec3& pos) const;
    static std::unique_ptr<ParticleFx> makeFx(const char* name, const char* texture);

    World&  owner;
    Layer   rain, splash, snow;
    float   openness = 1.f;   // smoothed, 0..1
    float   windTilt = 0.f;   // degrees from vertical
  };
