#pragma once

#include "app/persistence_service.h"
#include "homebrew/simulation.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace playden::app {

// Main-thread bridge between the renderer-neutral model and persistence IO.
// PersistenceService and Simulation outlive this object.
class PersistenceController {
 public:
  PersistenceController(homebrew::Simulation&, PersistenceService&);

  // Organization is device-wide (kDeviceOrganization): it loads at
  // construction and is saved whether or not a store account is signed in.
  // A store account arriving changes nothing.
  void Activate(std::uint64_t account_id);

  // A store account left: saves pending edits; preferences and organization stay.
  void Reset();
  void Tick();
  void Retry();

  // Sign-out step: organization outlives accounts, so this only saves pending
  // edits and reports success. The result remains available until consumed.
  void Clear();
  std::optional<StorageError> TakeClearResult();

  // Resolves required startup loads, captures the final model revisions, and
  // drains accepted saves before stopping the worker.
  void FlushAndShutdown();

 private:
  void Pump(std::size_t maximum);
  void ObserveAndSave();
  void BeginPreferencesLoad();
  void BeginOrganizationLoad();
  void Apply(PersistenceEvent);

  homebrew::Simulation& model_;
  PersistenceService& service_;
  // Always 0: device-wide organization is never invalidated by an account change.
  std::uint64_t organization_generation_ = 0;
  PersistenceRequestId preferences_load_request_ = 0;
  PersistenceRequestId organization_load_request_ = 0;
  homebrew::PersistenceLoadToken preferences_load_token_;
  homebrew::PersistenceLoadToken organization_load_token_;
  std::uint64_t submitted_preferences_revision_ = 0;
  std::uint64_t submitted_organization_revision_ = 0;
  std::optional<StorageError> clear_result_;
  bool preferences_load_failed_ = false;
  bool organization_load_failed_ = false;
  bool shutting_down_ = false;
};

}  // namespace playden::app
