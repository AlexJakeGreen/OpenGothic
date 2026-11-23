#pragma once

#include <cstdint>
#include <random>
#include <string_view>
#include <vector>

#include "game/gametime.h"

// Precipitation state of a world: daily schedule, manual overrides and per-channel intensity.
// Pure logic - no rendering or audio. Driven by game time, nothing here is saved.
class Weather final {
  public:
    enum Kind : uint8_t {
      Rain = 0,
      Snow = 1,
      };

    // `effects` mirrors the GAME/skyEffects setting: false means no weather at all
    // `rainPerDay`: rain windows per game day, fractional allowed. `permanentStorm`: debugging aid.
    Weather(std::string_view worldName, bool effects, float rainPerDay = 1.f, bool permanentStorm = false);

    void  tick(gtime now);

    // manual control (console commands). Start opens a window around `now`.
    void  startRain(gtime now);
    void  stopRain (gtime now);
    void  startSnow(gtime now);
    void  stopSnow (gtime now);

    // Precipitation (drops, sound, wetness): it starts only after the sky has closed in,
    // and ends before the sky clears.
    float rainIntensity() const;
    float snowIntensity() const;
    bool  isThunderstorm() const { return channel[Rain].thunder && channel[Rain].value>0; }
    bool  isRaining() const { return rainIntensity()>rainThreshold; }

    // Lightning: driven by real elapsed time (milliseconds), separate from the game-time schedule.
    void  tickLightning(uint64_t dt);
    // brightness of the current flash, 0..1
    float lightning() const { return flash; }
    // true once per strike; `distanceMeters` is how far away it hit (thunder arrives later)
    bool  takeStrike(float& distanceMeters);
    // where the current bolt is drawn: world azimuth (radians) and a seed that varies its shape;
    // chosen by the visual side, which knows where the camera looks
    void  setBolt(float azimuth, float seed) { boltAz = azimuth; boltSeed = seed; }
    float boltAzimuth() const { return boltAz;   }
    float boltShape()   const { return boltSeed; }

    // How open the sky above the player is, 0..1 (see Shelter). Set by the visual side.
    void  setOpenness(float v) { open = v; }

    // How wet the world looks, 0..1: follows the rain, weaker for a sheltered player, zero for snow.
    float wetness() const;
    // Overcast as seen by scene lighting (sun, exposure): weaker under cover. The sky and haze use overcast().
    float overcastLight() const;

    // how dark and heavy the sky is, 0..1 (rain dominates, snow is lighter)
    float overcast() const;

    // true when the world has no automatic schedule (no open sky)
    bool  autoDisabled() const { return noAuto; }

  private:
    struct Channel final {
      int64_t begin   = 0;      // window begin, game milliseconds
      int64_t end     = 0;      // window end
      int64_t rampIn  = 0;      // fade-in time
      int64_t rampOut = 0;      // fade-out time
      bool    active  = false;
      bool    thunder = false;
      // forced fade-out (manual stop): linear decay from `stopFrom` starting at `stopAt`
      bool    stopping = false;
      int64_t stopAt   = 0;
      float   stopFrom = 0;
      float   value    = 0;
      };

    struct Planned final {
      int64_t begin;
      int64_t duration;
      };

    void    planDay(int64_t day);
    void    startPlanned(int64_t t);
    // `fade` is the fade-in/out time in game milliseconds, negative: a part of the window
    void    openWindow(Kind k, int64_t begin, int64_t duration, bool thunder, int64_t fade = -1);
    void    stop(Kind k, gtime now);
    void    update(Channel& c, int64_t t);
    bool    rollThunder();
    int64_t uniform(int64_t lo, int64_t hi);

    static constexpr float rainThreshold = 0.3f;

    float         open         = 1;
    float         flash        = 0;
    int64_t       flashAge     = -1;   // ms since the strike, -1 when idle
    int64_t       nextStrike   = 0;    // ms until the next strike
    bool          strikePending = false;
    float         strikeDist   = 0;
    float         boltAz       = 0;
    float         boltSeed     = 0;

    Channel       channel[2];
    bool          noAuto       = false;
    bool          effects      = true;
    float         rainPerDay   = 1.f;
    bool          permanentStorm = false;
    std::vector<Planned> plan;
    bool          firstPlanned = false;
    int64_t       plannedDay   = -1;
    int           plainRains   = 0;   // rains since the last thunderstorm
    std::mt19937  rng;
  };
