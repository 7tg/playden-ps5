#include "runtime/kit_manifest.h"
#if PLAYDEN_TEST_KIT_ADMISSION
#include "runtime/artifact_hash.h"
#include "runtime/kit_admission.h"
#endif

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace playden::runtime;

namespace {

std::string HashBytes(std::string_view bytes) {
#if PLAYDEN_TEST_KIT_ADMISSION
  return *HashArtifactBytes(bytes);
#else
  std::uint64_t value = 1469598103934665603ULL;
  for (unsigned char byte : bytes) value = (value ^ byte) * 1099511628211ULL;
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (unsigned index = 0; index < 4; ++index)
    output << std::setw(16) << (value + index * 0x9e3779b97f4a7c15ULL);
  return output.str();
#endif
}

std::optional<std::string> HashFile(const fs::path& path) {
#if PLAYDEN_TEST_KIT_ADMISSION
  return HashArtifactFile(path);
#else
  std::ifstream input(path, std::ios::binary);
  if (!input) return std::nullopt;
  return HashBytes(std::string(std::istreambuf_iterator<char>(input), {}));
#endif
}

void Write(const fs::path& path, const std::string& bytes, unsigned mode = 0644) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << bytes;
  ::chmod(path.c_str(), mode);
}

std::string Pe(bool pe64) {
  std::string bytes(512, '\0');
  bytes[0] = 'M'; bytes[1] = 'Z'; bytes[0x3c] = 0x80;
  bytes[0x80] = 'P'; bytes[0x81] = 'E';
  const unsigned machine = pe64 ? 0x8664 : 0x014c;
  bytes[0x84] = static_cast<char>(machine & 0xff);
  bytes[0x85] = static_cast<char>(machine >> 8);
  const unsigned magic = pe64 ? 0x020b : 0x010b;
  bytes[0x98] = static_cast<char>(magic & 0xff);
  bytes[0x99] = static_cast<char>(magic >> 8);
  return bytes;
}

std::string Quote(const std::string& text) { return "\"" + text + "\""; }

struct Fixture {
  fs::path root = fs::canonical(fs::temp_directory_path()) /
                  ("playden-kit-manifest-" + std::to_string(::getpid()));
  const std::string wrapper = std::string(40, 'a');
  const std::string wine = std::string(40, 'b');
  const std::string helper_revision = std::string(40, 'c');
  const std::string helper_base = std::string(40, 'd');
  const std::string foundation = std::string(40, 'e');
  const std::string dxvk_archive = std::string(64, 'f');
  KitManifestTrustedConfig trust;

  Fixture() {
    fs::remove_all(root);
    const std::string runtime = "runtime/wine-wow64/artifact-e16";
    const char* core[] = {"ntdll.dll", "kernel32.dll", "kernelbase.dll",
                          "mmdevapi.dll", "dinput8.dll", "rsaenh.dll"};
    std::ostringstream inventory;
    inventory << '{';
    bool first = true;
    auto runtime_file = [&](const std::string& relative, const std::string& bytes,
                            unsigned mode = 0644) {
      Write(root / runtime / relative, bytes, mode);
      if (!first) inventory << ',';
      first = false;
      inventory << Quote(relative) << ':' << Quote(*HashFile(root / runtime / relative));
    };
    for (const char* name : core) {
      runtime_file("lib/wine/i386-windows/" + std::string(name), Pe(false));
      runtime_file("lib/wine/x86_64-windows/" + std::string(name), Pe(true));
    }
    runtime_file("lib/wine/x86_64-windows/wowprospero.dll", Pe(true));
    for (const char* name : {"ntdll.prx", "wineserver.prx", "win32u.prx",
                             "wineps5.prx", "libvulkan.prx", "wowprospero.prx"})
      runtime_file("lib/wine/x86_64-unix/" + std::string(name), "prx-" + std::string(name), 0755);
    inventory << '}';
    Write(root / runtime / "runtime-inventory.json", inventory.str());

    const fs::path bottle = root / "template/bottle";
    for (const char* name : core) {
      Write(bottle / "drive_c/windows/system32" / name, Pe(true));
      Write(bottle / "drive_c/windows/syswow64" / name, Pe(false));
    }
    Write(bottle / "drive_c/windows/system32/wowprospero.dll", Pe(true));
    const char* dxvk[] = {"d3d8.dll", "d3d9.dll", "d3d10core.dll", "d3d11.dll", "dxgi.dll"};
    for (const char* name : dxvk) {
      Write(bottle / "drive_c/windows/system32" / name, Pe(true));
      Write(bottle / "drive_c/windows/syswow64" / name, Pe(false));
    }
    fs::create_directories(bottle / "drive_c/users/prospero/Desktop");
    fs::create_directories(bottle / "drive_c/users/prospero/Documents");
    Write(bottle / "dosdevices/.pw-symlinks", "c:\t../drive_c\nz:\t/\n");
    WriteRegistry(true);
    Write(root / "recipes/steam-220-hl2-v1.json",
          "{\"id\":\"steam-220-hl2-v1\",\"version\":\"1\",\"requirements\":{\"audio\":\"ps5\"}}");
    Write(root / "titles/variant/eboot.bin", "eboot", 0755);
    Write(root / "titles/variant/lapy.elf", "helper", 0755);
    Write(root / "titles/variant/lapy_elevation_protocol.h", "protocol");
    Write(root / "titles/variant/sce_sys/param.json", "{\"titleId\":\"FAKE78001\"}");
    RefreshInnerManifests();

    trust.current_firmware = "12.02";
    trust.required_architecture = "x86_64+wow64-i386";
    trust.allowed_capabilities = {"wine-wow64", "ps5-audio"};
    trust.sources = {{"wrapper", {{"revision", wrapper}, {"baseline_revision", wrapper}}},
                     {"wine", {{"revision", wine}}},
                     {"helper", {{"revision", helper_revision}, {"base_revision", helper_base}}},
                     {"foundation", {{"revision", foundation}}},
                     {"sdk", {{"version", "0.42"}}},
                     {"dxvk", {{"version", "2.6.2"}, {"archive_sha256", dxvk_archive}}}};
    trust.title_pins["FAKE78001"] = {*HashFile(root / "titles/variant/lapy.elf"),
                                      *HashFile(root / "titles/variant/lapy_elevation_protocol.h")};
    trust.hash_file = HashFile;
    trust.hash_bytes = [](std::string_view bytes) -> std::optional<std::string> {
      return HashBytes(bytes);
    };
    RewriteManifest();
  }

  ~Fixture() { fs::remove_all(root); }

  void WriteRegistry(bool valid) {
    const std::string crypto64 =
        "[Software\\\\Microsoft\\\\Cryptography\\\\Defaults\\\\Provider Types\\\\Type 001]\n\"Name\"=\"RSA Full\"\n"
        "[Software\\\\Microsoft\\\\Cryptography\\\\Defaults\\\\Provider\\\\RSA Full]\n\"Image Path\"=\"C:\\\\windows\\\\system32\\\\rsaenh.dll\"\n\"Type\"=dword:00000001\n";
    const std::string crypto32 =
        "[Software\\\\Wow6432Node\\\\Microsoft\\\\Cryptography\\\\Defaults\\\\Provider Types\\\\Type 001]\n\"Name\"=\"RSA Full\"\n"
        "[Software\\\\Wow6432Node\\\\Microsoft\\\\Cryptography\\\\Defaults\\\\Provider\\\\RSA Full]\n\"Image Path\"=\"C:\\\\windows\\\\system32\\\\rsaenh.dll\"\n\"Type\"=dword:00000001\n";
    const std::string com =
        "[Software\\\\Classes\\\\Wow6432Node\\\\CLSID\\\\{BCDE0395-E52F-467C-8E3D-C4579291692E}]\n"
        "[Software\\\\Classes\\\\Wow6432Node\\\\CLSID\\\\{BCDE0395-E52F-467C-8E3D-C4579291692E}\\\\InprocServer32]\n@=\"C:\\\\windows\\\\system32\\\\mmdevapi.dll\"\n\"ThreadingModel\"=\"Both\"\n"
        "[Software\\\\Classes\\\\Wow6432Node\\\\CLSID\\\\{25E609E4-B259-11CF-BFC7-444553540000}]\n"
        "[Software\\\\Classes\\\\Wow6432Node\\\\CLSID\\\\{25E609E4-B259-11CF-BFC7-444553540000}\\\\InprocServer32]\n@=\"C:\\\\windows\\\\system32\\\\dinput8.dll\"\n\"ThreadingModel\"=\"Both\"\n";
    Write(root / "template/bottle/system.reg",
          std::string("[Software\\\\Microsoft\\\\Wow64\\\\x86]\n@=\"") +
              (valid ? "wowprospero.dll" : "wrong.dll") + "\"\n" + crypto64 + crypto32 + com);
    Write(root / "template/bottle/user.reg",
          "[Software\\\\Wine\\\\Drivers]\n\"Audio\"=\"ps5\"\n");
  }

  void RefreshInnerManifests() {
    const auto helper = *HashFile(root / "titles/variant/lapy.elf");
    const auto protocol = *HashFile(root / "titles/variant/lapy_elevation_protocol.h");
    Write(root / "titles/variant/lapy-manifest.json",
          "{\"schema\":\"lapy-owned-build/1\",\"mode\":\"elf-helper\",\"target_title\":\"FAKE78001\","
          "\"elf_sha256\":" + Quote(helper) + ",\"protocol_sha256\":" + Quote(protocol) +
          ",\"features\":[\"root_layout_probe_retry\"]}");
    Write(root / "titles/variant/native-title-build.json",
          "{\"schema\":\"prospero-win-native-title/1\",\"title_id\":\"FAKE78001\","
          "\"source_revision\":" + Quote(wrapper) + ",\"source_dirty\":false,\"eboot_sha256\":" +
          Quote(*HashFile(root / "titles/variant/eboot.bin")) + "}");
  }

  void RewriteRuntimeInventory() {
    const fs::path runtime = root / "runtime/wine-wow64/artifact-e16";
    std::ostringstream out; out << '{'; bool first = true;
    for (const auto& item : fs::recursive_directory_iterator(runtime)) {
      if (!item.is_regular_file() || item.path().filename() == "runtime-inventory.json") continue;
      if (!first) out << ','; first = false;
      out << Quote(fs::relative(item.path(), runtime).generic_string()) << ':' << Quote(*HashFile(item.path()));
    }
    out << '}'; Write(runtime / "runtime-inventory.json", out.str());
  }

  void RewriteManifest(std::string firmware = "12.02", std::string runtime_path =
                       "runtime/wine-wow64/artifact-e16") {
    std::vector<KitFile> entries;
    for (const auto& item : fs::recursive_directory_iterator(root)) {
      if (!item.is_regular_file() || item.path().filename() == "manifest.json") continue;
      const auto status = fs::status(item.path());
      entries.push_back({fs::relative(item.path(), root).generic_string(), item.file_size(),
                         *HashFile(item.path()), static_cast<unsigned>(status.permissions()) & 0777});
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    std::ostringstream lines, files;
    files << '[';
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      std::ostringstream mode; mode << std::oct << std::setw(4) << std::setfill('0') << e.mode;
      lines << e.path << '\t' << e.size << '\t' << e.sha256 << '\t' << mode.str() << '\n';
      if (i) files << ',';
      files << "{\"path\":" << Quote(e.path) << ",\"size\":" << e.size
            << ",\"sha256\":" << Quote(e.sha256) << ",\"mode\":" << Quote(mode.str()) << '}';
    }
    files << ']';
    auto source = [&](const char* name, const std::string& body) {
      return Quote(name) + ":{" + body + ",\"url\":\"https://example.invalid/source\"}";
    };
    std::ostringstream dxvk;
    dxvk << "{\"version\":\"2.6.2\",\"files\":{";
    for (const auto& pair : {std::pair{"x64", "system32"}, std::pair{"x32", "syswow64"}}) {
      if (pair.first == std::string("x32")) dxvk << ',';
      dxvk << Quote(pair.first) << ":{";
      bool first = true;
      for (const char* name : {"d3d8.dll", "d3d9.dll", "d3d10core.dll", "d3d11.dll", "dxgi.dll"}) {
        if (!first) dxvk << ','; first = false;
        dxvk << Quote(name) << ':' << Quote(*HashFile(root / "template/bottle/drive_c/windows" / pair.second / name));
      }
      dxvk << '}';
    }
    dxvk << "}}";
    const std::string sources = "{" +
      source("wrapper", "\"revision\":" + Quote(wrapper) + ",\"baseline_revision\":" + Quote(wrapper)) + "," +
      source("wine", "\"revision\":" + Quote(wine)) + "," +
      source("helper", "\"revision\":" + Quote(helper_revision) + ",\"base_revision\":" + Quote(helper_base)) + "," +
      source("foundation", "\"revision\":" + Quote(foundation)) + "," +
      source("sdk", "\"version\":\"0.42\"") + "," +
      source("dxvk", "\"version\":\"2.6.2\",\"archive_sha256\":" + Quote(dxvk_archive)) + "}";
    const std::string manifest =
      "{\"schema\":\"playden-prospero-kit/1\",\"kit_id\":\"kit-1\",\"firmware\":[" + Quote(firmware) +
      "],\"architecture\":\"x86_64+wow64-i386\",\"capabilities\":[\"ps5-audio\",\"wine-wow64\"],"
      "\"sources\":" + sources + ",\"runtime\":{\"bundle_id\":\"wine-wow64\",\"artifact_revision\":\"artifact-e16\","
      "\"wine_revision\":" + Quote(wine) + ",\"path\":" + Quote(runtime_path) +
      ",\"inventory_manifest\":\"runtime/wine-wow64/artifact-e16/runtime-inventory.json\","
      "\"module_counts\":{\"i386-windows\":6,\"x86_64-windows\":7,\"x86_64-unix\":6}},"
      "\"template\":{\"path\":\"template/bottle\",\"user\":\"prospero\"},"
      "\"recipe\":{\"id\":\"steam-220-hl2-v1\",\"version\":\"1\",\"path\":\"recipes/steam-220-hl2-v1.json\",\"sha256\":" +
      Quote(*HashFile(root / "recipes/steam-220-hl2-v1.json")) + "},\"variants\":[{\"id\":\"variant\",\"title_id\":\"FAKE78001\","
      "\"path\":\"titles/variant\",\"wrapper_revision\":" + Quote(wrapper) + ",\"helper_revision\":" + Quote(helper_revision) +
      ",\"helper_manifest\":\"titles/variant/lapy-manifest.json\",\"helper_protocol\":\"titles/variant/lapy_elevation_protocol.h\","
      "\"native_build_manifest\":\"titles/variant/native-title-build.json\"}],\"dxvk\":" + dxvk.str() +
      ",\"files\":" + files.str() + ",\"files_sha256\":" + Quote(HashBytes(lines.str())) + "}";
    Write(root / "manifest.json", manifest);
    trust.manifest_sha256 = *HashFile(root / "manifest.json");
  }

  KitManifestResult Verify() const {
#if PLAYDEN_TEST_KIT_ADMISSION
    return VerifyInstalledKit(root, trust);
#else
    return VerifyKitManifest(root, trust);
#endif
  }
};

void CheckRejected(const KitManifestResult& result, KitManifestStatus status) {
  assert(!result.view);
  assert(result.status == status);
  assert(!result.reason.empty());
}

}  // namespace

int main() {
  {
    Fixture fixture;
    const auto result = fixture.Verify();
    if (result.status != KitManifestStatus::kVerified)
      std::fprintf(stderr, "valid fixture rejected: %s\n", result.reason.c_str());
    assert(result.status == KitManifestStatus::kVerified);
    assert(result.view && result.view->kit_id == "kit-1");
    assert(result.available_variant_ids == std::vector<std::string>{"variant"});
    assert(result.capabilities == std::vector<std::string>({"ps5-audio", "wine-wow64"}));
    assert(result.artifacts.size() >= 2);
  }
  {
    Fixture fixture; fixture.trust.sources["wine"]["revision"] = std::string(40, '9');
    CheckRejected(fixture.Verify(), KitManifestStatus::kUnsupportedSource);
  }
  {
    Fixture fixture; Write(fixture.root / "unexpected.bin", "extra");
    CheckRejected(fixture.Verify(), KitManifestStatus::kIntegrityFailure);
  }
  {
    Fixture fixture;
    Write(fixture.root / "runtime/wine-wow64/artifact-e16/lib/wine/i386-windows/ntdll.dll", Pe(true));
    fixture.RewriteRuntimeInventory(); fixture.RewriteManifest();
    CheckRejected(fixture.Verify(), KitManifestStatus::kIntegrityFailure);
  }
  {
    Fixture fixture;
    Write(fixture.root / "titles/variant/lapy.elf", "attacker helper", 0755);
    fixture.RefreshInnerManifests(); fixture.RewriteManifest();
    CheckRejected(fixture.Verify(), KitManifestStatus::kIntegrityFailure);
  }
  {
    Fixture fixture; fixture.RewriteManifest("99.99");
    CheckRejected(fixture.Verify(), KitManifestStatus::kUnsupportedFirmware);
  }
  {
    Fixture fixture; fixture.RewriteManifest("12.02", "../escape");
    CheckRejected(fixture.Verify(), KitManifestStatus::kMalformed);
  }
  {
    Fixture fixture;
    fs::create_symlink("recipes/steam-220-hl2-v1.json", fixture.root / "alias");
    CheckRejected(fixture.Verify(), KitManifestStatus::kIntegrityFailure);
  }
  {
    Fixture fixture; fixture.WriteRegistry(false); fixture.RewriteManifest();
    CheckRejected(fixture.Verify(), KitManifestStatus::kIntegrityFailure);
  }
#if PLAYDEN_TEST_KIT_ADMISSION
  {
    Fixture fixture;
    // Admission must bind real SHA-256 rather than trusting caller callbacks.
    fixture.trust.hash_file = [](const fs::path&) -> std::optional<std::string> {
      return std::nullopt;
    };
    fixture.trust.hash_bytes = [](std::string_view) -> std::optional<std::string> {
      return std::string(64, '0');
    };
    assert(fixture.Verify().status == KitManifestStatus::kVerified);
    Write(fixture.root / "titles/variant/eboot.bin", "changed", 0755);
    CheckRejected(fixture.Verify(), KitManifestStatus::kIntegrityFailure);
  }
  {
    Fixture fixture;
    // Same-length mutation catches substitution independently of file size.
    Write(fixture.root / "titles/variant/eboot.bin", "other", 0755);
    CheckRejected(fixture.Verify(), KitManifestStatus::kIntegrityFailure);
  }
  {
    Fixture fixture;
    const auto ancestor = fixture.root.parent_path() / (fixture.root.filename().string() + "-alias");
    fs::create_directory_symlink(fixture.root.parent_path(), ancestor);
    const auto result = VerifyInstalledKit(ancestor / fixture.root.filename(), fixture.trust);
    fs::remove(ancestor);
    CheckRejected(result, KitManifestStatus::kIntegrityFailure);
  }
#endif
  std::puts("runtime_kit_manifest_test: all assertions passed");
}
