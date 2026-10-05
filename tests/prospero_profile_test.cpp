#include "runtime/prospero_profile.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

namespace {

using namespace playden::runtime;

std::vector<std::uint8_t> PeImage(unsigned machine) {
  std::vector<std::uint8_t> image(0x200, 0);
  image[0] = 'M';
  image[1] = 'Z';
  image[0x3c] = 0x80;
  image[0x80] = 'P';
  image[0x81] = 'E';
  image[0x84] = machine & 0xff;
  image[0x85] = machine >> 8;
  return image;
}

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream file(path);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

ProsperoGame HalfLife() {
  ProsperoGame game;
  game.app_id = 220;
  game.name = "Half-Life 2";
  game.install_root = "/data/homebrew/FAKE77031/games/220/";
  game.launch.executable = "hl2.exe";
  game.launch.arguments = "-game hl2 -steam";
  return game;
}

}  // namespace

int main() {
  // PE headers.
  auto pe32 = PeImage(0x014c);
  auto pe64 = PeImage(0x8664);
  auto arm = PeImage(0xaa64);
  CHECK(ParsePeArchitecture(pe32.data(), pe32.size()) == PeArchitecture::kPe32);
  CHECK(ParsePeArchitecture(pe64.data(), pe64.size()) == PeArchitecture::kPe64);
  CHECK(!ParsePeArchitecture(arm.data(), arm.size()));
  pe32[0x3c] = 0xff;
  pe32[0x3d] = 0xff;
  CHECK(!ParsePeArchitecture(pe32.data(), pe32.size()));  // header offset outside the file
  CHECK(!ParsePeArchitecture(pe64.data(), 0x20));

  // A Steam launch entry becomes an [application] profile with Z: paths.
  ProsperoProfile profile;
  std::string reason;
  CHECK(DraftProsperoProfile(HalfLife(), &profile, &reason));
  CHECK(profile.id == "playden-steam-220");
  CHECK(profile.text ==
        "; Drafted by Playden from Steam launch data.\n"
        "[application]\n"
        "id = playden-steam-220\n"
        "name = Half-Life 2\n"
        "executable = Z:\\data\\homebrew\\FAKE77031\\games\\220\\hl2.exe\n"
        "working_directory = Z:\\data\\homebrew\\FAKE77031\\games\\220\n"
        "arguments = -game hl2 -steam\n"
        "prefix = playden\n"
        "runtime = wine-wow64\n"
        "architecture = pe32\n"
        "graphics = auto\n");

  // Nested executables default to their own folder; an explicit working folder wins.
  auto nested = HalfLife();
  nested.launch.executable = "bin\\win64\\Game.exe";
  nested.launch.arguments.clear();
  nested.architecture = PeArchitecture::kPe64;
  nested.name = std::string("Bad\tName") + std::string(200, 'x');
  CHECK(DraftProsperoProfile(nested, &profile, &reason));
  CHECK(profile.text.find("executable = Z:\\data\\homebrew\\FAKE77031\\games\\220\\bin\\win64\\Game.exe\n") != std::string::npos);
  CHECK(profile.text.find("working_directory = Z:\\data\\homebrew\\FAKE77031\\games\\220\\bin\\win64\n") != std::string::npos);
  CHECK(profile.text.find("arguments") == std::string::npos);
  CHECK(profile.text.find("architecture = pe64") != std::string::npos);
  CHECK(profile.text.find("name = Bad Name") != std::string::npos);
  const auto name_line = profile.text.substr(profile.text.find("name = "));
  CHECK(name_line.find('\n') == 7 + kProsperoMaxNameBytes);
  nested.launch.working_directory = "bin";
  CHECK(DraftProsperoProfile(nested, &profile, &reason));
  CHECK(profile.text.find("working_directory = Z:\\data\\homebrew\\FAKE77031\\games\\220\\bin\n") != std::string::npos);

  // Unsafe or oversized inputs are refused with a reason, leaving the output untouched.
  const auto before = profile.text;
  auto bad = HalfLife();
  bad.launch.executable = "..\\escape.exe";
  CHECK(!DraftProsperoProfile(bad, &profile, &reason) && !reason.empty());
  bad = HalfLife();
  bad.install_root = "relative/games/220";
  CHECK(!DraftProsperoProfile(bad, &profile, &reason));
  bad.install_root = "/data/C:/games";
  CHECK(!DraftProsperoProfile(bad, &profile, &reason));
  bad = HalfLife();
  bad.launch.arguments = "-a\n-b";
  CHECK(!DraftProsperoProfile(bad, &profile, &reason));
  bad = HalfLife();
  bad.install_root = "/data/" + std::string(260, 'p');
  CHECK(!DraftProsperoProfile(bad, &profile, &reason));
  bad = HalfLife();
  bad.prefix = "../wine";
  CHECK(!DraftProsperoProfile(bad, &profile, &reason));
  CHECK(profile.text == before);

  // Publishing writes the profile and adds one catalog entry, keeping what was there.
  const auto root = std::filesystem::temp_directory_path() / ("playden-prospero-" + std::to_string(::getpid()));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "profiles");
  std::ofstream(root / "profiles" / "profiles.lst") << "# games\nnotepad.profile";
  CHECK(DraftProsperoProfile(HalfLife(), &profile, &reason));
  CHECK(PublishProsperoProfile(root.string(), profile, &reason));
  CHECK(PublishProsperoProfile(root.string(), profile, &reason));  // idempotent
  CHECK(ReadText(root / "profiles" / "playden-steam-220.profile") == profile.text);
  CHECK(ReadText(root / "profiles" / "profiles.lst") == "# games\nnotepad.profile\nplayden-steam-220.profile\n");

  // The catalog holds at most 16 entries.
  std::ofstream full(root / "profiles" / "profiles.lst");
  for (int index = 0; index < 16; ++index) full << "game-" << index << ".profile\n";
  full.close();
  auto other = HalfLife();
  other.app_id = 400;
  CHECK(DraftProsperoProfile(other, &profile, &reason));
  CHECK(!PublishProsperoProfile(root.string(), profile, &reason) && reason == "prospero-win profile list is full");
  std::filesystem::remove_all(root);

  std::puts("prospero profile tests passed");
}
