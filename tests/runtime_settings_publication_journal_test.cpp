#include "runtime/settings_publication_journal.h"
#include "runtime/artifact_hash.h"
#include "runtime/publication_contract.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <csignal>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace playden::runtime;
template<std::size_t N> void Copy(char (&target)[N], const std::string& value) {
  CHECK(value.size() < N); std::memcpy(target, value.c_str(), value.size() + 1);
}
void Refresh(SettingsPublicationOperation& o) {
  const auto& r = o.install; const auto& p = o.plan;
  o.descriptor_bytes = "{\"schema_version\":1,\"app_id\":220,\"settings_revision\":" + std::to_string(p.settings_revision) +
      ",\"title_id\":\"FAKE78001\",\"install_id\":\"install-220\",\"data_root\":\"/data/playden/games/install-220\","
      "\"default_profile_id\":\"playden-steam-220\",\"runtime_bundle_id\":\"wine-wow64\","
      "\"recipe_id\":\"steam-220-hl2-v1\",\"recipe_version\":\"1\",\"exit_policy\":\"exit-title\"}\n";
  o.catalog_bytes = p.profile_id + ".profile\n";
  PwTitlePublicationMetadata m{}; m.schema_version = 2;
  Copy(m.install_id, r.install_id); Copy(m.title_id, r.title_id); Copy(m.data_root, r.data_root);
  Copy(m.profile_id, p.profile_id); Copy(m.runtime_bundle_id, r.runtime_bundle_id);
  Copy(m.recipe_id, p.recipe_id); Copy(m.recipe_version, p.recipe_version); Copy(m.kit_id, o.kit_id);
  m.settings_revision = p.settings_revision;
  Copy(m.profile_sha256, *HashArtifactBytes(p.profile_bytes));
  Copy(m.descriptor_sha256, *HashArtifactBytes(o.descriptor_bytes));
  Copy(m.catalog_sha256, *HashArtifactBytes(o.catalog_bytes));
  m.input_count = p.input_presets.size();
  for (std::size_t i = 0; i < p.input_presets.size(); ++i) {
    Copy(m.inputs[i].name, p.input_presets[i].first); Copy(m.inputs[i].sha256, *HashArtifactBytes(p.input_presets[i].second));
  }
  Copy(m.dxvk_path, r.install_path + "/dxvk.conf"); m.dxvk_present = !p.dxvk_config.empty();
  if (m.dxvk_present) Copy(m.dxvk_sha256, *HashArtifactBytes(p.dxvk_config));
  m.registry_count = 2;
  Copy(m.registry[0].key, "Software\\Wine\\AppDefaults\\probe.exe"); Copy(m.registry[0].name, "Version");
  Copy(m.registry[1].key, "Control Panel\\Desktop"); Copy(m.registry[1].name, "LogPixels");
  m.registry[0].kind = m.registry[1].kind = PW_TITLE_PUBLICATION_REGISTRY_ABSENT;
  for (const auto& desired : p.registry) {
    if (desired.name == "Version" && !desired.data.empty()) {
      m.registry[0].kind = PW_TITLE_PUBLICATION_REGISTRY_STRING; Copy(m.registry[0].string_value, "win7");
    }
    if (desired.name == "LogPixels" && !desired.data.empty()) {
      m.registry[1].kind = PW_TITLE_PUBLICATION_REGISTRY_DWORD; m.registry[1].dword = 120;
    }
  }
  auto metadata = SerializePublicationMetadata(m); CHECK(metadata); o.metadata_bytes = *metadata;
}
SettingsPublicationOperation Fixture() {
  SettingsPublicationOperation o; o.transaction_id = "00000000-0000-7000-8000-000000000001"; o.kit_id = "kit-hl2";
  auto& r = o.install;
  r.install_id = "install-220"; r.title_id = "FAKE78001"; r.app_id = 220;
  r.data_root = "/data/playden/games/install-220"; r.default_profile_id = "playden-steam-220";
  r.runtime_bundle_id = "wine-wow64"; r.recipe_id = "steam-220-hl2-v1"; r.recipe_version = "1";
  r.settings_revision = 7; r.launch_entry_id = "main"; r.launch_path = "probe.exe";
  r.install_path = "/data/homebrew/FAKE78001/games/220"; r.volume_id = "internal";
  r.artifacts = {{"wine", "e16ec8f", std::string(64, 'a')}};
  r.last_failure = RuntimeFailure{"prior-failure", "prior message"};
  r.journal = PreparationJournal{"owned-operation", "prepare", "profile"};
  auto& p = o.plan;
  p.app_id = r.app_id; p.recipe_id = r.recipe_id; p.recipe_version = r.recipe_version;
  p.profile_id = r.default_profile_id; p.settings_revision = r.settings_revision;
  p.launch_entry_id = "main"; p.launch_entry = {"main", "Play", "probe.exe", ""};
  p.profile_bytes = "[application]\nid = playden-steam-220\nname = Probe\n"
      "executable = Z:\\data\\homebrew\\FAKE78001\\games\\220\\probe.exe\n"
      "working_directory = Z:\\data\\homebrew\\FAKE78001\\games\\220\n"
      "arguments = -steam\nprefix = game\nruntime = wine-wow64\narchitecture = pe64\ngraphics = dxvk\n"
      "[input]\npreset = gamepad\nmode = keyboard\n";
  p.input_presets = {{"gamepad.input", "[input]\nmode = keyboard\ncross = space\n"}};
  Refresh(o); return o;
}
int main() {
  std::string reason;
  CHECK(!FreezeSettingsPublicationOperation({}, &reason));
  CHECK(!DecodeSettingsPublicationOperation({}, &reason));
  CHECK(!DecodeSettingsPublicationOperation(playden::Bytes(kMaxSettingsPublicationOperationBytes + 1), &reason));
  auto source = Fixture(); auto frozen = FreezeSettingsPublicationOperation(source, &reason);
  if (!frozen) std::fprintf(stderr, "%s\n", reason.c_str()); CHECK(frozen);
  auto encoded = EncodeSettingsPublicationOperation(*frozen, &reason); CHECK(encoded);
  auto decoded = DecodeSettingsPublicationOperation(*encoded, &reason); CHECK(decoded);
  CHECK(EncodeSettingsPublicationOperation(*decoded, &reason) == encoded);
  auto publication = RebuildFrozenProfilePublication(*decoded, &reason); CHECK(publication);
  CHECK(publication->profile_references_validated && publication->profile_bytes == decoded->plan.profile_bytes);
  CHECK(publication->input_presets.size() == 1 && publication->input_presets[0].filename == "gamepad.input");
  CHECK(decoded->install.launch_arguments.empty() && decoded->plan.profile_bytes.find("arguments = -steam") != std::string::npos);
  CHECK(decoded->install.artifacts[0].identity == "e16ec8f" && decoded->install.last_failure->message == "prior message");
  CHECK(decoded->install.journal->step == "profile");
  auto changed = *frozen; ++changed.install.settings_revision;
  CHECK(!EncodeSettingsPublicationOperation(changed, &reason));
  changed = *frozen; changed.plan.profile_bytes += "\n";
  CHECK(!EncodeSettingsPublicationOperation(changed, &reason));
  CHECK(!RebuildFrozenProfilePublication(changed, &reason));
  changed = *frozen; changed.pending_receipt_bytes = changed.committed_receipt_bytes;
  CHECK(!EncodeSettingsPublicationOperation(changed, &reason));
  changed = *frozen; changed.plan_sha256[0] = 'z'; CHECK(!EncodeSettingsPublicationOperation(changed, &reason));
  changed = *frozen; changed.install.launch_arguments = "adoption-arguments";
  CHECK(!EncodeSettingsPublicationOperation(changed, &reason)); // Whole record is frozen, not only launch bytes.
  auto extra = *encoded; extra.push_back(0); CHECK(!DecodeSettingsPublicationOperation(extra, &reason));
  for (std::size_t i = 0; i < encoded->size(); ++i) {
    auto truncated = *encoded; truncated.resize(i); CHECK(!DecodeSettingsPublicationOperation(truncated, &reason));
  }
  for (std::size_t i = 0; i < encoded->size(); ++i) {
    auto mutated = *encoded; mutated[i] ^= 1;
    CHECK(!DecodeSettingsPublicationOperation(mutated, &reason));
  }
  auto oversized = *encoded; std::fill(oversized.begin(), oversized.begin() + 4, 255);
  CHECK(!DecodeSettingsPublicationOperation(oversized, &reason)); // Length checked before allocation.
  changed = source; changed.transaction_id = "00000000-0000-0000-0000-000000000000";
  CHECK(!FreezeSettingsPublicationOperation(changed, &reason));
  changed = source; changed.plan.input_presets[0].first = "Gamepad.input"; Refresh(changed);
  CHECK(!FreezeSettingsPublicationOperation(changed, &reason));
  changed = source; changed.plan.profile_bytes += "[unsupported]\nkey = value\n"; Refresh(changed);
  CHECK(!FreezeSettingsPublicationOperation(changed, &reason));
  changed = source; changed.plan.registry = {{"Software\\Wine\\AppDefaults\\probe.exe", "Version", "\"win7\""}};
  Refresh(changed); CHECK(!FreezeSettingsPublicationOperation(changed, &reason)); // Missing mandatory original backup.
  changed.registry_backup = RegistryBackupBinding{"registry-backup-" + changed.transaction_id + ".v1", 0644, 4, *HashArtifactBytes("old!")};
  CHECK(FreezeSettingsPublicationOperation(changed, &reason));
  changed.registry_backup->filename = "../other"; CHECK(!FreezeSettingsPublicationOperation(changed, &reason));
  changed = source; changed.install.settings_revision = changed.plan.settings_revision = std::numeric_limits<std::uint64_t>::max();
  changed.previous_revision = changed.plan.settings_revision - 1; Refresh(changed);
  CHECK(FreezeSettingsPublicationOperation(changed, &reason));
  changed = source; changed.dxvk_before = {true, 0644, 0, *HashArtifactBytes(""), ""};
  CHECK(!FreezeSettingsPublicationOperation(changed, &reason)); // An empty existing config has no ownership header.
  changed = source; changed.previous_revision = 8; CHECK(!FreezeSettingsPublicationOperation(changed, &reason));
  changed = source;
  const std::string header = "# Generated by Playden from this game's settings.\n#";
  changed.plan.dxvk_config = header + std::string(256 * 1024 - header.size(), 'x');
  changed.dxvk_before = {true, 0644, changed.plan.dxvk_config.size(), *HashArtifactBytes(changed.plan.dxvk_config), changed.plan.dxvk_config};
  for (unsigned i = 1; i < 64; ++i) changed.plan.input_presets.emplace_back("pad" + std::to_string(i) + ".input", "[input]\nmode = keyboard\n;");
  for (auto& input : changed.plan.input_presets) input.second.resize(8192, ' ');
  Refresh(changed); auto large = FreezeSettingsPublicationOperation(changed, &reason);
  if (!large) std::fprintf(stderr, "%s\n", reason.c_str()); CHECK(large);
  auto large_bytes = EncodeSettingsPublicationOperation(*large, &reason);
  CHECK(large_bytes && large_bytes->size() > 1024 * 1024 && large_bytes->size() <= kMaxSettingsPublicationOperationBytes);
  CHECK(DecodeSettingsPublicationOperation(*large_bytes, &reason));
  changed.plan.dxvk_config += "x"; CHECK(!FreezeSettingsPublicationOperation(changed, &reason));
  namespace fs = std::filesystem;
  char pattern[] = "/tmp/playden-settings-operation-XXXXXX"; CHECK(mkdtemp(pattern));
  const auto root = fs::canonical(pattern);
  CHECK(LoadSettingsPublicationOperation(root).state == PublicationRecoveryState::kAbsent);
  CHECK(CreateSettingsPublicationOperation(root, *frozen).ok());
  auto loaded = LoadSettingsPublicationOperation(root);
  CHECK(loaded.state == PublicationRecoveryState::kValid && loaded.operation);
  CHECK(EncodeSettingsPublicationOperation(*loaded.operation, &reason) == encoded);
  CHECK(!CreateSettingsPublicationOperation(root, *frozen).ok());
  changed = source; changed.install.launch_arguments = "adoption-only";
  auto refrozen = FreezeSettingsPublicationOperation(changed, &reason); CHECK(refrozen);
  CHECK(!CreateSettingsPublicationOperation(root, *refrozen).ok());
  loaded = LoadSettingsPublicationOperation(root);
  CHECK(loaded.state == PublicationRecoveryState::kValid && loaded.operation->install.launch_arguments.empty());
  CHECK(chmod((root / "settings-operation.v1").c_str(), 0644) == 0);
  CHECK(LoadSettingsPublicationOperation(root).state == PublicationRecoveryState::kCorrupt);
  CHECK(chmod((root / "settings-operation.v1").c_str(), 0600) == 0);
  fs::create_hard_link(root / "settings-operation.v1", root / "alias");
  CHECK(LoadSettingsPublicationOperation(root).state == PublicationRecoveryState::kCorrupt); fs::remove(root / "alias");
  std::ofstream(root / "settings-operation.v1", std::ios::binary) << "partial";
  CHECK(LoadSettingsPublicationOperation(root).state == PublicationRecoveryState::kCorrupt);
  CHECK(!CreateSettingsPublicationOperation(root, *frozen).ok());
  fs::remove(root / "settings-operation.v1");
  fs::create_symlink("missing", root / "settings-operation.v1");
  CHECK(LoadSettingsPublicationOperation(root).state == PublicationRecoveryState::kCorrupt); fs::remove(root / "settings-operation.v1");
  changed = source; changed.plan.registry = {{"Software\\Wine\\AppDefaults\\probe.exe", "Version", "\"win7\""}};
  Refresh(changed);
  auto backup = CreatePublicationRegistryBackup(root, changed.transaction_id, "old!", 0644); CHECK(backup.ok());
  changed.registry_backup = backup.value(); auto backed = FreezeSettingsPublicationOperation(changed, &reason); CHECK(backed);
  CHECK(CreateSettingsPublicationOperation(root, *backed).ok());
  CHECK(LoadSettingsPublicationOperation(root).state == PublicationRecoveryState::kValid);
  fs::remove(root / backed->registry_backup->filename);
  CHECK(LoadSettingsPublicationOperation(root).state == PublicationRecoveryState::kCorrupt);
  fs::remove(root / "settings-operation.v1");
  CHECK(!CreateSettingsPublicationOperation(root, *backed).ok());
  CHECK(!fs::exists(root / "settings-operation.v1"));
  CHECK(chmod(root.c_str(), 0755) == 0);
  CHECK(LoadSettingsPublicationOperation(root).state == PublicationRecoveryState::kUnavailable);
  CHECK(!CreateSettingsPublicationOperation(root, *frozen).ok());
  CHECK(chmod(root.c_str(), 0700) == 0);
  CHECK(!CreateSettingsPublicationOperation(root / "not-created", *frozen).ok());
  CHECK(!fs::exists(root / "not-created"));
  // A write failure leaves only retained private temporary evidence, never a
  // valid operation. Reconstructing the store observes absent, not Ready.
  const auto child = fork(); CHECK(child >= 0);
  if (!child) {
    struct rlimit limit{}; if (getrlimit(RLIMIT_FSIZE, &limit)) _exit(2);
    limit.rlim_cur = 16; if (setrlimit(RLIMIT_FSIZE, &limit)) _exit(3);
    std::signal(SIGXFSZ, SIG_IGN);
    _exit(CreateSettingsPublicationOperation(root, *frozen).ok() ? 4 : 0);
  }
  int status = 0;
  CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  CHECK(LoadSettingsPublicationOperation(root).state == PublicationRecoveryState::kAbsent);
  bool partial = false;
  for (const auto& entry : fs::directory_iterator(root)) {
    partial = true; struct stat info{};
    CHECK(lstat(entry.path().c_str(), &info) == 0 && S_ISREG(info.st_mode) && (info.st_mode & 07777) == 0600);
  }
  CHECK(partial);
  fs::remove_all(root);
  std::puts("settings publication operation tests passed");
}
