#include "sounddefinitions.h"

#include <Tempest/Log>
#include <algorithm>
#include <cctype>
#include <gothic.h>

using namespace Tempest;

SoundDefinitions::SoundDefinitions() {
  auto vm = Gothic::inst().createPhoenixVm("Sfx.dat");

  vm->enumerate_instances_by_class_name("C_SFX", [this, &vm](zenkit::DaedalusSymbol& s) {
    try {
      this->sfx[s.name()] = vm->init_instance<zenkit::ISoundEffect>(&s);
      }
    catch(const zenkit::DaedalusScriptError&) {
      // There was an error during initialization. Ignore it.
      }
  });
  }

std::vector<std::string> SoundDefinitions::namesContaining(std::string_view needle) const {
  auto lower = [](std::string s) {
    for(auto& c:s)
      c = char(std::tolower(uint8_t(c)));
    return s;
    };
  const std::string n = lower(std::string(needle));
  std::vector<std::string> ret;
  for(auto& i:sfx)
    if(lower(i.first).find(n)!=std::string::npos)
      ret.push_back(i.first);
  std::sort(ret.begin(),ret.end()); // stable order independent of hashing
  return ret;
  }

const zenkit::ISoundEffect& SoundDefinitions::operator[](std::string_view name) const {
  std::string buf(name); // FIXME
  auto i = sfx.find(buf);
  if(i!=sfx.end())
    return *i->second;
  static const zenkit::ISoundEffect s {};
  return s;
  }

