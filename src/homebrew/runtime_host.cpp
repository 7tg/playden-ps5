// runtime_host.cpp - Worker-owned composition of the per-game runtime services.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "homebrew/runtime_host.hpp"

#include "platform/storage.h"
#include "runtime/private_directories.h"
#include "runtime/settings_publication_owner.h"

#include <pthread.h>
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
}  // namespace

// Construction order resolves the ownership cycle:
//   repository -> records -> installer -> registry -> coordinator -> owner -> service.
// The repository's availability query consults the coordinator, which is built
// later; the repository constructor never calls it, and Load/Reserve/Adopt only
// run after the whole composition exists. The coordinator, OWNER and service hold
// references to the records/installer, so members are destroyed in reverse order.
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
  std::unique_ptr<rt::PackageInstaller> installer;
  ps5::TitleRegistry registry;
  std::unique_ptr<ps5::Ps5NativeCoordinator> coordinator;
  std::unique_ptr<rt::SettingsPublicationOwner> owner;
  std::unique_ptr<rt::RuntimeInstallService> service;
};

RuntimeHost::RuntimeHost(RuntimeHostConfig config) : config_(std::move(config)) {}
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

void RuntimeHost::Prepare(std::uint32_t app_id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return;
    prepare_requests_.push_back(app_id);
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

void RuntimeHost::Run() {
  auto unavailable = [this](std::string reason) {
    Publish([&](RuntimeSnapshot& s) { s.status = RuntimeHostStatus::kUnavailable; s.reason = std::move(reason); });
  };
  std::string reason;
  if (!rt::EnsurePrivateDirectories(config_.boundary, config_.state_root, config_.state_root, &reason)) {
    unavailable("Private runtime state is unavailable: " + reason);
  }
  std::unique_ptr<Composition> composition;
  if (reason.empty()) {
    composition = std::make_unique<Composition>(config_);
    std::string kit_reason;
    composition->installer = config_.make_installer ? config_.make_installer(&kit_reason) : nullptr;
    const bool kit_available = composition->installer != nullptr;
    if (!kit_available) {
      if (kit_reason.empty()) kit_reason = "No verified compatibility kit is installed.";
      composition->installer = std::make_unique<UnavailablePackageInstaller>(kit_reason);
    }
    composition->coordinator = std::make_unique<ps5::Ps5NativeCoordinator>(
        composition->records, *composition->installer, composition->registry, config_.homebrew_root, config_.native);
    composition->owner = std::make_unique<rt::SettingsPublicationOwner>(
        composition->records, *composition->installer, *composition->coordinator);
    composition->service = std::make_unique<rt::RuntimeInstallService>(
        composition->records, *composition->installer, *composition->coordinator,
        [](const rt::RuntimeInstall&) { return rt::CompatibilityResult{true, {}}; },
        [this] { std::lock_guard<std::mutex> lock(mutex_); return stopping_; });
    const auto loaded = composition->repository.Load();
    if (!loaded.ok()) {
      unavailable("Runtime install records are unavailable: " + loaded.message());
      composition.reset();
    } else {
      // Startup only reads durable state. Records with a journal wait for an
      // explicit recovery action; nothing resumes, registers or rebuilds here.
      std::vector<RuntimeInstallView> installs;
      for (const auto& record : composition->repository.Snapshot()) {
        RuntimeInstallView view;
        view.install_id = record.install_id; view.app_id = record.app_id; view.title_id = record.title_id;
        view.state = record.state; view.recovery_pending = record.journal.has_value();
        if (record.last_failure) view.failure = record.last_failure->message;
        installs.push_back(std::move(view));
      }
      Publish([&](RuntimeSnapshot& s) {
        s.status = RuntimeHostStatus::kReady; s.reason.clear();
        s.kit_available = kit_available; s.kit_reason = kit_available ? std::string{} : kit_reason;
        s.installs = std::move(installs);
      });
    }
  }
  for (;;) {
    std::vector<std::uint32_t> requests;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !prepare_requests_.empty(); });
      if (stopping_) break;
      requests.swap(prepare_requests_);
    }
    for (const auto app_id : requests) {
      std::string refusal;
      const auto current = Snapshot();
      if (!composition) refusal = current.reason.empty() ? "The runtime services are unavailable." : current.reason;
      else if (!current.kit_available) refusal = current.kit_reason;
      // Preparation with an admitted kit is connected in a later increment.
      else refusal = "Preparing games is not connected yet.";
      Publish([&](RuntimeSnapshot& s) { s.refusals[app_id] = refusal; });
    }
  }
  composition.reset();
  Publish([](RuntimeSnapshot& s) { s.status = RuntimeHostStatus::kStopped; });
}

}  // namespace playden::homebrew
