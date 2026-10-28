#include "app/preferences.h"
#include "platform/storage.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    char pattern[] = "/tmp/playden-preferences-XXXXXX";
    const char* created = mkdtemp(pattern);
    Check(created != nullptr, "creates a temporary test directory");
    if (created != nullptr) {
      path_ = created;
    }
  }

  ~TemporaryDirectory() {
    if (path_.empty()) {
      return;
    }
    std::remove((path_ + "/preferences.conf.tmp").c_str());
    std::remove((path_ + "/preferences.conf").c_str());
    rmdir(path_.c_str());
  }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

void StorageRejectsPathsOutsideItsRoot() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  const playden::Bytes data{'x'};

  const auto parent = storage.WriteAtomic("../outside", data);
  Check(!parent.ok(), "rejects parent traversal");
  Check(parent.error() == playden::StorageError::kInvalidPath,
        "reports traversal as an invalid path");

  const auto absolute = storage.Read("/etc/passwd");
  Check(!absolute.ok(), "rejects absolute paths");
  Check(absolute.error() == playden::StorageError::kInvalidPath,
        "reports absolute paths as invalid");

  const auto windows = storage.Read("folder\\..\\secret");
  Check(!windows.ok(), "rejects platform-ambiguous separators");
}

void StorageDistinguishesMissingFiles() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());

  const auto result = storage.Read("missing.bin");

  Check(!result.ok(), "missing read fails");
  Check(result.error() == playden::StorageError::kNotFound,
        "missing read has a distinct error");
  Check(result.message().find("missing.bin") != std::string::npos,
        "missing error identifies the file");
}

void StorageWritesAndReadsBytesAtomically() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  const playden::Bytes expected{0x00, 0x41, 0xff, 0x0a};

  const auto written = storage.WriteAtomic("preferences.conf", expected);
  Check(written.ok(), "atomic write succeeds");

  const auto read = storage.Read("preferences.conf");
  Check(read.ok(), "written file can be read");
  if (read.ok()) {
    Check(read.value() == expected, "read preserves every byte");
  }

  struct stat ignored {};
  Check(stat((directory.path() + "/preferences.conf.tmp").c_str(), &ignored) != 0,
        "atomic write leaves no temporary file");
}

void StorageRejectsOversizedReads() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  const playden::Bytes oversized(1024 * 1024 + 1, 0x7f);
  Check(storage.WriteAtomic("preferences.conf", oversized).ok(),
        "oversized read fixture is written");

  const auto read = storage.Read("preferences.conf");

  Check(!read.ok(), "read rejects files above its memory bound");
  Check(read.error() == playden::StorageError::kReadFailed,
        "oversized file is reported as a read failure");
  Check(read.message().find("1048576") != std::string::npos,
        "oversized read error reports the byte limit");
}

void MissingPreferencesUseDefaultsWithoutAnError() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  playden::PreferencesStore store(storage);

  const auto loaded = store.Load();

  Check(!loaded.preferences.reduce_motion, "reduce motion defaults to false");
  Check(loaded.status == playden::PreferencesLoadStatus::kDefaultMissing,
        "missing preferences are classified separately");
  Check(loaded.diagnostic.empty(), "first run does not show an error");
}

void PreferencesPersistAcrossStoreInstances() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  playden::PreferencesStore first_store(storage);
  playden::Preferences preferences;
  Check(first_store.Save(preferences).ok(), "initial preference save succeeds");
  preferences.reduce_motion = true;

  const auto saved = first_store.Save(preferences);
  Check(saved.ok(), "preference replacement succeeds");

  playden::PreferencesStore restarted_store(storage);
  const auto loaded = restarted_store.Load();
  Check(loaded.preferences.reduce_motion, "reduce motion survives restart");
  Check(loaded.status == playden::PreferencesLoadStatus::kLoaded,
        "valid preferences report a loaded status");
  Check(loaded.diagnostic.empty(), "valid preferences have no diagnostic");
}

void TitleStoreSuffixPersistsAndVersionOneStillLoads() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  playden::PreferencesStore store(storage);
  playden::Preferences preferences;
  preferences.title_store_suffix = true;
  Check(store.Save(preferences).ok(), "suffix preference saves");
  const auto loaded = store.Load();
  Check(loaded.status == playden::PreferencesLoadStatus::kLoaded && loaded.preferences.title_store_suffix &&
            !loaded.preferences.reduce_motion,
        "title store suffix survives restart");
  const std::string old_format = "version=1\nreduce_motion=1\n";
  Check(storage.WriteAtomic("preferences.conf", playden::Bytes(old_format.begin(), old_format.end())).ok(),
        "version 1 fixture is written");
  const auto legacy = store.Load();
  Check(legacy.status == playden::PreferencesLoadStatus::kLoaded && legacy.preferences.reduce_motion &&
            !legacy.preferences.title_store_suffix,
        "version 1 preferences still load, without the suffix");
  const std::string version_two = "version=2\nreduce_motion=0\ntitle_store_suffix=1\n";
  Check(storage.WriteAtomic("preferences.conf", playden::Bytes(version_two.begin(), version_two.end())).ok(),
        "version 2 fixture is written");
  const auto two = store.Load();
  Check(two.status == playden::PreferencesLoadStatus::kLoaded && two.preferences.title_store_suffix &&
            !two.preferences.cloud_saves_auto,
        "version 2 preferences load with automatic Cloud saves off (it is opt-in)");
  playden::Preferences manual;
  manual.cloud_saves_auto = false;
  Check(store.Save(manual).ok(), "manual Cloud saves preference saves");
  const auto off = store.Load();
  Check(off.status == playden::PreferencesLoadStatus::kLoaded && !off.preferences.cloud_saves_auto,
        "automatic Cloud saves off survives restart");
}

void SignInMethodPersistsAndVersionThreeOpensQr() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  playden::PreferencesStore store(storage);
  playden::Preferences preferences;
  preferences.steam_sign_in_password = true;
  preferences.cloud_saves_auto = true;
  Check(store.Save(preferences).ok(), "sign-in method preference saves");
  const auto saved = storage.Read("preferences.conf");
  Check(saved.ok() && std::string(saved.value().begin(), saved.value().end()) ==
                          "version=8\nreduce_motion=0\ntitle_store_suffix=0\ncloud_saves_auto=1\nsteam_sign_in_password=1\n"
                          "downloads_slow_network=0\nsteam_stay_signed_in=1\nbeta_updates=0\ndownload_server=\n",
        "version 8 format includes Automatic download server");
  const auto loaded = store.Load();
  Check(loaded.status == playden::PreferencesLoadStatus::kLoaded && loaded.preferences.steam_sign_in_password &&
            loaded.preferences.cloud_saves_auto,
        "password sign-in survives restart");
  const std::string version_three = "version=3\nreduce_motion=1\ntitle_store_suffix=1\ncloud_saves_auto=1\n";
  Check(storage.WriteAtomic("preferences.conf", playden::Bytes(version_three.begin(), version_three.end())).ok(),
        "version 3 fixture is written");
  const auto three = store.Load();
  Check(three.status == playden::PreferencesLoadStatus::kLoaded && three.preferences.reduce_motion &&
            three.preferences.title_store_suffix && three.preferences.cloud_saves_auto &&
            !three.preferences.steam_sign_in_password,
        "version 3 preferences load and sign in with the QR code");
  Check(!store.Load().preferences.beta_updates, "old preferences default to stable updates");
  preferences.beta_updates = true;
  Check(store.Save(preferences).ok() && store.Load().preferences.beta_updates, "beta opt-in survives restart");
  preferences.beta_updates = false;
  Check(store.Save(preferences).ok() && !store.Load().preferences.beta_updates, "beta opt-out survives restart");
  preferences.downloads_slow_network = true;
  Check(store.Save(preferences).ok() && store.Load().preferences.downloads_slow_network,
        "downloads for slow networks survive restart");
  const std::string version_four =
      "version=4\nreduce_motion=0\ntitle_store_suffix=0\ncloud_saves_auto=1\nsteam_sign_in_password=1\n";
  Check(storage.WriteAtomic("preferences.conf", playden::Bytes(version_four.begin(), version_four.end())).ok(),
        "version 4 fixture is written");
  const auto four = store.Load();
  Check(four.status == playden::PreferencesLoadStatus::kLoaded && four.preferences.steam_sign_in_password &&
            !four.preferences.downloads_slow_network,
        "version 4 preferences load with standard downloads");
  const std::string bad = "version=4\nreduce_motion=0\ntitle_store_suffix=0\ncloud_saves_auto=0\nsteam_sign_in_password=2\n";
  Check(storage.WriteAtomic("preferences.conf", playden::Bytes(bad.begin(), bad.end())).ok(), "bad fixture is written");
  Check(store.Load().status == playden::PreferencesLoadStatus::kDefaultCorrupt, "bad sign-in value is corrupt");
}

void StaySignedInDefaultsOnAndPersistsOff() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  playden::PreferencesStore store(storage);
  Check(store.Load().preferences.steam_stay_signed_in, "a fresh install stays signed in");
  const std::string version_five =
      "version=5\nreduce_motion=0\ntitle_store_suffix=0\ncloud_saves_auto=0\nsteam_sign_in_password=1\n"
      "downloads_slow_network=1\n";
  Check(storage.WriteAtomic("preferences.conf", playden::Bytes(version_five.begin(), version_five.end())).ok(),
        "version 5 fixture is written");
  const auto five = store.Load();
  Check(five.status == playden::PreferencesLoadStatus::kLoaded && five.preferences.steam_sign_in_password &&
            five.preferences.downloads_slow_network && five.preferences.steam_stay_signed_in,
        "version 5 preferences load and stay signed in");
  playden::Preferences preferences;
  preferences.steam_stay_signed_in = false;
  Check(store.Save(preferences).ok(), "stay signed in off saves");
  const auto off = store.Load();
  Check(off.status == playden::PreferencesLoadStatus::kLoaded && !off.preferences.steam_stay_signed_in,
        "stay signed in off survives restart");
  const std::string bad = "version=6\nreduce_motion=0\ntitle_store_suffix=0\ncloud_saves_auto=0\nsteam_sign_in_password=0\n"
                          "downloads_slow_network=0\nsteam_stay_signed_in=2\n";
  Check(storage.WriteAtomic("preferences.conf", playden::Bytes(bad.begin(), bad.end())).ok(), "bad fixture is written");
  const auto corrupt = store.Load();
  Check(corrupt.status == playden::PreferencesLoadStatus::kDefaultCorrupt && corrupt.preferences.steam_stay_signed_in,
        "a bad stay signed in value is corrupt and restores the default");
}

void CorruptPreferencesRestoreDefaultsWithVisibleDiagnostic() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  const playden::Bytes corrupt{'n', 'o', 't', ' ', 'v', 'a', 'l', 'i', 'd'};
  Check(storage.WriteAtomic("preferences.conf", corrupt).ok(),
        "corrupt fixture is written");
  playden::PreferencesStore store(storage);

  const auto loaded = store.Load();

  Check(!loaded.preferences.reduce_motion,
        "corrupt preferences restore reduce motion default");
  Check(loaded.status == playden::PreferencesLoadStatus::kDefaultCorrupt,
        "corruption is distinct from a missing preference file");
  Check(!loaded.diagnostic.empty(), "corruption produces a visible diagnostic");
  Check(loaded.diagnostic.find("corrupt") != std::string::npos,
        "diagnostic explains that preferences are corrupt");
}

void VersionEightLoadsDownloadServerWithoutResettingOtherFields() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  playden::PreferencesStore store(storage);
  const std::string serialized =
      "version=8\nreduce_motion=1\ntitle_store_suffix=1\ncloud_saves_auto=1\nsteam_sign_in_password=1\n"
      "downloads_slow_network=1\nsteam_stay_signed_in=0\nbeta_updates=1\n"
      "download_server=fastly.cdn.steampipe.steamcontent.com\n";
  Check(storage.WriteAtomic("preferences.conf", playden::Bytes(serialized.begin(), serialized.end())).ok(),
        "version 8 fixture is written");
  const auto loaded = store.Load();
  Check(loaded.status == playden::PreferencesLoadStatus::kLoaded && loaded.preferences.reduce_motion &&
            loaded.preferences.title_store_suffix && loaded.preferences.cloud_saves_auto &&
            loaded.preferences.steam_sign_in_password && loaded.preferences.downloads_slow_network &&
            !loaded.preferences.steam_stay_signed_in && loaded.preferences.beta_updates,
        "version 8 loads all existing preferences alongside the download server");
  Check(loaded.preferences.download_server == "fastly.cdn.steampipe.steamcontent.com",
        "version 8 restores the chosen download server");
}

void OlderPreferencesUseAutomaticDownloadServer() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  playden::PreferencesStore store(storage);
  const std::string fields[] = {
      "reduce_motion=1\n", "title_store_suffix=1\n", "cloud_saves_auto=1\n",
      "steam_sign_in_password=1\n", "downloads_slow_network=1\n", "steam_stay_signed_in=0\n",
      "beta_updates=1\n"};
  std::string values;
  for (int version = 1; version <= 7; ++version) {
    values += fields[version - 1];
    const std::string serialized = "version=" + std::to_string(version) + "\n" + values;
    Check(storage.WriteAtomic("preferences.conf", playden::Bytes(serialized.begin(), serialized.end())).ok(),
          "legacy preference fixture is written");
    const auto loaded = store.Load();
    Check(loaded.status == playden::PreferencesLoadStatus::kLoaded && loaded.preferences.download_server.empty(),
          "every older preference version uses Automatic downloads");
    Check(loaded.preferences.reduce_motion && loaded.preferences.title_store_suffix == (version >= 2) &&
              loaded.preferences.cloud_saves_auto == (version >= 3) &&
              loaded.preferences.steam_sign_in_password == (version >= 4) &&
              loaded.preferences.downloads_slow_network == (version >= 5) &&
              loaded.preferences.steam_stay_signed_in == (version < 6) &&
              loaded.preferences.beta_updates == (version >= 7),
          "migration retains old fields and defaults later fields");
    Check(store.Save(loaded.preferences).ok(), "migrated preferences save");
    const auto saved = storage.Read("preferences.conf");
    Check(saved.ok() && std::string(saved.value().begin(), saved.value().end()).rfind("version=8\n", 0) == 0,
          "saving a migrated file writes version 8");
  }
}

void DownloadServerRoundTripsAndInvalidHostsUseAutomatic() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  playden::PreferencesStore store(storage);
  const std::string prefix =
      "version=8\nreduce_motion=1\ntitle_store_suffix=1\ncloud_saves_auto=1\nsteam_sign_in_password=1\n"
      "downloads_slow_network=1\nsteam_stay_signed_in=0\nbeta_updates=1\ndownload_server=";
  // "fastest" is the Fastest choice; it needs no version change.
  const std::string valid[] = {"", "fastest", "fastly.cdn.steampipe.steamcontent.com", "CACHE-1.Example.net", std::string(253, 'a')};
  for (const auto& host : valid) {
    playden::Preferences preferences;
    preferences.download_server = host;
    Check(store.Save(preferences).ok(), "download server preference saves");
    const auto loaded = store.Load();
    Check(loaded.status == playden::PreferencesLoadStatus::kLoaded && loaded.preferences.download_server == host,
          "Automatic and valid download server hosts survive restart");
  }
  {
    const std::string serialized = prefix + "fastest\n";
    Check(storage.WriteAtomic("preferences.conf", playden::Bytes(serialized.begin(), serialized.end())).ok(),
          "version 8 fastest fixture is written");
    const auto loaded = store.Load();
    Check(loaded.status == playden::PreferencesLoadStatus::kLoaded && loaded.preferences.download_server == "fastest",
          "a version 8 file with Fastest loads it");
  }
  const std::string invalid[] = {"https://cache.example", "cache.example/path", "cache.example:443", "cache_name",
                                 "cache name", "cache\tname", "cache\rname", "cache=name", "caf\xc3\xa9.example",
                                 std::string("cache\0name", 10), std::string(254, 'a')};
  for (const auto& host : invalid) {
    const std::string serialized = prefix + host + "\n";
    Check(storage.WriteAtomic("preferences.conf", playden::Bytes(serialized.begin(), serialized.end())).ok(),
          "invalid download server fixture is written");
    const auto loaded = store.Load();
    Check(loaded.status == playden::PreferencesLoadStatus::kLoaded && loaded.preferences.download_server.empty(),
          "an invalid download server loads as Automatic");
    Check(loaded.preferences.reduce_motion && loaded.preferences.title_store_suffix &&
              loaded.preferences.cloud_saves_auto && loaded.preferences.steam_sign_in_password &&
              loaded.preferences.downloads_slow_network && !loaded.preferences.steam_stay_signed_in &&
              loaded.preferences.beta_updates,
          "an invalid server retains all other preferences");
    playden::Preferences preferences;
    preferences.reduce_motion = true;
    preferences.download_server = host;
    Check(store.Save(preferences).ok(), "invalid server can save as Automatic");
    const auto saved = store.Load();
    Check(saved.status == playden::PreferencesLoadStatus::kLoaded && saved.preferences.reduce_motion &&
              saved.preferences.download_server.empty(),
          "saving an invalid server keeps the file readable");
  }
  playden::Preferences injected;
  injected.download_server = "cache.example\nbeta_updates=1";
  Check(store.Save(injected).ok() && store.Load().status == playden::PreferencesLoadStatus::kLoaded &&
            store.Load().preferences.download_server.empty() && !store.Load().preferences.beta_updates,
        "saving a server with a newline cannot inject a preference");
}

void VersionEightRejectsTruncatedOrMalformedFiles() {
  TemporaryDirectory directory;
  playden::Storage storage(directory.path());
  playden::PreferencesStore store(storage);
  const std::string serialized =
      "version=8\nreduce_motion=1\ntitle_store_suffix=0\ncloud_saves_auto=0\nsteam_sign_in_password=0\n"
      "downloads_slow_network=0\nsteam_stay_signed_in=1\nbeta_updates=0\ndownload_server=cache.example\n";
  for (std::size_t length = 0; length < serialized.size(); ++length) {
    const auto truncated = serialized.substr(0, length);
    Check(storage.WriteAtomic("preferences.conf", playden::Bytes(truncated.begin(), truncated.end())).ok(),
          "truncated preference fixture is written");
    const auto loaded = store.Load();
    Check(loaded.status == playden::PreferencesLoadStatus::kDefaultCorrupt && !loaded.preferences.reduce_motion,
          "every truncated version 8 file restores defaults as corrupt");
  }
  std::string invalid_flag = serialized;
  invalid_flag.replace(invalid_flag.find("reduce_motion=1"), 15, "reduce_motion=2");
  std::string missing_field = serialized;
  missing_field.erase(missing_field.find("beta_updates=0\n"), 15);
  const std::string malformed[] = {serialized + "beta_updates=1\n", serialized + "unknown=1\n", serialized + "\n",
                                   invalid_flag, missing_field};
  for (const auto& text : malformed) {
    Check(storage.WriteAtomic("preferences.conf", playden::Bytes(text.begin(), text.end())).ok(),
          "malformed preference fixture is written");
    Check(store.Load().status == playden::PreferencesLoadStatus::kDefaultCorrupt,
          "malformed version 8 fields are corrupt");
  }
}

void PreferencesPrepareIndependentRoot() {
  TemporaryDirectory parent;
  const auto root=parent.path()+"/preferences-root";
  playden::Storage storage(root);
  playden::PreferencesStore preferences(storage);
  Check(preferences.Save({true}).ok(),"preferences prepare a root independently of authentication");
  Check(preferences.Load().preferences.reduce_motion,"prepared preferences restore");
  struct stat info{};
  Check(stat(root.c_str(),&info)==0 && (info.st_mode & 0777)==0700,"new preference root is private");
  std::remove((root+"/preferences.conf").c_str());rmdir(root.c_str());
}

}  // namespace

int main() {
  PreferencesPrepareIndependentRoot();
  StorageRejectsPathsOutsideItsRoot();
  StorageDistinguishesMissingFiles();
  StorageWritesAndReadsBytesAtomically();
  StorageRejectsOversizedReads();
  MissingPreferencesUseDefaultsWithoutAnError();
  PreferencesPersistAcrossStoreInstances();
  CorruptPreferencesRestoreDefaultsWithVisibleDiagnostic();
  TitleStoreSuffixPersistsAndVersionOneStillLoads();
  SignInMethodPersistsAndVersionThreeOpensQr();
  StaySignedInDefaultsOnAndPersistsOff();
  VersionEightLoadsDownloadServerWithoutResettingOtherFields();
  OlderPreferencesUseAutomaticDownloadServer();
  DownloadServerRoundTripsAndInvalidHostsUseAutomatic();
  VersionEightRejectsTruncatedOrMalformedFiles();

  if (failures != 0) {
    std::fprintf(stderr, "%d preference test assertion(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("preferences_test: all assertions passed");
  return EXIT_SUCCESS;
}
