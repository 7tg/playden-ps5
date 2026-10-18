#include "steam/service.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <future>
#include <functional>
#include <string>
#include <vector>

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
  Gate* restore_gate=nullptr;
  AuthError result=AuthError::kNone;
  std::atomic<int> calls{0};
  AuthResult AuthenticateQr(const std::atomic<bool>&,const ChallengeCallback& challenge) override {
    int call=++calls;
    if(begin_gate && call==1)begin_gate->ArriveAndWait();
    challenge("https://fixture.invalid/qr");
    return {result,result==AuthError::kNone?std::optional<StoredSession>(Session(call)):std::nullopt};
  }
  AuthResult Restore(const StoredSession& session,const std::atomic<bool>&) override {
    ++calls;if(restore_gate)restore_gate->ArriveAndWait();
    return {result,result==AuthError::kNone?std::optional<StoredSession>(session):std::nullopt};
  }
  // Password sign-in: asks for an email code (approval with an app code when approval is set), rejects
  // "WRONG" and signs in with any other code.
  std::string password_seen, account_seen;
  bool approval=false;
  AuthResult AuthenticateCredentials(Credentials& credentials,const std::atomic<bool>& cancel,
                                     const GuardPromptCallback& prompt,const GuardCodeSource& code) override {
    ++calls;password_seen=credentials.password;account_seen=credentials.account_name;
    ZeroizeSecret(credentials.password);
    GuardPrompt shown;
    shown.kind=approval?GuardPromptKind::kApproval:GuardPromptKind::kEmailCode;
    shown.code_allowed=approval;shown.email_domain=approval?"":"example.invalid";
    prompt(shown);
    while(!cancel.load()){
      if(auto typed=code()){
        if(*typed=="WRONG"){GuardPrompt again;again.kind=GuardPromptKind::kEmailCode;again.email_domain="example.invalid";again.code_rejected=true;prompt(again);continue;}
        if(result!=AuthError::kNone)return {result,std::nullopt};
        return {AuthError::kNone,Session(7)};
      }
      std::this_thread::sleep_for(1ms);
    }
    return {AuthError::kCanceled,std::nullopt};
  }
};
// Holds the first QR sign-in at a gate, so later commands queue behind it.
class Recording : public Backend {
 public:
  Gate* gate=nullptr;
  AuthResult AuthenticateQr(const std::atomic<bool>& cancel,const ChallengeCallback& challenge) override {
    if(gate)gate->ArriveAndWait();
    return Backend::AuthenticateQr(cancel,challenge);
  }
};
SteamEvent WaitEvent(SteamService& service,SteamEventKind kind) {
  auto deadline=std::chrono::steady_clock::now()+3s;
  int last_kind=-1;
  while(std::chrono::steady_clock::now()<deadline){
    for(auto& event:service.Pump(64)){
      last_kind=static_cast<int>(event.kind);
      Check(event.challenge_url.find("fixture-refresh-secret")==std::string::npos,"refresh leaked to event");
      Check(event.account_name.find("fixture-access-secret")==std::string::npos,"access leaked to event");
      if(event.kind==kind)return event;
    }
    std::this_thread::sleep_for(1ms);
  }
  std::cerr<<"event deadline exceeded waiting="<<static_cast<int>(kind)<<" last="<<last_kind<<'\n';
  std::exit(1);
}
// The service's log lines, collected from its worker.
class Lines {
 public:
  std::function<void(const std::string&)> Sink() { return [this](const std::string& line){ std::lock_guard<std::mutex> lock(m); lines.push_back(line); }; }
  bool Has(const std::string& part) const {
    std::lock_guard<std::mutex> lock(m);
    for(const auto& line:lines) if(line.find(part)!=std::string::npos) return true;
    return false;
  }
  bool WaitFor(const std::string& part) const {
    auto deadline=std::chrono::steady_clock::now()+3s;
    while(std::chrono::steady_clock::now()<deadline){ if(Has(part)) return true; std::this_thread::sleep_for(1ms); }
    return false;
  }
 private:
  mutable std::mutex m; std::vector<std::string> lines;
};
int main(){
  {
    // Each step of a password sign-in is logged, without the account, password or tokens.
    Store store;Backend backend;backend.approval=true;Lines log;SteamService service(backend,store,log.Sink());
    auto id=service.BeginCredentialSignIn("fixture-account","fixture-password");
    WaitEvent(service,SteamEventKind::kAwaitingApproval);
    service.SubmitGuardCode("WRONG");WaitEvent(service,SteamEventKind::kCodeRejected);
    service.SubmitGuardCode("12345");WaitEvent(service,SteamEventKind::kAuthenticated);
    const auto prefix="request="+std::to_string(id)+" op=password ";
    Check(log.Has(prefix+"start")&&log.Has(prefix+"event=awaiting-approval code_allowed=1")&&
          log.Has(prefix+"event=code-rejected")&&log.Has(prefix+"event=authenticated"),"password sign-in steps logged");
    Check(!log.Has("fixture-account")&&!log.Has("fixture-password")&&!log.Has("fixture-refresh-secret")&&
          !log.Has("fixture-access-secret")&&!log.Has("example.invalid"),"log leaked account data");
  }
  {
    // A result that arrives after a newer command replaced its request says it was dropped.
    Store store;Backend backend;Gate gate;backend.begin_gate=&gate;Lines log;SteamService service(backend,store,log.Sink());
    auto first=service.BeginQrSignIn();gate.Wait();service.Cancel(first);gate.Release();
    WaitEvent(service,SteamEventKind::kCanceled);
    Check(log.WaitFor("request="+std::to_string(first)+" op=qr result=none dropped=superseded"),"dropped result logged");
    Check(log.Has("request="+std::to_string(first)+" event=canceled by=player"),"cancel logged");
  }
  {
    // A restore with no saved session logs why the player sees sign-in.
    Store store;Backend backend;Lines log;SteamService service(backend,store,log.Sink());
    service.RestoreSession();WaitEvent(service,SteamEventKind::kFailure);
    Check(log.WaitFor("op=restore event=failure error=no-session"),"restore failure logged");
  }
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
    Store store;store.value=Session(8);Backend backend;backend.result=AuthError::kNetwork;SteamService service(backend,store);
    auto id=service.RestoreSession();auto e=WaitEvent(service,SteamEventKind::kOfflineSession);
    Check(e.request_id==id&&e.error==AuthError::kNetwork&&e.steam_id==8&&e.account_name=="fixture-account",
          "network restore did not publish saved offline identity");
    Check(store.Load().value().has_value()&&store.saves==0&&store.clears==0,
          "offline restore changed saved credentials");
    service.Cancel(id);Check(service.Pump(64).empty(),"offline terminal remained active");
    backend.result=AuthError::kNone;
    auto retry=service.RestoreSession();e=WaitEvent(service,SteamEventKind::kAuthenticated);
    Check(e.request_id==retry&&e.steam_id==8,"offline restore could not retry authentication");
  }
  {
    Store store;store.value=Session();Backend backend;backend.result=AuthError::kRejected;SteamService service(backend,store);
    service.RestoreSession();auto e=WaitEvent(service,SteamEventKind::kFailure);
    Check(e.error==AuthError::kRejected&&store.Load().value().has_value()&&store.clears==0,
          "rejected restore became offline or changed saved credentials");
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
    for(auto invalid:{StoredSession{},StoredSession{9,"fixture-account","","fixture-access-secret",{}}}){
      Store store;store.value=invalid;Backend backend;backend.result=AuthError::kNetwork;SteamService service(backend,store);
      service.RestoreSession();
      auto e=WaitEvent(service,SteamEventKind::kFailure);
      Check(e.error==AuthError::kProtocol&&backend.calls==0,"structurally invalid saved session became offline");
    }
  }
  {
    Store store;store.value=Session(10);Backend backend;backend.result=AuthError::kNetwork;Gate gate;
    backend.restore_gate=&gate;SteamService service(backend,store);
    auto id=service.RestoreSession();gate.Wait();service.Cancel(id);gate.Release();
    auto e=WaitEvent(service,SteamEventKind::kCanceled);
    Check(e.request_id==id,"restore cancellation identity changed");
    std::this_thread::sleep_for(10ms);
    Check(service.Pump(64).empty()&&store.Load().value().has_value(),
          "late canceled restore published offline or cleared credentials");
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
    Check(store.saves==0&&store.clears==0,"failed fresh authentication changed session storage");
  }
  {
    Store store;store.value=Session();Backend backend;Gate gate;backend.begin_gate=&gate;
    auto service=std::make_unique<SteamService>(backend,store);
    service->BeginQrSignIn();gate.Wait();service->SignOut();gate.Release();service.reset();
    Check(!store.Load().value(),"shutdown dropped signout barrier");
  }
  {
    // Password sign-in: email code, a wrong code, then the right one; the session is saved as for QR.
    Store store;Backend backend;SteamService service(backend,store);
    std::string password="fixture-password";
    auto id=service.BeginCredentialSignIn("fixture-account",password);
    auto e=WaitEvent(service,SteamEventKind::kNeedsEmailCode);
    Check(e.request_id==id&&e.email_domain=="example.invalid"&&e.code_allowed&&e.code_is_email,"email prompt");
    Check(backend.password_seen=="fixture-password"&&backend.account_seen=="fixture-account","credentials not handed over");
    service.SubmitGuardCode("WRONG");
    e=WaitEvent(service,SteamEventKind::kCodeRejected);
    Check(e.code_is_email&&e.email_domain=="example.invalid","rejected prompt");
    service.SubmitGuardCode("AB12C");
    e=WaitEvent(service,SteamEventKind::kAuthenticated);
    Check(e.request_id==id&&e.steam_id==7&&store.saves==1,"password sign-in not saved");
  }
  {
    // Approval with a typed code allowed.
    Store store;Backend backend;backend.approval=true;SteamService service(backend,store);
    service.BeginCredentialSignIn("fixture-account","fixture-password");
    auto e=WaitEvent(service,SteamEventKind::kAwaitingApproval);
    Check(e.code_allowed&&!e.code_is_email,"approval prompt");
    service.SubmitGuardCode("XY34Z");WaitEvent(service,SteamEventKind::kAuthenticated);
  }
  {
    // Codes go only to the running sign-in: one sent before it, or after a cancel, is dropped.
    Store store;Backend backend;SteamService service(backend,store);
    service.SubmitGuardCode("EARLY");
    auto id=service.BeginCredentialSignIn("fixture-account","fixture-password");
    WaitEvent(service,SteamEventKind::kNeedsEmailCode);
    std::this_thread::sleep_for(20ms);
    Check(service.Pump(64).empty(),"a stale code reached the sign-in");
    service.Cancel(id);
    Check(WaitEvent(service,SteamEventKind::kCanceled).request_id==id,"cancel lost");
    service.SubmitGuardCode("LATE");
    std::this_thread::sleep_for(20ms);
    Check(service.Pump(64).empty()&&store.saves==0,"canceled password sign-in continued");
  }
  {
    // A failed password sign-in reports the backend's error and leaves storage alone.
    Store store;store.value=Session();Backend backend;backend.result=AuthError::kRateLimited;SteamService service(backend,store);
    service.BeginCredentialSignIn("fixture-account","fixture-password");
    WaitEvent(service,SteamEventKind::kNeedsEmailCode);service.SubmitGuardCode("AB12C");
    Check(WaitEvent(service,SteamEventKind::kFailure).error==AuthError::kRateLimited,"rate limit lost");
    Check(store.saves==0&&store.clears==0,"failed password sign-in changed storage");
  }
  {
    // A queued password sign-in replaced before it runs never reaches the backend.
    Store store;Recording backend;Gate gate;backend.gate=&gate;SteamService service(backend,store);
    service.BeginQrSignIn();gate.Wait();
    service.BeginCredentialSignIn("fixture-account","fixture-password");
    service.BeginQrSignIn();gate.Release();
    WaitEvent(service,SteamEventKind::kAuthenticated);
    Check(backend.password_seen.empty(),"replaced password sign-in ran");
  }
  std::cout<<"steam_auth_test passed\n";
}
