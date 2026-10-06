// runtime_host.cpp - Worker-owned composition of the per-game runtime services.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "homebrew/runtime_host.hpp"

#include "platform/storage.h"
#include "runtime/private_directories.h"
#include "runtime/prospero_profile.h"
#include "runtime/settings_publication_journal.h"
#include "runtime/settings_publication_owner.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <utility>

namespace playden::homebrew {
namespace rt = playden::runtime;
namespace {
// Stands in for the package layer while no verified kit is admitted. It reports
// unknown package state and refuses every action, so nothing is staged.
class UnavailablePackageInstaller final : public rt::PackageInstaller {
 public:
  explicit UnavailablePackageInstaller(std::string reason) : reason_(std::move(reason)) {}
  rt::PackageStatus Inspect(const rt::RuntimeInstall&) override { return {}; }
  rt::PackageActionResult StageTitle(const rt::RuntimeInstall&) override { return Refuse(); }
  rt::PackageActionResult CopyTemplate(const rt::RuntimeInstall&) override { return Refuse(); }
  rt::PackageActionResult ValidateArtifacts(const rt::RuntimeInstall&) override { return Refuse(); }
  rt::PackageActionResult PublishProfile(const rt::RuntimeInstall&) override { return Refuse(); }
 private:
  rt::PackageActionResult Refuse() const { return {rt::PackageActionState::kFailed, "kit-unavailable", reason_}; }
  std::string reason_;
};

// Bounded wait while ShadowMount registers a submitted title.
constexpr int kNativeAwaitPolls = 30;
constexpr auto kNativeAwaitInterval = std::chrono::seconds(2);
constexpr int kMaxPreparationSteps = 64;

std::string InstallId(std::uint32_t app_id) { return "steam-" + std::to_string(app_id); }
}  // namespace

std::optional<std::string> RandomUuid() {
  unsigned char bytes[16];
  const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return std::nullopt;
  std::size_t read_bytes = 0;
  while (read_bytes < sizeof(bytes)) {
    const auto count = read(fd, bytes + read_bytes, sizeof(bytes) - read_bytes);
    if (count <= 0) { close(fd); return std::nullopt; }
    read_bytes += static_cast<std::size_t>(count);
  }
  close(fd);
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);  // version 4
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);  // RFC 4122 variant
  char text[37];
  std::snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9],
                bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return std::string(text);
}

// Construction order resolves the ownership cycle:
//   repository -> records -> installer -> registry -> coordinator -> owner -> service.
// The repository's availability query consults the coordinator, which is built
// later; the repository constructor never calls it, and Load/Reserve/Adopt only
// run after the whole composition exists. The installer's publication callback and
// the service's driver dereference the owner at call time. Members are destroyed
// in reverse order, so no collaborator outlives what it references.
struct RuntimeHost::Composition {
  explicit Composition(const RuntimeHostConfig& config)
      : storage(config.state_root.string()),
        repository(storage, "installs.json",
                   [this](const std::string& title) {
                     return coordinator ? coordinator->Availability(title) : rt::NativeTitleAvailability{};
                   }),
        records(repository),
        registry(config.registry_transport) {}

  playden::Storage storage;
  rt::RuntimeInstallRepository repository;
  rt::RepositoryInstallRecordStore records;
  std::optional<RuntimeKit> kit;
  std::unique_ptr<rt::PackageInstaller> installer;
  ps5::TitleRegistry registry;
  std::unique_ptr<ps5::Ps5NativeCoordinator> coordinator;
  std::unique_ptr<rt::SettingsPublicationOwner> owner;
  std::unique_ptr<rt::RuntimeInstallService> service;
  std::filesystem::path request_directory;
  std::vector<rt::GameRecipe> recipes;
  std::vector<rt::SettingsPreset> presets;
  std::string settings_data_reason;
};

RuntimeHost::RuntimeHost(RuntimeHostConfig config) : config_(std::move(config)) {
  if (!config_.uuid) config_.uuid = RandomUuid;
  if (!config_.unix_time) config_.unix_time = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
}
RuntimeHost::~RuntimeHost() { Shutdown(); }

bool RuntimeHost::Start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_) return true;
  pthread_attr_t attributes;
  if (pthread_attr_init(&attributes) != 0) return false;
  auto* thread = new pthread_t{};
  const bool ok = pthread_attr_setstacksize(&attributes, config_.stack_bytes) == 0 &&
                  pthread_create(thread, &attributes, &RuntimeHost::Entry, this) == 0;
  pthread_attr_destroy(&attributes);
  if (!ok) {
    delete thread;
    snapshot_.status = RuntimeHostStatus::kUnavailable;
    snapshot_.reason = "The runtime worker could not start.";
    ++snapshot_.revision;
    return false;
  }
  thread_ = thread;
  started_ = true;
  return true;
}

void RuntimeHost::Shutdown() {
  pthread_t* thread = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    thread = static_cast<pthread_t*>(thread_);
    thread_ = nullptr;
  }
  wake_.notify_all();
  if (thread) {
    pthread_join(*thread, nullptr);
    delete thread;
  }
}

RuntimeSnapshot RuntimeHost::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

bool RuntimeHost::Stopping() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stopping_;
}

void RuntimeHost::Prepare(RuntimePrepareRequest request) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return;
    prepare_requests_.push_back(std::move(request));
  }
  wake_.notify_all();
}

void* RuntimeHost::Entry(void* self) {
  static_cast<RuntimeHost*>(self)->Run();
  return nullptr;
}

void RuntimeHost::Publish(const std::function<void(RuntimeSnapshot&)>& change) {
  std::lock_guard<std::mutex> lock(mutex_);
  change(snapshot_);
  ++snapshot_.revision;
}

void RuntimeHost::PublishInstalls(const Composition& composition) {
  std::vector<RuntimeInstallView> installs;
  for (const auto& record : composition.repository.Snapshot()) {
    RuntimeInstallView view;
    view.install_id = record.install_id; view.app_id = record.app_id; view.title_id = record.title_id;
    view.state = record.state;
    if (record.journal) view.step = record.journal->step;
    view.recovery_pending = record.journal.has_value();
    if (record.last_failure) view.failure = record.last_failure->message;
    installs.push_back(std::move(view));
  }
  Publish([&](RuntimeSnapshot& s) { s.installs = std::move(installs); });
}

// The complete OWNER protocol for the publish-profile step: persist the exact
// requested plan, arm OWNER, then capture, publish, register and commit Ready.
// Recovery journals resume their saved request or frozen operation; nothing is
// regenerated from newer settings.
rt::PackageActionResult RuntimeHost::DrivePublication(Composition& c, const rt::RuntimeInstall& record) {
  auto failed = [](std::string code, std::string message) {
    return rt::PackageActionResult{rt::PackageActionState::kFailed, std::move(code), std::move(message)};
  };
  if (!record.journal) return failed("publication-journal", "No preparation journal to publish.");
  const auto& prefix = config_.host_namespace_prefix;
  if (record.journal->operation == "settings-request")
    return c.owner->ResumeRequested(record, c.request_directory, prefix);
  if (record.journal->operation != "prepare") return c.owner->Resume(record, prefix);
  if (!c.kit) return failed("kit-unavailable", "No verified compatibility kit is installed.");

  const rt::GameRecipe* recipe = nullptr;
  for (const auto& candidate : c.recipes)
    if (candidate.app_id == record.app_id && candidate.id == record.recipe_id) recipe = &candidate;
  if (!recipe) return failed("recipe-unavailable", "The game's preparation recipe is unavailable.");
  rt::SettingsLaunchInput input;
  input.recipe = recipe;
  input.record = config_.settings_record ? config_.settings_record(record.app_id).value_or(rt::GameSettingsRecord{})
                                         : rt::GameSettingsRecord{};
  for (const auto& preset : c.presets)
    if (preset.id == input.record.preset_id) input.preset = &preset;
  input.kit = rt::KitCapabilitiesFromManifest(c.kit->capabilities, true);
  input.target = {record.default_profile_id, record.install_path};
  input.runtime_dir = config_.runtime_dir;
  rt::SettingsLaunchPlan plan;
  std::string reason;
  if (input.record.revision != record.settings_revision)
    return failed("settings-changed", "The game's settings changed during preparation; prepare it again.");
  if (!rt::BuildSettingsLaunchPlan(input, &plan, &reason)) return failed("settings-plan", reason);
  const auto transaction = config_.uuid();
  if (!transaction) return failed("random-unavailable", "No secure random source for the publication transaction.");
  rt::SettingsPublicationRequest request;
  request.transaction_id = *transaction;
  request.kit_id = c.kit->view.kit_id;
  request.install = record;
  request.plan = std::move(plan);
  request.registry_unix_time = config_.unix_time();
  const auto requested = c.owner->Request(request, c.request_directory);
  if (requested.state != rt::PackageActionState::kComplete) return requested;
  const auto armed = c.records.Find(record.install_id);
  if (!armed) return failed("publication-owner", "The armed OWNER record is missing.");
  return c.owner->ResumeRequested(*armed, c.request_directory, prefix);
}

std::string RuntimeHost::RunPrepare(Composition& c, const RuntimePrepareRequest& request) {
  if (!c.kit) return Snapshot().kit_reason;
  std::optional<rt::RuntimeInstall> existing;
  for (const auto& record : c.repository.Snapshot())
    if (record.app_id == request.app_id) existing = record;
  rt::InstallServiceResult started = rt::InstallServiceResult::Success();
  if (existing) {
    if (existing->state == rt::PreparationState::kReady) return "This game is already prepared.";
    if (existing->install_path != request.install_path)
      return "The game's files moved since preparation started; recovery is not connected yet.";
    if (existing->state == rt::PreparationState::kFailed || existing->state == rt::PreparationState::kUnavailable)
      started = c.service->Retry(existing->install_id);
    else if (existing->state == rt::PreparationState::kDownloaded)
      started = c.service->Prepare(existing->install_id);
    else
      return "An interrupted preparation needs recovery, which is not connected yet.";
  } else {
    const rt::GameRecipe* recipe = nullptr;
    for (const auto& candidate : c.recipes) if (candidate.app_id == request.app_id) recipe = &candidate;
    if (!recipe)
      return c.settings_data_reason.empty() ? "No compatible preparation recipe exists for this game."
                                            : c.settings_data_reason;
    if (recipe->id != c.kit->view.recipe.id || std::to_string(recipe->version) != c.kit->view.recipe.version)
      return "The installed kit does not carry this game's recipe.";
    if (recipe->launch_entries.empty()) return "The game's recipe has no launch entry.";
    const auto& launch = recipe->launch_entries.front();
    const auto settings = config_.settings_record ? config_.settings_record(request.app_id) : std::nullopt;
    rt::ReserveRequest reserve;
    reserve.install_id = InstallId(request.app_id);
    reserve.app_id = request.app_id;
    reserve.default_profile_id = rt::ProsperoProfileId(request.app_id);
    reserve.runtime_bundle_id = c.kit->view.runtime.bundle_id;
    reserve.recipe_id = recipe->id;
    reserve.recipe_version = std::to_string(recipe->version);
    reserve.settings_revision = settings ? settings->revision : 0;
    reserve.launch_entry_id = launch.id;
    reserve.launch_path = launch.executable;
    reserve.launch_working_directory = launch.working_directory;
    reserve.install_path = request.install_path;
    reserve.volume_id = request.volume_id;
    for (const auto& variant : c.kit->view.variants) reserve.kit_title_variants.push_back(variant.title_id);
    auto reserved = c.repository.Reserve(reserve);
    if (!reserved.ok()) return "A PS5 title could not be reserved: " + reserved.message();
    auto record = reserved.value();
    record.artifacts.clear();
    for (const auto& artifact : c.kit->view.artifacts)
      record.artifacts.push_back({artifact.artifact_id, artifact.identity, artifact.sha256});
    const auto bound = c.repository.Update(record);
    if (!bound.ok()) return "The kit artifacts could not be recorded: " + bound.message();
    PublishInstalls(c);
    started = c.service->Prepare(record.install_id);
  }
  PublishInstalls(c);
  if (!started.ok()) return started.message;
  const auto install_id = existing ? existing->install_id : InstallId(request.app_id);
  int awaits = 0;
  for (int steps = 0; steps < kMaxPreparationSteps && !Stopping(); ++steps) {
    const auto current = c.records.Find(install_id);
    if (!current) return "The preparation record disappeared.";
    if (current->state == rt::PreparationState::kReady) return {};
    if (current->state != rt::PreparationState::kPreparing && current->state != rt::PreparationState::kRegistering)
      return current->last_failure ? current->last_failure->message : "Preparation stopped.";
    const auto step = c.service->Step(install_id);
    PublishInstalls(c);
    if (!step.ok()) return step.message;
    const auto after = c.records.Find(install_id);
    if (after && after->journal && after->journal->step == "await-native") {
      if (++awaits > kNativeAwaitPolls)
        return "The console has not finished registering the title; retry later.";
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait_for(lock, kNativeAwaitInterval, [this] { return stopping_; });
    }
  }
  return Stopping() ? "Preparation was interrupted." : "Preparation did not finish.";
}

void RuntimeHost::Run() {
  auto unavailable = [this](std::string reason) {
    Publish([&](RuntimeSnapshot& s) { s.status = RuntimeHostStatus::kUnavailable; s.reason = std::move(reason); });
  };
  std::string reason;
  const auto request_directory = config_.state_root / "requests";
  const bool private_ready =
      rt::EnsurePrivateDirectories(config_.boundary, config_.state_root, config_.state_root, &reason) &&
      rt::EnsurePrivateDirectories(config_.boundary, config_.state_root, request_directory, &reason);
  if (!private_ready) unavailable("Private runtime state is unavailable: " + reason);
  std::unique_ptr<Composition> composition;
  if (private_ready) {
    composition = std::make_unique<Composition>(config_);
    auto& c = *composition;
    c.request_directory = request_directory;
    if (!rt::LoadSettingsData(config_.runtime_dir, &c.presets, &c.recipes, &c.settings_data_reason) &&
        c.settings_data_reason.empty())
      c.settings_data_reason = "Game settings data is unavailable.";
    std::string kit_reason;
    c.kit = config_.admit_kit ? config_.admit_kit(&kit_reason) : std::nullopt;
    if (c.kit) {
      auto operations = config_.package_operations;
      operations.canceled = [this] { return Stopping(); };
      c.installer = std::make_unique<rt::FilesystemPackageInstaller>(c.kit->view, config_.package_paths, operations);
    } else {
      if (kit_reason.empty()) kit_reason = "No verified compatibility kit is installed.";
      c.installer = std::make_unique<UnavailablePackageInstaller>(kit_reason);
    }
    c.coordinator = std::make_unique<ps5::Ps5NativeCoordinator>(
        c.records, *c.installer, c.registry, config_.homebrew_root, config_.native);
    c.owner = std::make_unique<rt::SettingsPublicationOwner>(c.records, *c.installer, *c.coordinator);
    c.service = std::make_unique<rt::RuntimeInstallService>(
        c.records, *c.installer, *c.coordinator,
        [&c](const rt::RuntimeInstall& record) {
          for (const auto& recipe : c.recipes)
            if (recipe.app_id == record.app_id && recipe.id == record.recipe_id &&
                std::to_string(recipe.version) == record.recipe_version)
              return rt::CompatibilityResult{true, {}};
          return rt::CompatibilityResult{false, "No compatible preparation recipe exists for this game."};
        },
        [this] { return Stopping(); },
        [this, &c](const rt::RuntimeInstall& record) { return DrivePublication(c, record); });
    const auto loaded = c.repository.Load();
    if (!loaded.ok()) {
      unavailable("Runtime install records are unavailable: " + loaded.message());
      composition.reset();
    } else {
      // Startup only reads durable state. Records with a journal wait for an
      // explicit recovery action; nothing resumes, registers or rebuilds here.
      PublishInstalls(c);
      const bool kit_available = c.kit.has_value();
      Publish([&](RuntimeSnapshot& s) {
        s.status = RuntimeHostStatus::kReady; s.reason.clear();
        s.kit_available = kit_available; s.kit_reason = kit_available ? std::string{} : kit_reason;
      });
    }
  }
  for (;;) {
    std::vector<RuntimePrepareRequest> requests;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !prepare_requests_.empty(); });
      if (stopping_) break;
      requests.swap(prepare_requests_);
    }
    for (const auto& request : requests) {
      std::string refusal;
      if (!composition) {
        const auto current = Snapshot();
        refusal = current.reason.empty() ? "The runtime services are unavailable." : current.reason;
      } else {
        Publish([&](RuntimeSnapshot& s) { s.preparing_app = request.app_id; s.refusals.erase(request.app_id); });
        refusal = RunPrepare(*composition, request);
      }
      Publish([&](RuntimeSnapshot& s) {
        s.preparing_app.reset();
        if (refusal.empty()) s.refusals.erase(request.app_id);
        else s.refusals[request.app_id] = refusal;
      });
    }
  }
  composition.reset();
  Publish([](RuntimeSnapshot& s) { s.status = RuntimeHostStatus::kStopped; });
}

}  // namespace playden::homebrew
