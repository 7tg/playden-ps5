#include "app/persistence_controller.h"

#include <utility>

namespace playden::app {
namespace {

constexpr std::size_t kEventsPerTick = 16;
constexpr std::size_t kShutdownEvents = 64;

bool PreferencesLoadFailed(PreferencesLoadStatus status) {
  return status == PreferencesLoadStatus::kDefaultCorrupt ||
         status == PreferencesLoadStatus::kDefaultReadError;
}

PreferencesLoadResult PreferenceResult(const PersistenceEvent& event) {
  PreferencesLoadResult result;
  if (event.preferences) result.preferences = *event.preferences;
  result.status = event.preferences_status;
  return result;
}

}  // namespace

PersistenceController::PersistenceController(homebrew::Simulation& model,
                                             PersistenceService& service)
    : model_(model), service_(service) {
  BeginPreferencesLoad();
  BeginOrganizationLoad();
}

void PersistenceController::BeginPreferencesLoad() {
  preferences_load_token_ = model_.BeginPreferencesLoad();
  preferences_load_request_ = service_.LoadPreferences();
  preferences_load_failed_ = false;
  if (preferences_load_request_) return;

  PreferencesLoadResult failure;
  failure.status = PreferencesLoadStatus::kDefaultReadError;
  failure.diagnostic = "Preferences are busy and could not be loaded.";
  model_.ApplyPreferences(preferences_load_token_, failure);
  preferences_load_failed_ = true;
}

void PersistenceController::BeginOrganizationLoad() {
  organization_load_token_ = model_.BeginOrganizationLoad(kDeviceOrganization);
  organization_load_request_ =
      service_.LoadOrganization(kDeviceOrganization, organization_generation_);
  organization_load_failed_ = false;
  if (organization_load_request_) return;

  model_.FailOrganizationLoad(kDeviceOrganization, organization_load_token_,
                              StorageError::kReadFailed);
  organization_load_failed_ = true;
}

// Organization belongs to the device: store accounts coming and going leave it alone.
void PersistenceController::Activate(std::uint64_t) {}

void PersistenceController::Reset() {
  if (shutting_down_) return;
  ObserveAndSave();
}

void PersistenceController::Tick() {
  if (shutting_down_) return;
  Pump(kEventsPerTick);
  if (model_.TakePersistenceRetryRequest()) Retry();
  ObserveAndSave();
}

void PersistenceController::Retry() {
  if (shutting_down_) return;
  if (preferences_load_failed_) BeginPreferencesLoad();
  else submitted_preferences_revision_ = 0;

  if (organization_load_failed_) BeginOrganizationLoad();
  else submitted_organization_revision_ = 0;
}

void PersistenceController::Clear() {
  // Signing out keeps the device's organization; there is nothing to delete.
  if (shutting_down_ || clear_result_) return;
  ObserveAndSave();
  clear_result_ = StorageError::kNone;
}

std::optional<StorageError> PersistenceController::TakeClearResult() {
  auto result = clear_result_;
  clear_result_.reset();
  return result;
}

void PersistenceController::Pump(std::size_t maximum) {
  for (auto& event : service_.Pump(maximum)) Apply(std::move(event));
}

void PersistenceController::Apply(PersistenceEvent event) {
  if (event.kind == PersistenceEventKind::kPreferencesLoaded) {
    if (event.request_id != preferences_load_request_) return;
    preferences_load_request_ = 0;
    const auto loaded = PreferenceResult(event);
    preferences_load_failed_ = PreferencesLoadFailed(loaded.status);
    model_.ApplyPreferences(preferences_load_token_, loaded);
    return;
  }
  if (event.kind == PersistenceEventKind::kPreferencesSaved) {
    model_.PreferencesSaveResult(event.revision, event.error);
    return;
  }
  if (event.kind == PersistenceEventKind::kOrganizationCleared) return;
  if (event.account_id != kDeviceOrganization ||
      event.account_generation != organization_generation_) return;
  if (event.kind == PersistenceEventKind::kOrganizationLoaded) {
    if (event.request_id != organization_load_request_) return;
    organization_load_request_ = 0;
    if (event.error == StorageError::kNone) {
      organization_load_failed_ = !model_.ApplyOrganization(
          kDeviceOrganization, organization_load_token_, std::move(event.organization));
    } else {
      organization_load_failed_ = true;
      model_.FailOrganizationLoad(kDeviceOrganization, organization_load_token_,
                                  event.error);
    }
    return;
  }
  if (event.kind == PersistenceEventKind::kOrganizationSaved)
    model_.OrganizationSaveResult(event.account_id, event.revision, event.error);
}

void PersistenceController::ObserveAndSave() {
  if (!preferences_load_request_ && !preferences_load_failed_ &&
      model_.PreferencesDirty()) {
    const auto revision = model_.PreferencesRevision();
    if (revision != submitted_preferences_revision_) {
      const auto request =
          service_.SavePreferences(model_.ExportPreferences(), revision);
      submitted_preferences_revision_ = revision;
      if (!request)
        model_.PreferencesSaveResult(revision, StorageError::kWriteFailed);
    }
  }

  if (organization_load_request_ ||
      organization_load_failed_ || !model_.OrganizationReadyForSave() ||
      !model_.OrganizationDirty()) return;
  const auto revision = model_.OrganizationRevision();
  if (revision == submitted_organization_revision_) return;
  const auto request = service_.SaveOrganization(
      kDeviceOrganization, organization_generation_, revision, model_.ExportOrganization());
  submitted_organization_revision_ = revision;
  if (!request)
    model_.OrganizationSaveResult(kDeviceOrganization, revision,
                                  StorageError::kWriteFailed);
}

void PersistenceController::FlushAndShutdown() {
  if (shutting_down_) return;

  // Complete required loads before stopping: their results supply untouched
  // stored fields which the model merges with edits made while IO was pending.
  ObserveAndSave();
  service_.WaitIdle();
  Pump(kShutdownEvents);
  ObserveAndSave();
  service_.WaitIdle();
  Pump(kShutdownEvents);
  service_.Shutdown();
  Pump(kShutdownEvents);
  shutting_down_ = true;
}

}  // namespace playden::app
