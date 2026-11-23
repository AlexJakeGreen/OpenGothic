#include "shelter.h"

#include <algorithm>
#include <cmath>

#include "physics/dynamicworld.h"
#include "world/world.h"

using namespace Tempest;

// All values are our own choice. Units: centimetres and milliseconds.
namespace {
constexpr uint64_t evalInterval  = 220;     // evaluate a few times per second
constexpr float    smoothTime    = 350.f;   // time constant of the smoothing, settles in about a second
constexpr float    headHeight    = 150.f;
constexpr float    upRayLength   = 3000.f;
constexpr float    slantLength   = 1800.f;  // about 18 m
constexpr int      slantBlocked  = 4;       // blocked rays (of 5, the one up included) that count as covered
}

Shelter::Shelter(World& owner) : owner(owner) {
  }

bool Shelter::isCovered(const Vec3& pos) const {
  auto* phys = owner.physic();
  if(phys==nullptr)
    return false;

  const Vec3 head    = pos + Vec3(0.f, headHeight, 0.f);
  const bool upBlock = phys->ray(head, head + Vec3(0.f, upRayLength, 0.f)).hasCol;

  // signal 1: the room lookup says inside and the ceiling is really there
  if(upBlock && !owner.roomAt(pos).empty())
    return true;

  // signal 2: long rays slanted upward at 45 degrees in four directions
  int blocked = upBlock ? 1 : 0;
  const float k = slantLength*0.70710678f;
  const Vec3  dirs[4] = { {k,k,0}, {-k,k,0}, {0,k,k}, {0,k,-k} };
  for(auto& d : dirs)
    if(phys->ray(head, head + d).hasCol)
      ++blocked;
  return blocked>=slantBlocked;
  }

void Shelter::tick(uint64_t dt, const Vec3& playerPos) {
  sinceEval += dt;
  if(sinceEval>=evalInterval) {
    sinceEval = 0;
    target    = isCovered(playerPos) ? 0.f : 1.f;
    }
  const float a = 1.f - std::exp(-float(dt)/smoothTime);
  open += (target-open)*a;
  }
