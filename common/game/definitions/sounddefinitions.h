#pragma once

#include <zenkit/addon/daedalus.hh>

#include <unordered_map>
#include <string>
#include <vector>

class SoundDefinitions final {
  public:
    SoundDefinitions();

    const zenkit::ISoundEffect& operator [](std::string_view name) const;

    // names of all sound definitions that contain `needle` (case-insensitive)
    std::vector<std::string>    namesContaining(std::string_view needle) const;

  private:
    std::unordered_map<std::string, std::shared_ptr<zenkit::ISoundEffect>> sfx;
  };

