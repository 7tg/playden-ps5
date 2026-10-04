#include "library/service.h"
#include <algorithm>

namespace playden {
LibraryService::LibraryService(steam::LibraryBackend& backend, SessionRepository& repository)
    : backend_(backend), repository_(repository), worker_([this]{Run();}) {}
LibraryService::~LibraryService() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    if(cancel_) cancel_->store(true);
    pending_.reset(); events_.clear();
  }
  wake_.notify_one(); worker_.join();
}
std::uint64_t LibraryService::Refresh(std::uint64_t account,std::uint64_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(generation < generation_) return 0;
  if(cancel_) cancel_->store(true);
  cancel_ = std::make_shared<std::atomic<bool>>(false);
  generation_ = generation;
  ++request_;
  pending_ = Job{request_,account,generation,cancel_};
  events_.clear();
  LibraryEvent loading;
  loading.kind = LibraryEventKind::kLoading;
  loading.request_id = request_; loading.account_id = account; loading.account_generation = generation;
  events_.push_back(std::move(loading));
  wake_.notify_one();
  return request_;
}
void LibraryService::Invalidate(std::uint64_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(generation < generation_) return;
  if(cancel_) cancel_->store(true);
  generation_ = generation; ++request_;
  pending_.reset(); events_.clear();
}
bool LibraryService::Current(const Job& job) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !stopping_ && job.request==request_ && job.generation==generation_ && !job.cancel->load();
}
std::vector<LibraryEvent> LibraryService::Pump(std::size_t maximum) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LibraryEvent> result;
  const auto count=std::min(maximum,events_.size());
  result.reserve(count);
  for(std::size_t i=0;i<count;++i) {
    result.push_back(std::move(events_.front())); events_.pop_front();
  }
  return result;
}
void LibraryService::Run() {
  for(;;) {
    std::optional<Job> job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock,[this]{return stopping_ || pending_.has_value();});
      if(stopping_) return;
      job=std::move(pending_); pending_.reset();
    }
    try { Execute(*job); }
    catch(...) { Emit(*job,{steam::LibraryError::kProtocol,{},false}); }
  }
}
void LibraryService::Execute(const Job& job) {
  if(!Current(job)) return;
  auto loaded=repository_.Load();
  if(!Current(job)) return;
  if(!loaded.ok()) { Emit(job,{steam::LibraryError::kPersistence,{},false}); return; }
  if(!loaded.value() || !job.account || loaded.value()->steam_id!=job.account || loaded.value()->access_token.empty()) {
    Emit(job,{steam::LibraryError::kNoSession,{},false}); return;
  }
  Emit(job,backend_.Fetch(*loaded.value(),*job.cancel));
}
void LibraryService::Emit(const Job& job,steam::LibraryResult result) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(stopping_ || job.request!=request_ || job.generation!=generation_ || job.cancel->load()) return;
  LibraryEvent event;
  event.request_id=job.request; event.account_id=job.account; event.account_generation=job.generation;
  event.error=result.error;
  if(result.error==steam::LibraryError::kNone) {
    event.kind=LibraryEventKind::kComplete;
    event.games=std::move(result.games); event.metadata_partial=result.metadata_partial;
  } else event.kind=LibraryEventKind::kFailure;
  // Only the current request exists: one loading event plus one terminal event.
  events_.push_back(std::move(event));
}
}
