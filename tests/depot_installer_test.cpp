#include "steam/backend/depot_chunk.h"
#include "steam/backend/depot_installer.h"
#include "steam/backend/steam_install_runner.h"

#include <mbedtls/aes.h>
#include <mbedtls/sha1.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <unistd.h>

using namespace playden;
using namespace playden::steam;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

namespace {

const std::array<std::uint8_t, 32> kKey = [] {
  std::array<std::uint8_t, 32> key{};
  for (std::size_t index = 0; index < key.size(); ++index) key[index] = static_cast<std::uint8_t>(index * 7 + 1);
  return key;
}();

void Le16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void Le32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  Le16(out, value & 0xffff);
  Le16(out, value >> 16);
}

// A one-entry stored zip, as Steam's "PK" chunk containers.
std::vector<std::uint8_t> StoredZip(const std::vector<std::uint8_t>& data) {
  std::vector<std::uint8_t> zip;
  Le32(zip, 0x04034b50); Le16(zip, 20); Le16(zip, 0); Le16(zip, 0); Le16(zip, 0); Le16(zip, 0);
  Le32(zip, 0); Le32(zip, static_cast<std::uint32_t>(data.size())); Le32(zip, static_cast<std::uint32_t>(data.size()));
  Le16(zip, 1); Le16(zip, 0); zip.push_back('z');
  zip.insert(zip.end(), data.begin(), data.end());
  const auto central = static_cast<std::uint32_t>(zip.size());
  Le32(zip, 0x02014b50); Le16(zip, 20); Le16(zip, 20); Le16(zip, 0); Le16(zip, 0); Le16(zip, 0); Le16(zip, 0);
  Le32(zip, 0); Le32(zip, static_cast<std::uint32_t>(data.size())); Le32(zip, static_cast<std::uint32_t>(data.size()));
  Le16(zip, 1); Le16(zip, 0); Le16(zip, 0); Le16(zip, 0); Le16(zip, 0); Le32(zip, 0); Le32(zip, 0); zip.push_back('z');
  const auto central_size = static_cast<std::uint32_t>(zip.size()) - central;
  Le32(zip, 0x06054b50); Le16(zip, 0); Le16(zip, 0); Le16(zip, 1); Le16(zip, 1);
  Le32(zip, central_size); Le32(zip, central); Le16(zip, 0);
  return zip;
}

// Steam symmetric encryption: AES-256-ECB(IV) followed by AES-256-CBC(IV) with PKCS7.
std::vector<std::uint8_t> Encrypt(std::vector<std::uint8_t> plain) {
  std::array<std::uint8_t, 16> iv{};
  for (std::size_t index = 0; index < iv.size(); ++index) iv[index] = static_cast<std::uint8_t>(0xa0 + index);
  const auto pad = static_cast<std::uint8_t>(16 - plain.size() % 16);
  plain.insert(plain.end(), pad, pad);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, kKey.data(), 256);
  std::vector<std::uint8_t> out(16 + plain.size());
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, iv.data(), out.data());
  auto chain = iv;
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, plain.size(), chain.data(), plain.data(), out.data() + 16);
  mbedtls_aes_free(&aes);
  return out;
}

std::array<std::uint8_t, 20> Sha1(const std::vector<std::uint8_t>& data) {
  std::array<std::uint8_t, 20> digest{};
  mbedtls_sha1(data.data(), data.size(), digest.data());
  return digest;
}

std::string HexOf(const std::array<std::uint8_t, 20>& bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text;
  for (const auto byte : bytes) { text.push_back(kDigits[byte >> 4]); text.push_back(kDigits[byte & 15]); }
  return text;
}

struct World {
  std::map<std::string, std::vector<std::uint8_t>> served;  // sha hex -> encrypted container
  std::map<std::uint32_t, DepotManifest> manifests;
  int chunk_calls = 0;
  int cancel_after = -1;
  bool corrupt = false;
  int busy_calls = 0;  // the first chunk requests answer as a busy CDN would
  std::uint64_t free_bytes = 1ull << 40;
  std::atomic<bool>* cancel = nullptr;

  ManifestChunk AddChunk(const std::vector<std::uint8_t>& plain, std::uint64_t offset) {
    ManifestChunk chunk;
    chunk.sha = Sha1(plain);
    chunk.offset = offset;
    chunk.original_size = static_cast<std::uint32_t>(plain.size());
    const auto container = Encrypt(StoredZip(plain));
    chunk.compressed_size = static_cast<std::uint32_t>(container.size());
    served[HexOf(chunk.sha)] = container;
    return chunk;
  }
  ManifestFile AddFile(const std::string& path, const std::vector<std::string>& pieces) {
    ManifestFile file;
    file.path = path;
    std::uint64_t offset = 0;
    for (const auto& piece : pieces) {
      std::vector<std::uint8_t> plain(piece.begin(), piece.end());
      file.chunks.push_back(AddChunk(plain, offset));
      offset += plain.size();
    }
    file.size = offset;
    return file;
  }

  InstallerHooks Hooks() {
    InstallerHooks hooks;
    hooks.access = [this](const StoredSession&, std::uint32_t, const std::vector<DepotRequest>& depots,
                          const std::atomic<bool>&) {
      DepotAccessResult result;
      result.error = CmError::kNone;
      for (const auto& depot : depots) result.grants.push_back({depot.depot_id, depot.manifest_gid, kKey, 77});
      return result;
    };
    hooks.servers = [](std::uint32_t, const std::atomic<bool>&, std::vector<ContentServer>* out) {
      *out = {{"a.example.invalid", "a.example.invalid", 1}, {"b.example.invalid", "b.example.invalid", 2}};
      return CdnError::kNone;
    };
    hooks.manifest = [this](const ContentServer&, const DepotGrant& grant, const std::atomic<bool>&, DepotManifest* out) {
      *out = manifests.at(grant.depot_id);
      return InstallError::kNone;
    };
    hooks.chunk = [this](const ContentServer&, std::uint32_t, const std::string& sha, std::size_t,
                         const std::atomic<bool>&, std::vector<std::uint8_t>* out) {
      ++chunk_calls;
      if (cancel_after >= 0 && chunk_calls > cancel_after && cancel) { cancel->store(true); return CdnError::kCanceled; }
      if (busy_calls > 0) { --busy_calls; return CdnError::kNetwork; }
      *out = served.at(sha);
      if (corrupt) out->back() ^= 0xff;
      return CdnError::kNone;
    };
    hooks.free_bytes = [this](const std::string&) { return free_bytes; };
    return hooks;
  }
};

std::string Read(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

InstallTarget Target(const std::filesystem::path& root) {
  InstallTarget target;
  target.app_id = 440;
  target.install_root = root.string();
  DepotInfo base;
  base.id = 441;
  base.manifest_gid = 11;
  DepotInfo patch;
  patch.id = 442;
  patch.manifest_gid = 12;
  target.depots = {base, patch};
  return target;
}

World MakeWorld() {
  World world;
  DepotManifest base;
  base.depot_id = 441;
  base.gid = 11;
  base.files.push_back(world.AddFile("game.exe", {"MZ-executable-", "second-chunk"}));
  ManifestFile folder;
  folder.path = "data";
  folder.flags = kDepotFileFlagDirectory;
  base.files.push_back(folder);
  base.files.push_back(world.AddFile("data/level.dat", {"level-one"}));
  base.files.push_back(world.AddFile("empty.txt", {}));
  DepotManifest patch;
  patch.depot_id = 442;
  patch.gid = 12;
  patch.files.push_back(world.AddFile("DATA/level.dat", {"level-patched"}));  // later depot wins, any case
  world.manifests[441] = base;
  world.manifests[442] = patch;
  return world;
}

void CountsActualUsefulCompressedBytes(const std::filesystem::path& root) {
  auto world = MakeWorld();
  // HTTP permits the advertised size plus padding; a verified body can differ from metadata.
  for (auto& [id, manifest] : world.manifests)
    for (auto& file : manifest.files)
      for (auto& chunk : file.chunks) chunk.compressed_size += 16;
  auto hooks = world.Hooks();
  const auto fetch = hooks.chunk;
  std::uint64_t received = 0;
  hooks.chunk = [&](const ContentServer& server, std::uint32_t depot, const std::string& sha,
                    std::size_t cap, const std::atomic<bool>& cancel, std::vector<std::uint8_t>* out) {
    const auto result = fetch(server, depot, sha, cap, cancel, out);
    if (result == CdnError::kNone) received += out->size();
    return result;
  };
  InstallStats last;
  hooks.stats = [&](const char* phase, const InstallStats& stats) { if (std::string(phase) == "end") last = stats; };
  DepotInstaller installer(hooks);
  std::atomic<bool> cancel{false};
  const auto outcome = installer.Run({}, Target(root), cancel);
  CHECK(outcome.error == InstallError::kNone);
  CHECK(last.compressed_bytes == received);
  CHECK(last.compressed_bytes + 3 * 16 == outcome.progress.download_total);
}

void StorageFailureEndsStats(const std::filesystem::path& root) {
  auto world = MakeWorld();
  std::filesystem::create_directories(root);
  std::ofstream(root / "DATA") << "not a directory";
  auto hooks = world.Hooks();
  int ends = 0;
  hooks.stats = [&](const char* phase, const InstallStats&) { if (std::string(phase) == "end") ++ends; };
  DepotInstaller installer(hooks);
  std::atomic<bool> cancel{false};
  CHECK(installer.Run({}, Target(root), cancel).error == InstallError::kStorage);
  CHECK(ends == 1);
}

void StatsAndRemovalLogs(const std::filesystem::path& root) {
  InstallStats stats;
  stats.download_ms = 300000; stats.compressed_bytes = 6000000000ull;
  stats.files_total = 48000; stats.files_created = 48000;
  const auto line = FormatInstallStats("end", stats);
  CHECK(line.size() < 480);
  CHECK(line.find("dl_ms=300000") != std::string::npos && line.find("reuse=0") != std::string::npos);
  CmClient cm(CmClientHooks{}, cm::DeviceIdentity{});
  CdnClient cdn([](const HttpRequest&, const std::atomic<bool>&) { return HttpResponse{}; });
  SteamInstallRunner runner(cm, cdn);
  int removals = 0;
  GameId expected_app = 440;
  const char* expected_ok = "ok=1";
  runner.SetLog([&](GameId app, const char* step, const char* result, const char* detail) {
    CHECK(app == expected_app && std::string(step) == "install-removal" && std::string(result) == "done");
    CHECK(std::string(detail) == expected_ok);
    ++removals;
  });
  CHECK(runner.Remove((root / "440").string()));
  CHECK(removals == 1);
  expected_app = 441; expected_ok = "ok=0";
  std::filesystem::create_directories(root / "441");
  std::ofstream(root / "441" / "keep.txt") << "unmanaged";
  CHECK(!runner.Remove((root / "441").string()));
  CHECK(removals == 2);
  CHECK(!runner.Remove((root / "account-name").string() + "/.."));
  CHECK(removals == 2); // no unvalidated path/name is logged
}

void RunStats(const std::filesystem::path& base) {
  for (int mode = 0; mode < 3; ++mode) {
    auto world = MakeWorld();
    world.manifests[441].files.pop_back(); // two files and three merged chunks
    world.manifests[442].files[0].path = "data/level.dat"; // stable folder count on either filesystem
    std::atomic<bool> cancel{false};
    world.cancel = &cancel;
    if (mode == 1) world.cancel_after = 1;
    if (mode == 2) world.corrupt = true;
    auto hooks = world.Hooks();
    InstallStats last;
    int ends = 0, shapes = 0;
    hooks.stats = [&](const char* phase, const InstallStats& stats) {
      if (std::string(phase) == "end") { ++ends; last = stats; }
      if (std::string(phase) == "shape") {
        ++shapes;
        if (ends == 0) CHECK(stats.chunks_pending == 3 && stats.files_pending == 2 && stats.files_total == 2);
      }
    };
    CdnStats totals;
    totals.requests = 100;
    hooks.cdn_stats = [&] { return totals; };
    auto fetch = hooks.chunk;
    hooks.chunk = [&](const ContentServer& server, std::uint32_t depot, const std::string& sha,
                      std::size_t cap, const std::atomic<bool>& canceled, std::vector<std::uint8_t>* out) {
      ++totals.requests;
      if (mode == 0) usleep(600);
      return fetch(server, depot, sha, cap, canceled, out);
    };
    DepotInstaller installer(hooks);
    auto target = Target(base / std::to_string(mode));
    const auto outcome = installer.Run({}, target, cancel);
    CHECK(ends == 1 && shapes == 1 && last.requests == static_cast<std::uint64_t>(world.chunk_calls));
    CHECK(last.slots == 1 && last.reuse == 0);
    if (mode == 0) {
      CHECK(outcome.error == InstallError::kNone && last.chunks_done == 3 && last.files_created == 2);
      CHECK(last.fetch_ms >= 1); // sub-millisecond chunks accumulate before conversion
      CHECK(last.original_bytes == 39 && last.folders_created == 1);
      CHECK(last.compressed_bytes == outcome.progress.download_total);
    } else if (mode == 1) {
      CHECK(outcome.error == InstallError::kCanceled && last.chunks_done == 1);
      CHECK(last.original_bytes > 0 && last.compressed_bytes > 0);
      cancel = false; world.cancel_after = -1;
      installer.Run({}, target, cancel);
      CHECK(ends == 2 && last.chunks_skipped == 1 && last.chunks_done == 2);
      CHECK(last.files_created <= 1 && last.requests == static_cast<std::uint64_t>(world.chunk_calls - 2));
    } else {
      CHECK(outcome.error == InstallError::kCorrupt && last.chunks_done == 0);
      CHECK(last.compressed_bytes == 0 && last.original_bytes == 0 && last.corrupt_retries == 5);
    }
  }
}

void InstallsMergedDepotsAndWritesRecord(const std::filesystem::path& root) {
  auto world = MakeWorld();
  DepotInstaller installer(world.Hooks());
  std::atomic<bool> cancel{false};
  InstallProgress last;
  const auto outcome = installer.Run({}, Target(root), cancel, [&](const InstallProgress& progress) { last = progress; });
  CHECK(outcome.error == InstallError::kNone);
  CHECK(Read(root / "game.exe") == "MZ-executable-second-chunk");
  CHECK(Read(root / "data/level.dat") == "level-patched");
  CHECK(std::filesystem::exists(root / "empty.txt") && std::filesystem::file_size(root / "empty.txt") == 0);
  CHECK(std::filesystem::exists(root / DepotInstaller::kInstallRecord));
  CHECK(!std::filesystem::exists(root / DepotInstaller::kJournalDir));
  CHECK(Read(root / DepotInstaller::kFileList).find("f\tdata/level.dat") != std::string::npos ||
        Read(root / DepotInstaller::kFileList).find("f\tDATA/level.dat") != std::string::npos);
  CHECK(last.download_done == last.download_total && last.files_total == 3);
  CHECK(world.chunk_calls == 3);  // the shadowed base level.dat is never fetched
  CHECK(Read(root / DepotInstaller::kInstallRecord).find("\"app_id\":440") != std::string::npos);
}

void ResumesWithoutRefetchingFinishedChunks(const std::filesystem::path& root) {
  auto world = MakeWorld();
  std::atomic<bool> cancel{false};
  world.cancel = &cancel;
  world.cancel_after = 2;
  DepotInstaller installer(world.Hooks());
  const auto first = installer.Run({}, Target(root), cancel);
  CHECK(first.error == InstallError::kCanceled);
  CHECK(std::filesystem::exists(root / DepotInstaller::kJournalDir));
  CHECK(!std::filesystem::exists(root / DepotInstaller::kInstallRecord));
  cancel = false;
  world.cancel_after = -1;
  world.chunk_calls = 0;
  const auto second = installer.Run({}, Target(root), cancel);
  CHECK(second.error == InstallError::kNone);
  CHECK(world.chunk_calls == 1);  // only the chunk interrupted by the cancel
  CHECK(Read(root / "game.exe") == "MZ-executable-second-chunk");
}

void RefusesWhenSpaceIsShort(const std::filesystem::path& root) {
  auto world = MakeWorld();
  world.free_bytes = 1024;
  DepotInstaller installer(world.Hooks());
  std::atomic<bool> cancel{false};
  CHECK(installer.Run({}, Target(root), cancel).error == InstallError::kNoSpace);
  CHECK(world.chunk_calls == 0);
}

void CorruptChunksFailAfterRetries(const std::filesystem::path& root) {
  auto world = MakeWorld();
  world.corrupt = true;
  DepotInstaller installer(world.Hooks());
  std::atomic<bool> cancel{false};
  CHECK(installer.Run({}, Target(root), cancel).error == InstallError::kCorrupt);
  CHECK(world.chunk_calls == DepotInstaller::kChunkAttempts * 2);
}

void BusyCdnIsRetriedAfterAWait(const std::filesystem::path& root) {
  auto world = MakeWorld();
  world.busy_calls = 3;
  auto hooks = world.Hooks();
  std::vector<int> waits;
  hooks.pause = [&](int milliseconds, const std::atomic<bool>&) { waits.push_back(milliseconds); };
  DepotInstaller installer(std::move(hooks));
  std::atomic<bool> cancel{false};
  CHECK(installer.Run({}, Target(root), cancel).error == InstallError::kNone);
  CHECK((waits == std::vector<int>{1000, 2000, 4000}));
}

void BrokenManifestIsFetchedAgainElsewhere(const std::filesystem::path& root) {
  // A busy CDN answers 502 or sends a cut-off manifest: the next server is asked after a wait.
  auto world = MakeWorld();
  auto hooks = world.Hooks();
  const auto good = hooks.manifest;
  int calls = 0;
  hooks.manifest = [&](const ContentServer& server, const DepotGrant& grant, const std::atomic<bool>& cancel,
                       DepotManifest* out) {
    ++calls;
    if (calls == 1) return InstallError::kNetwork;
    if (calls == 2) return InstallError::kCorrupt;
    return good(server, grant, cancel, out);
  };
  std::vector<int> waits;
  hooks.pause = [&](int milliseconds, const std::atomic<bool>&) { waits.push_back(milliseconds); };
  DepotInstaller installer(std::move(hooks));
  std::atomic<bool> cancel{false};
  CHECK(installer.Run({}, Target(root / "recovers"), cancel).error == InstallError::kNone);
  CHECK((waits == std::vector<int>{1000, 2000}));

  // Never sent by one server and never readable from another: a network problem, not corrupt content.
  auto failing = world.Hooks();
  int tries = 0;
  failing.manifest = [&](const ContentServer&, const DepotGrant&, const std::atomic<bool>&, DepotManifest*) {
    return ++tries % 2 ? InstallError::kNetwork : InstallError::kCorrupt;
  };
  failing.pause = [](int, const std::atomic<bool>&) {};
  DepotInstaller stuck(std::move(failing));
  CHECK(stuck.Run({}, Target(root / "stuck"), cancel).error == InstallError::kNetwork);
  CHECK(tries == 4);
}

void RemovesOnlyManagedFolders(const std::filesystem::path& base) {
  const auto unmanaged = base / "unmanaged";
  std::filesystem::create_directories(unmanaged);
  std::ofstream(unmanaged / "keep.txt") << "keep";
  CHECK(!RemoveManagedInstall(unmanaged.string()));
  CHECK(std::filesystem::exists(unmanaged / "keep.txt"));
  const auto managed = base / "managed";
  InstallsMergedDepotsAndWritesRecord(managed);
  CHECK(RemoveManagedInstall(managed.string()));
  CHECK(!std::filesystem::exists(managed));
  CHECK(!RemoveManagedInstall("relative/path"));
}

}  // namespace

int main() {
  const auto base = std::filesystem::temp_directory_path() / ("playden-installer-test-" + std::to_string(::getpid()));
  std::filesystem::remove_all(base);
  CountsActualUsefulCompressedBytes(base / "actual-compressed");
  StorageFailureEndsStats(base / "storage-stats");
  StatsAndRemovalLogs(base / "removal");
  RunStats(base / "stats");
  InstallsMergedDepotsAndWritesRecord(base / "install");
  ResumesWithoutRefetchingFinishedChunks(base / "resume");
  RefusesWhenSpaceIsShort(base / "space");
  CorruptChunksFailAfterRetries(base / "corrupt");
  BusyCdnIsRetriedAfterAWait(base / "busy");
  BrokenManifestIsFetchedAgainElsewhere(base / "manifest");
  RemovesOnlyManagedFolders(base);
  std::filesystem::remove_all(base);
  std::puts("depot installer tests passed");
}
