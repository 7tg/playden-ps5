#pragma once
#include "steam/library_backend.h"
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace playden {
enum class LibraryEventKind { kLoading, kComplete, kFailure };
struct LibraryEvent {
  LibraryEventKind kind = LibraryEventKind::kFailure;
  std::uint64_t request_id = 0;
  std::uint64_t account_id = 0;
  std::uint64_t account_generation = 0;
  steam::LibraryError error = steam::LibraryError::kNone;
  std::vector<GameSummary> games;
  bool metadata_partial = false;
};

// One worker owns all network/storage calls. Give this service its own repository
// instance if that adapter has worker-local mutable state. Both dependencies
// outlive the service. Events contain no credentials; a failure is not an empty
// successful snapshot. Call Invalidate before sign-out or account replacement.
class LibraryService {
 public:
  LibraryService(steam::LibraryBackend&, SessionRepository&);
  ~LibraryService();
  LibraryService(const LibraryService&) = delete;
  LibraryService& operator=(const LibraryService&) = delete;
  // Generations are monotonic. A stale submission returns 0 without work.
  std::uint64_t Refresh(std::uint64_t account_id, std::uint64_t account_generation);
  void Invalidate(std::uint64_t account_generation);
  std::vector<LibraryEvent> Pump(std::size_t max_events);
 private:
  struct Job {
    std::uint64_t request, account, generation;
    std::shared_ptr<std::atomic<bool>> cancel;
  };
  bool Current(const Job&) const;
  void Run();
  void Execute(const Job&);
  void Emit(const Job&, steam::LibraryResult);
  steam::LibraryBackend& backend_;
  SessionRepository& repository_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::optional<Job> pending_;
  std::shared_ptr<std::atomic<bool>> cancel_;
  std::deque<LibraryEvent> events_;
  std::uint64_t request_ = 0;
  std::uint64_t generation_ = 0;
  bool stopping_ = false;
  std::thread worker_;
};
}
