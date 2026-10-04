#include "steam/service.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <future>

using namespace playden;
using namespace playden::steam;
using namespace std::chrono_literals;
void Check(bool value, const char* message) {
  if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
StoredSession Session(std::uint64_t id = 1) {
  return {id, "fixture-account", "fixture-refresh-secret", "fixture-access-secret", {}};
}
class Gate {
 public:
  void ArriveAndWait() { std::unique_lock<std::mutex> lock(m); arrived=true; cv.notify_all(); cv.wait(lock,[&]{return released;}); }
  void Wait() { std::unique_lock<std::mutex> lock(m); Check(cv.wait_for(lock,3s,[&]{return arrived;}),"worker did not reach gate"); }
  void Release() { std::lock_guard<std::mutex> lock(m); released=true; cv.notify_all(); }
 private:
  std::mutex m; std::condition_variable cv; bool arrived=false,released=false;
};
class Store : public SessionRepository {
 public:
  mutable std::mutex m;
  mutable std::optional<StoredSession> value;
  mutable int saves=0,clears=0;
  Gate* save_gate=nullptr;
  bool fail_load=false,fail_clear=false;
  Result<std::optional<StoredSession>> Load() const override {
    std::lock_guard<std::mutex> lock(m);
    return fail_load ? Result<std::optional<StoredSession>>::Failure(StorageError::kInvalidData,"invalid fixture") : Result<std::optional<StoredSession>>::Success(value);
  }
  Result<void> Save(const StoredSession& session) const override {
    if(save_gate) save_gate->ArriveAndWait();
    std::lock_guard<std::mutex> lock(m); value=session; ++saves; return Result<void>::Success();
  }
  Result<void> Clear() const override {
    std::lock_guard<std::mutex> lock(m); ++clears;
    if(fail_clear)return Result<void>::Failure(StorageError::kRemoveFailed,"fixture removal failure");
    value.reset();return Result<void>::Success();
  }
};
class Backend : public AuthBackend {
 public:
  Gate* begin_gate=nullptr;
  AuthError result=AuthError::kNone;
  std::atomic<int> calls{0};
  AuthResult AuthenticateQr(const std::atomic<bool>&,const ChallengeCallback& challenge) override {
    int call=++calls;
    if(begin_gate && call==1)begin_gate->ArriveAndWait();
    challenge("https://fixture.invalid/qr");
    return {result,result==AuthError::kNone?std::optional<StoredSession>(Session(call)):std::nullopt};
  }
  AuthResult Restore(const StoredSession& session,const std::atomic<bool>&) override {
    ++calls; return {result,result==AuthError::kNone?std::optional<StoredSession>(session):std::nullopt};
  }
};
SteamEvent WaitEvent(SteamService& service,SteamEventKind kind) {
  auto deadline=std::chrono::steady_clock::now()+3s;
  while(std::chrono::steady_clock::now()<deadline){
    for(auto& event:service.Pump(64)){
      Check(event.challenge_url.find("fixture-refresh-secret")==std::string::npos,"refresh leaked to event");
      Check(event.account_name.find("fixture-access-secret")==std::string::npos,"access leaked to event");
      if(event.kind==kind)return event;
    }
    std::this_thread::sleep_for(1ms);
  }
  Check(false,"event deadline exceeded");return {};
}
int main(){
  {
    Store store;Backend backend;SteamService service(backend,store);
    auto id=service.BeginQrSignIn();auto e=WaitEvent(service,SteamEventKind::kAuthenticated);
    Check(e.request_id==id&&e.steam_id==1,"authentication identity");
    Check(store.Load().value().has_value(),"approved session not saved");
  }
  {
    Store store;Backend backend;Gate gate;backend.begin_gate=&gate;SteamService service(backend,store);
    service.BeginQrSignIn();gate.Wait();service.SignOut();gate.Release();
    WaitEvent(service,SteamEventKind::kSignedOut);
    Check(!store.Load().value()&&store.saves==0,"late approval resurrected signed out session");
  }
  {
    Store store;Backend backend;Gate gate;backend.begin_gate=&gate;SteamService service(backend,store);
    service.BeginQrSignIn();gate.Wait();auto newest=service.BeginQrSignIn();gate.Release();
    auto e=WaitEvent(service,SteamEventKind::kAuthenticated);
    Check(e.request_id==newest&&e.steam_id==2&&store.saves==1,"replaced attempt authenticated");
  }
  {
    Store store;Backend backend;Gate gate;store.save_gate=&gate;SteamService service(backend,store);
    service.BeginQrSignIn();gate.Wait();
    auto signout=std::async(std::launch::async,[&]{service.SignOut();});
    Check(signout.wait_for(100ms)==std::future_status::ready,"SignOut blocked on storage I/O");
    gate.Release();WaitEvent(service,SteamEventKind::kSignedOut);
    Check(!store.Load().value(),"signout during save left credentials");
  }
  {
    Store store;Backend backend;Gate gate;backend.begin_gate=&gate;SteamService service(backend,store);
    auto id=service.BeginQrSignIn();gate.Wait();service.Cancel(id);gate.Release();
    WaitEvent(service,SteamEventKind::kCanceled);
  }
  {
    Store store;store.value=Session(7);Backend backend;SteamService service(backend,store);
    service.RestoreSession();auto e=WaitEvent(service,SteamEventKind::kAuthenticated);
    Check(e.steam_id==7,"restore identity changed");
  }
  {
    Store store;store.value=Session();Backend backend;backend.result=AuthError::kCredentialsRevoked;SteamService service(backend,store);
    service.RestoreSession();auto e=WaitEvent(service,SteamEventKind::kFailure);
    Check(e.error==AuthError::kCredentialsRevoked&&!store.Load().value(),"revoked session retained");
  }
  {
    Store store;store.fail_clear=true;store.value=Session();Backend backend;SteamService service(backend,store);
    service.SignOut();auto e=WaitEvent(service,SteamEventKind::kFailure);
    Check(e.error==AuthError::kPersistence,"clear failure hidden");
    service.RestoreSession();e=WaitEvent(service,SteamEventKind::kFailure);
    Check(e.error==AuthError::kPersistence&&backend.calls==0,"failed signout allowed restore");
  }
  {
    Store store;store.fail_load=true;Backend backend;SteamService service(backend,store);
    service.RestoreSession();Check(WaitEvent(service,SteamEventKind::kFailure).error==AuthError::kPersistence,"corrupt store accepted");
  }
  {
    Store store;Backend backend;SteamService service(backend,store);
    auto id=service.BeginQrSignIn();WaitEvent(service,SteamEventKind::kAuthenticated);
    service.Cancel(id);Check(service.Pump(64).empty(),"late cancellation changed terminal state");
    Check(store.Load().value().has_value(),"late cancellation removed completed session");
  }
  {
    Store store;store.value=Session();store.fail_clear=true;Backend backend;
    backend.result=AuthError::kCredentialsRevoked;SteamService service(backend,store);
    service.RestoreSession();WaitEvent(service,SteamEventKind::kFailure);
    service.RestoreSession();Check(WaitEvent(service,SteamEventKind::kFailure).error==AuthError::kPersistence,"revocation clear failure not blocked");
    Check(backend.calls==1,"known revoked credentials retried after clear failure");
  }
  for(auto error:{AuthError::kRejected,AuthError::kExpired,AuthError::kNetwork}){
    Store store;Backend backend;backend.result=error;SteamService service(backend,store);
    auto id=service.BeginQrSignIn();Check(WaitEvent(service,SteamEventKind::kFailure).error==error,"backend error lost");
    service.Cancel(id);Check(service.Pump(64).empty(),"late cancel replaced failure");
    Check(store.saves==0,"failed authentication saved session");
  }
  {
    Store store;store.value=Session();Backend backend;Gate gate;backend.begin_gate=&gate;
    auto service=std::make_unique<SteamService>(backend,store);
    service->BeginQrSignIn();gate.Wait();service->SignOut();gate.Release();service.reset();
    Check(!store.Load().value(),"shutdown dropped signout barrier");
  }
  std::cout<<"steam_auth_test passed\n";
}
