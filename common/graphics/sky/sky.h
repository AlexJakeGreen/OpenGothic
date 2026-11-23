#pragma once

#include <Tempest/Matrix4x4>
#include <Tempest/Texture2d>

#include "graphics/lightsource.h"
#include "graphics/sceneglobals.h"
#include "resources.h"

class World;
class gtime;

class Sky final {
  public:
    struct State final {
      const Tempest::Texture2d* lay[2] = {};
      };

    Sky(std::string_view wname);
    ~Sky();

    // what the weather asks of the sky and lighting
    struct WeatherState final {
      float overcast    = 0;
      float lightning   = 0;
      float wetness     = 0;
      float boltAzimuth = 0;   // world azimuth of the lightning bolt, radians
      float boltSeed    = 0;   // varies the bolt's shape
      };

    void updateLight(const gtime gameTime, const WeatherState& weather);

    const LightSource&        sunLight()         const { return sun;     }
    const Tempest::Vec3&      ambientLight()     const { return ambient; }
    float                     sunIntensity()     const { return GSunIntensity;  }
    float                     moonIntensity()    const { return GMoonIntensity; }

    const Tempest::Texture2d& sunImage()  const { return *sunImg;  }
    const Tempest::Texture2d& moonImage() const { return *moonImg; }

    // dense rain clouds replace the day layers once the sky is mostly covered
    const State&              cloudsDay()   const { return (overcast>=rainLayerAt && rainClouds.lay[0]!=nullptr) ? rainClouds : clouds[0]; }
    float                     overcastLevel() const { return overcast; }
    float                     lightningLevel() const { return lightning; }
    float                     wetnessLevel()   const { return wetness;   }
    float                     boltAzimuth()    const { return boltAz;    }
    float                     boltShape()      const { return boltSeed;  }
    const Tempest::Texture2d& boltImage()      const { return *boltImg;   }
    const State&              cloudsNight() const { return clouds[1]; }
    Tempest::Vec2             cloudsOffset(int layer, uint64_t tickCount) const;
    float                     isNight() const;

  private:
    const Tempest::Texture2d*     skyTexture(std::string_view name, bool day, size_t id);
    const Tempest::Texture2d*     implSkyTexture(std::string_view name, bool day, size_t id);

    LightSource                   sun;
    Tempest::Vec3                 ambient;

    State                         clouds[2]; //day, night;
    State                         rainClouds;
    float                         overcast = 0;
    float                         lightning = 0;
    float                         wetness   = 0;
    float                         boltAz    = 0;
    float                         boltSeed  = 0;
    const Tempest::Texture2d*     boltImg   = &Resources::fallbackBlack();
    static constexpr float        rainLayerAt = 0.5f; // our own choice

    const Tempest::Texture2d*     sunImg   = &Resources::fallbackBlack();
    const Tempest::Texture2d*     moonImg  = &Resources::fallbackBlack();

    float                         GSunIntensity  = 0;
    float                         GMoonIntensity = 0;
  };
