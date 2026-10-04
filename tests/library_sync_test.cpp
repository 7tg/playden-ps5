#include "library/service.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace playden;
using namespace playden::steam;
using namespace std::chrono_literals;
namespace {
class Repository final : public SessionRepository {
 public:
  mutable std::mutex mutex;
  std::optional<StoredSession> session = StoredSession{123,"synthetic","private-refresh","private-access",{}};
  bool fail = false;
  mutable std::atomic<bool> block{false}, entered{false}, release{false};
  Result<std::optional<StoredSession>> Load() const override {
    entered=true;
    while(block && !release) std::this_thread::sleep_for(1ms);
    std::lock_guard<std::mutex> lock(mutex);
    if (fail) return Result<std::optional<StoredSession>>::Failure(StorageError::kReadFailed,"private details");
    return Result<std::optional<StoredSession>>::Success(session);
  }
  Result<void> Save(const StoredSession&) const override { assert(false); return Result<void>::Success(); }
  Result<void> Clear() const override { assert(false); return Result<void>::Success(); }
};
class Backend final : public LibraryBackend {
 public:
  std::atomic<int> calls{0};
  std::atomic<bool> block{false}, release{false}, canceled{false};
  LibraryResult result{LibraryError::kNone,{{42,"日本語 title",{},{},{}}},true};
  LibraryResult Fetch(const StoredSession& session,const std::atomic<bool>& cancel) override {
    assert(session.steam_id==123 && session.access_token=="private-access");
    ++calls;
    while (block && !release) {
      if (cancel) { canceled=true; break; }
      std::this_thread::sleep_for(1ms);
    }
    return result; // Deliberately ignores cancellation in its returned value.
  }
};
std::vector<LibraryEvent> Terminal(LibraryService& service) {
  const auto deadline=std::chrono::steady_clock::now()+2s;
  std::vector<LibraryEvent> collected;
  while(std::chrono::steady_clock::now()<deadline) {
    for(auto& event:service.Pump(8)) {
      const bool terminal=event.kind!=LibraryEventKind::kLoading;
      collected.push_back(std::move(event));
      if(terminal) return collected;
    }
    std::this_thread::sleep_for(1ms);
  }
  assert(false && "library worker deadline exceeded"); return {};
}
void WaitStarted(Backend& backend) {
  const auto deadline=std::chrono::steady_clock::now()+2s;
  while(!backend.calls && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
  assert(backend.calls);
}
void Retrieval() {
  Repository repository; Backend backend; LibraryService service(backend,repository);
  const auto request=service.Refresh(123,7);
  auto events=Terminal(service);
  assert(events.size()==2 && events.front().kind==LibraryEventKind::kLoading);
  const auto& complete=events.back();
  assert(complete.kind==LibraryEventKind::kComplete && complete.request_id==request);
  assert(complete.account_id==123 && complete.account_generation==7);
  assert(complete.games.size()==1 && complete.games[0].title=="日本語 title" && complete.metadata_partial);
  backend.result={LibraryError::kNone,{},false};
  service.Refresh(123,7); events=Terminal(service);
  assert(events.back().kind==LibraryEventKind::kComplete && events.back().games.empty());
  backend.result={LibraryError::kNetwork,{{999,"must not escape",{},{},{}}},false};
  service.Refresh(123,7); events=Terminal(service);
  assert(events.back().kind==LibraryEventKind::kFailure && events.back().error==LibraryError::kNetwork);
  assert(events.back().games.empty());
}
void WrongAccountAndStorageFailure() {
  for(int mode=0;mode<3;++mode) {
    Repository repository; Backend backend;
    if(mode==1) repository.session.reset();
    if(mode==2) repository.fail=true;
    LibraryService service(backend,repository);
    service.Refresh(456,2); const auto events=Terminal(service);
    assert(events.back().kind==LibraryEventKind::kFailure && backend.calls==0);
    assert(events.back().error==(mode==2?LibraryError::kPersistence:LibraryError::kNoSession));
  }
}
void InvalidationAndReplacement() {
  Repository repository; Backend backend; backend.block=true;
  LibraryService service(backend,repository);
  service.Refresh(123,1); WaitStarted(backend);
  service.Invalidate(2);
  backend.release=true;
  assert(service.Pump(8).empty());
  const auto request=service.Refresh(123,3);
  const auto events=Terminal(service);
  for(const auto& event:events) assert(event.account_generation==3 && event.request_id==request);
  assert(events.back().kind==LibraryEventKind::kComplete);
  service.Invalidate(4);
  assert(service.Pump(8).empty());
}
void ShutdownCancels() {
  Repository repository; Backend backend; backend.block=true;
  { LibraryService service(backend,repository); service.Refresh(123,1); WaitStarted(backend); }
  assert(backend.canceled);
}
void StaleStorageRead() {
  Repository repository; Backend backend; repository.block=true;
  LibraryService service(backend,repository);
  service.Refresh(123,1);
  const auto deadline=std::chrono::steady_clock::now()+2s;
  while(!repository.entered && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
  assert(repository.entered);
  service.Invalidate(2);
  // New account no longer matches the old credentials, including after the
  // blocked old read completes. Neither request may reach the network.
  service.Refresh(456,2); repository.release=true;
  const auto events=Terminal(service);
  assert(backend.calls==0 && events.back().error==LibraryError::kNoSession);
  for(const auto& event:events) assert(event.account_id==456 && event.account_generation==2);
}
void OldGenerationCannotRestart() {
  Repository repository; Backend backend; LibraryService service(backend,repository);
  service.Invalidate(5);
  assert(service.Refresh(123,4)==0);
  service.Invalidate(3);
  assert(service.Refresh(123,4)==0);
  assert(service.Pump(8).empty() && backend.calls==0);
  assert(service.Refresh(123,5)!=0);
  assert(Terminal(service).back().kind==LibraryEventKind::kComplete);
}
}
int main() { Retrieval(); WrongAccountAndStorageFailure(); InvalidationAndReplacement(); ShutdownCancels(); StaleStorageRead(); OldGenerationCannotRestart(); }
