#pragma once

#include <cstdint>

#include <Tempest/Vec>

class World;

// Tells whether the player stands under cover: a smoothed "open sky" factor, 1 outdoors, about 0 under a roof.
// Two independent signals are combined, because each one alone is wrong in some places:
//  - the player is inside a room/sector AND a ray straight up is blocked;
//  - or most of five long rays (one up, four slanted upward) are blocked.
class Shelter final {
  public:
    explicit Shelter(World& owner);

    void  tick(uint64_t dt, const Tempest::Vec3& playerPos);
    float openness() const { return open; }

  private:
    bool  isCovered(const Tempest::Vec3& pos) const;

    World&   owner;
    float    open     = 1.f;   // smoothed result
    float    target   = 1.f;   // last evaluation: 1 open, 0 covered
    bool     started  = false; // the first evaluation is applied without smoothing
    uint64_t sinceEval = 0;    // ms since the last evaluation
  };
