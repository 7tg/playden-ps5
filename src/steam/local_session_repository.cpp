#include "steam/local_session_repository.h"
#include <cerrno>
#include <sys/stat.h>

namespace playden {
LocalSessionRepository::LocalSessionRepository(std::string root)
    : root_(std::move(root)),storage_(root_),store_(storage_) {}
Result<void> LocalSessionRepository::Prepare() const {
  if (prepared_) return Result<void>::Success();
  if (root_.empty() || root_.find('\0') != std::string::npos)
    return Result<void>::Failure(StorageError::kInvalidPath,"Session storage path is invalid");
  if (mkdir(root_.c_str(),0700) != 0 && errno != EEXIST)
    return Result<void>::Failure(StorageError::kOpenFailed,"Session storage is unavailable");
  struct stat info{};
  if (stat(root_.c_str(),&info) != 0 || !S_ISDIR(info.st_mode))
    return Result<void>::Failure(StorageError::kOpenFailed,"Session storage is unavailable");
  const Bytes marker{'o','k'};
  constexpr const char* path = "session-storage.probe";
  const auto written = storage_.WriteAtomicPrivate(path,marker);
  if (!written.ok()) return Result<void>::Failure(written.error(),"Private session storage is unavailable");
  const auto loaded = storage_.Read(path,marker.size());
  const auto removed = storage_.Remove(path);
  if (!loaded.ok() || loaded.value() != marker || !removed.ok())
    return Result<void>::Failure(StorageError::kReadFailed,"Session storage validation failed");
  prepared_ = true;
  return Result<void>::Success();
}
Result<std::optional<StoredSession>> LocalSessionRepository::Load() const {
  const auto ready = Prepare();
  if (!ready.ok()) return Result<std::optional<StoredSession>>::Failure(ready.error(),ready.message());
  return store_.Load();
}
Result<void> LocalSessionRepository::Save(const StoredSession& session) const {
  const auto ready = Prepare();
  if (!ready.ok()) return ready;
  return store_.Save(session);
}
Result<void> LocalSessionRepository::Clear() const {
  // Clearing must remain possible even if the private-write probe now fails.
  if (root_.empty() || root_.find('\0') != std::string::npos)
    return Result<void>::Failure(StorageError::kInvalidPath,"Session storage path is invalid");
  return store_.Clear();
}
}
