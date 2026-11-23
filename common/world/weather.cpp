#include "weather.h"

#include <algorithm>
#include <cctype>

namespace {
// all values below are our own choice
constexpr int64_t minute = 60*1000;
constexpr int64_t hour   = 60*minute;
constexpr int64_t day    = 24*hour;

constexpr int64_t rainMinDuration  = 60*minute;
constexpr int64_t rainMaxDuration  = 150*minute;
constexpr int64_t firstRainMin     = 16*hour + 30*minute;
constexpr int64_t firstRainMax     = 18*hour;
constexpr int64_t manualDuration   = 90*minute;
constexpr int64_t manualFade       = 220*1000; // fade of a window started by hand, about 15 s of real time
constexpr int     plainRainsFree   = 3;     // no lightning in the first rains
constexpr float   thunderStep      = 0.15f; // added chance per further plain rain
constexpr float   thunderMax       = 0.6f;
constexpr int64_t strikeGapMin     = 6000;   // real ms between strikes
constexpr int64_t strikeGapMax     = 22000;
constexpr float   strikeDistMin    = 300.f;  // metres
constexpr float   strikeDistMax    = 2500.f;
constexpr float   stormMinRain     = 0.4f;   // no lightning while the rain is still weak
constexpr float   shelteredWetness = 0.3f;  // wetness left when fully under cover
constexpr float   shelteredLight   = 0.4f;  // overcast seen by the lighting when fully under cover
constexpr float   cloudsDoneAt    = 0.35f;  // window profile at which the sky is fully overcast
constexpr float   precipStartsAt  = 0.30f;  // and from which precipitation begins
constexpr float   rampPart         = 0.2f;  // fade in/out part of a window

bool iequals(std::string_view a, std::string_view b) {
  if(a.size()!=b.size())
    return false;
  for(size_t i=0; i<a.size(); ++i)
    if(std::tolower(uint8_t(a[i]))!=std::tolower(uint8_t(b[i])))
      return false;
  return true;
  }

float ramp(int64_t t, int64_t begin, int64_t end, int64_t rampIn, int64_t rampOut) {
  const int64_t len = end-begin;
  if(len<=0 || t<begin || t>=end)
    return 0;
  if(rampIn>0 && t<begin+rampIn)
    return float(t-begin)/float(rampIn);
  if(rampOut>0 && t>end-rampOut)
    return float(end-t)/float(rampOut);
  return 1.f;
  }

float smoothstep(float e0, float e1, float x) {
  const float t = std::min(std::max((x-e0)/(e1-e0), 0.f), 1.f);
  return t*t*(3.f-2.f*t);
  }
}

Weather::Weather(std::string_view worldName, bool effects, float rainPerDay, bool permanentStorm)
  : effects(effects), rainPerDay(std::max(0.f,rainPerDay)), permanentStorm(permanentStorm), rng(std::random_device{}()) {
  // worlds without a sky get no automatic weather
  noAuto = iequals(worldName,"DRAGONISLAND.ZEN");
  }

int64_t Weather::uniform(int64_t lo, int64_t hi) {
  if(hi<=lo)
    return lo;
  return std::uniform_int_distribution<int64_t>(lo,hi)(rng);
  }

bool Weather::rollThunder() {
  if(plainRains<plainRainsFree)
    return false;
  const float p = std::min(thunderMax, thunderStep*float(plainRains-plainRainsFree+1));
  return std::uniform_real_distribution<float>(0.f,1.f)(rng)<p;
  }

void Weather::openWindow(Kind k, int64_t begin, int64_t duration, bool thunder, int64_t fade) {
  Channel& c = channel[k];
  // scheduled windows fade over a fifth of their length at each end; manual ones over a fixed short time
  c.rampIn   = fade>=0 ? fade : int64_t(float(duration)*rampPart);
  c.rampOut  = c.rampIn;
  c.begin    = begin;
  c.end      = begin+duration;
  c.active   = true;
  c.thunder  = thunder;
  c.stopping = false;
  }

void Weather::planDay(int64_t d) {
  plannedDay = d;
  plan.clear();
  if(noAuto || rainPerDay<=0.f)
    return;

  // the first rain of a new game falls in the late afternoon of the first day
  if(!firstPlanned && d==0) {
    firstPlanned = true;
    plan.push_back({d*day + uniform(firstRainMin,firstRainMax), uniform(rainMinDuration,rainMaxDuration)});
    return;
    }
  firstPlanned = true;

  // whole part of the frequency always happens, the fractional part is a chance
  int n = int(rainPerDay);
  if(std::uniform_real_distribution<float>(0.f,1.f)(rng) < rainPerDay-float(n))
    ++n;

  // one window per equal part of the day, so windows never overlap and stay inside the day
  for(int i=0; i<n; ++i) {
    const int64_t slotBegin = day*i/n;
    const int64_t slotEnd   = day*(i+1)/n;
    const int64_t dur       = std::min(uniform(rainMinDuration,rainMaxDuration), slotEnd-slotBegin);
    plan.push_back({d*day + uniform(slotBegin,slotEnd-dur), dur});
    }
  }

// starts a planned window when its time has come; a manual rain in progress is left alone
void Weather::startPlanned(int64_t t) {
  while(!plan.empty() && t>=plan.front().begin) {
    const Planned p = plan.front();
    plan.erase(plan.begin());
    if(t>=p.begin+p.duration || channel[Rain].active)
      continue;
    const bool thunder = rollThunder();
    plainRains = thunder ? 0 : plainRains+1;
    openWindow(Rain,p.begin,p.duration,thunder);
    }
  }

void Weather::update(Channel& c, int64_t t) {
  if(c.stopping) {
    const float k = 1.f-float(t-c.stopAt)/float(manualFade);
    c.value = std::max(0.f,c.stopFrom*k);
    if(c.value<=0) {
      c.stopping = false;
      c.active   = false;
      }
    return;
    }
  if(!c.active) {
    c.value = 0;
    return;
    }
  if(t>=c.end) {
    c.active = false;
    c.value  = 0;
    return;
    }
  c.value = ramp(t,c.begin,c.end,c.rampIn,c.rampOut);
  }

float Weather::rainIntensity() const {
  return smoothstep(precipStartsAt, 1.f, channel[Rain].value);
  }

float Weather::snowIntensity() const {
  return smoothstep(precipStartsAt, 1.f, channel[Snow].value);
  }

float Weather::wetness() const {
  return rainIntensity()*(shelteredWetness + (1.f-shelteredWetness)*open);
  }

float Weather::overcastLight() const {
  return overcast()*(shelteredLight + (1.f-shelteredLight)*open);
  }

float Weather::overcast() const {
  // the sky closes in first: fully overcast before the precipitation really starts
  return smoothstep(0.f, cloudsDoneAt, std::max(channel[Rain].value, channel[Snow].value*0.6f));
  }

void Weather::tick(gtime now) {
  if(!effects)
    return;
  const int64_t t = now.toInt();
  if(now.day()!=plannedDay)
    planDay(now.day());
  startPlanned(t);
  update(channel[Rain],t);
  update(channel[Snow],t);
  if(permanentStorm && !noAuto) {
    channel[Rain].value   = 1.f;
    channel[Rain].thunder = true;
    }
  }

namespace {
// two quick flashes with a gap, then a fade; age in milliseconds
float flashShape(int64_t age) {
  if(age<0)
    return 0.f;
  if(age<90)
    return 1.f;
  if(age<170)
    return 0.15f;
  if(age<280)
    return 0.8f;
  if(age<700)
    return 0.8f*float(700-age)/420.f;
  return 0.f;
  }
}

void Weather::tickLightning(uint64_t dt) {
  const int64_t d = int64_t(dt);
  if(!isThunderstorm() || channel[Rain].value<stormMinRain) {
    flashAge = -1;
    flash    = 0;
    return;
    }
  if(flashAge>=0) {
    flashAge += d;
    if(flashAge>=700)
      flashAge = -1;
    }
  nextStrike -= d;
  if(nextStrike<=0 && flashAge<0) {
    flashAge      = 0;
    nextStrike    = uniform(strikeGapMin,strikeGapMax);
    strikeDist    = float(uniform(int64_t(strikeDistMin),int64_t(strikeDistMax)));
    strikePending = true;
    }
  // a far strike is dimmer
  const float nearness = std::clamp(1.f - strikeDist/(strikeDistMax*1.1f), 0.2f, 1.f);
  flash = flashShape(flashAge)*nearness;
  }

bool Weather::takeStrike(float& distanceMeters) {
  if(!strikePending)
    return false;
  strikePending  = false;
  distanceMeters = strikeDist;
  return true;
  }

void Weather::startRain(gtime now) {
  if(!effects)
    return;
  const bool thunder = rollThunder();
  plainRains = thunder ? 0 : plainRains+1;
  openWindow(Rain,now.toInt(),manualDuration,thunder,manualFade);
  tick(now);
  }

void Weather::startSnow(gtime now) {
  if(!effects)
    return;
  openWindow(Snow,now.toInt(),manualDuration,false,manualFade);
  tick(now);
  }

void Weather::stop(Kind k, gtime now) {
  Channel& c = channel[k];
  if(!c.active || c.value<=0)
    return;
  c.stopping = true;
  c.stopAt   = now.toInt();
  c.stopFrom = c.value;
  }

void Weather::stopRain(gtime now) { stop(Rain,now); }
void Weather::stopSnow(gtime now) { stop(Snow,now); }
