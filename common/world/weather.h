#pragma once

#include <cstdint>
#include <random>
#include <string_view>

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
    Weather(std::string_view worldName, bool effects);

    void  tick(gtime now);

    // manual control (console commands). Start opens a window around `now`.
    void  startRain(gtime now);
    void  stopRain (gtime now);
    void  startSnow(gtime now);
    void  stopSnow (gtime now);

    float rainIntensity() const { return channel[Rain].value; }
    float snowIntensity() const { return channel[Snow].value; }
    bool  isThunderstorm() const { return channel[Rain].thunder && channel[Rain].value>0; }
    bool  isRaining() const { return channel[Rain].value>rainThreshold; }

    // how dark and heavy the sky is, 0..1 (rain dominates, snow is lighter)
    float overcast() const;

    // true when the world has no automatic schedule (no open sky)
    bool  autoDisabled() const { return noAuto; }

  private:
    struct Channel final {
      int64_t begin   = 0;      // window begin, game milliseconds
      int64_t end     = 0;      // window end
      bool    active  = false;
      bool    thunder = false;
      // forced fade-out (manual stop): linear decay from `stopFrom` starting at `stopAt`
      bool    stopping = false;
      int64_t stopAt   = 0;
      float   stopFrom = 0;
      float   value    = 0;
      };

    void    planDay(int64_t day);
    void    openWindow(Kind k, int64_t begin, int64_t duration, bool thunder);
    void    stop(Kind k, gtime now);
    void    update(Channel& c, int64_t t);
    bool    rollThunder();
    int64_t uniform(int64_t lo, int64_t hi);

    static constexpr float rainThreshold = 0.3f;

    Channel       channel[2];
    bool          noAuto       = false;
    bool          effects      = true;
    bool          firstPlanned = false;
    int64_t       plannedDay   = -1;
    int           plainRains   = 0;   // rains since the last thunderstorm
    std::mt19937  rng;
  };
