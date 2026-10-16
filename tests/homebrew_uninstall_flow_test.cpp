// The order of an uninstall: Cloud saves, then the PS5 title, then the download.
#include "homebrew/uninstall_flow.hpp"

#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

using namespace playden::homebrew;
using Kind = UninstallAction::Kind;

int main() {
  // With Cloud: the sync is issued once, and nothing is removed until it settles.
  UninstallFlow flow;
  CHECK(flow.Begin(42, true, true));
  CHECK(!flow.Begin(42, false, true));
  CHECK(flow.Step(42) == UninstallStep::kSync);
  UninstallFacts titled;
  titled.has_title = true;
  CHECK(flow.Advance(42, titled).kind == Kind::kNone);
  CHECK(flow.TakeSyncRequests() == std::vector<GameId>{42});
  CHECK(flow.TakeSyncRequests().empty());
  // A status from before the sync was sent is not its result.
  CHECK(flow.CloudSettled(42, CloudStatus::Synced).kind == Kind::kNone && flow.Step(42) == UninstallStep::kSync);
  flow.SyncDispatched(42);
  CHECK(flow.CloudSettled(42, CloudStatus::Syncing).kind == Kind::kNone && flow.WaitingForCloud(42));
  CHECK(flow.CloudSettled(42, CloudStatus::Synced).kind == Kind::kNone);
  CHECK(flow.Step(42) == UninstallStep::kRemoveTitle);

  // The title is removed when no saves move and the game is not running, with the saves choice.
  titled.saves_moving = true;
  CHECK(flow.Advance(42, titled).kind == Kind::kNone);
  titled.saves_moving = false;
  titled.session_active = true;
  CHECK(flow.Advance(42, titled).kind == Kind::kNone);
  titled.session_active = false;
  titled.removals_handled = 7;
  auto action = flow.Advance(42, titled);
  CHECK(action.kind == Kind::kRemoveTitle && action.delete_saves);
  titled.removing = true;
  CHECK(flow.Advance(42, titled).kind == Kind::kNone);
  // Only after the runtime finished and the record is gone does the download go.
  UninstallFacts gone;
  gone.removals_handled = 8;
  CHECK(flow.Advance(42, gone).kind == Kind::kRemoveDownload);
  CHECK(!flow.Step(42) && flow.Active().empty());

  // A failed removal stops with the runtime's reason and keeps the download.
  CHECK(flow.Begin(43, false, false));
  CHECK(flow.TakeSyncRequests().empty());
  titled = {};
  titled.has_title = true;
  CHECK(flow.Advance(43, titled).kind == Kind::kRemoveTitle);
  titled.removals_handled = 1;
  titled.removal_failure = "Close the game before uninstalling it.";
  action = flow.Advance(43, titled);
  CHECK(action.kind == Kind::kStop && action.reason.find("Close the game") != std::string::npos);
  // A refusal before the removal began has no removal for Retry to continue.
  CHECK(action.reason.find("Retry") == std::string::npos);
  CHECK(!flow.Step(43));

  // A removal that stopped partway is continued by Retry.
  CHECK(flow.Begin(47, false, false));
  titled = {};
  titled.has_title = true;
  CHECK(flow.Advance(47, titled).kind == Kind::kRemoveTitle);
  titled.removals_handled = 1;
  titled.removal_started = true;
  titled.removal_failure = "The console has not confirmed that the PS5 title is gone.";
  action = flow.Advance(47, titled);
  CHECK(action.kind == Kind::kStop && action.reason.find("Retry on its PS5 title continues") != std::string::npos);

  // A game without a PS5 title goes straight to the download.
  CHECK(flow.Begin(44, false, false));
  CHECK(flow.Advance(44, UninstallFacts{}).kind == Kind::kRemoveDownload);

  // A Cloud conflict stops and asks; uninstalling again syncs again.
  CHECK(flow.Begin(45, false, true));
  flow.TakeSyncRequests();
  flow.SyncDispatched(45);
  action = flow.CloudSettled(45, CloudStatus::Conflict);
  CHECK(action.kind == Kind::kStop && action.reason.find("Choose which Cloud saves") != std::string::npos);
  CHECK(flow.Begin(45, false, true) && flow.Step(45) == UninstallStep::kSync);
  flow.TakeSyncRequests();
  flow.SyncDispatched(45);
  // A failed sync stops; the next uninstall goes on without syncing.
  action = flow.CloudSettled(45, CloudStatus::Failed);
  CHECK(action.kind == Kind::kStop && action.reason.find("without syncing") != std::string::npos);
  CHECK(flow.Begin(45, false, true) && flow.Step(45) == UninstallStep::kRemoveTitle);
  CHECK(flow.TakeSyncRequests().empty());

  // A running title stops a waiting sync; a game with nothing to sync continues.
  CHECK(flow.Begin(46, false, true));
  CHECK(flow.CloudRefused(46).kind == Kind::kNone);  // not issued yet: nothing to refuse
  flow.TakeSyncRequests();
  CHECK(flow.CloudRefused(46).kind == Kind::kStop && !flow.Step(46));
  CHECK(flow.Begin(47, false, true));
  flow.TakeSyncRequests();
  flow.CloudSkipped(47);
  CHECK(flow.Step(47) == UninstallStep::kRemoveTitle);
  // Unsupported Cloud counts as done.
  CHECK(flow.Begin(48, false, true));
  flow.TakeSyncRequests();
  flow.SyncDispatched(48);
  CHECK(flow.CloudSettled(48, CloudStatus::Unsupported).kind == Kind::kNone && flow.Step(48) == UninstallStep::kRemoveTitle);

  // Two uninstalls: one PS5 title removal at a time, because the runtime counts removals for all games together.
  CHECK(flow.Begin(60, false, false) && flow.Begin(61, false, false));
  UninstallFacts both;
  both.has_title = true;
  both.removals_handled = 5;
  CHECK(flow.Advance(60, both).kind == Kind::kRemoveTitle);
  CHECK(flow.Advance(61, both).kind == Kind::kNone && flow.Step(61) == UninstallStep::kRemoveTitle);
  // The first removal finishes while the second game is still queued: it must not read that as its own result.
  UninstallFacts first_done;
  first_done.removals_handled = 6;
  both.removals_handled = 6;
  CHECK(flow.Advance(61, both).kind == Kind::kNone);
  CHECK(flow.Advance(60, first_done).kind == Kind::kRemoveDownload);
  // Now the second removal goes, and only its own completion moves it on.
  action = flow.Advance(61, both);
  CHECK(action.kind == Kind::kRemoveTitle);
  CHECK(flow.Advance(61, both).kind == Kind::kNone && flow.Step(61) == UninstallStep::kRemoveTitle);
  both.removing = true;
  CHECK(flow.Advance(61, both).kind == Kind::kNone);
  UninstallFacts second_done;
  second_done.removals_handled = 7;
  CHECK(flow.Advance(61, second_done).kind == Kind::kRemoveDownload);
  CHECK(!flow.Step(60) && !flow.Step(61));

  // A stopped removal frees the slot for the waiting game.
  CHECK(flow.Begin(62, false, false) && flow.Begin(63, false, false));
  UninstallFacts stuck;
  stuck.has_title = true;
  CHECK(flow.Advance(62, stuck).kind == Kind::kRemoveTitle);
  CHECK(flow.Advance(63, stuck).kind == Kind::kNone);
  stuck.removals_handled = 1;
  CHECK(flow.Advance(62, stuck).kind == Kind::kStop);
  stuck.removals_handled = 0;
  CHECK(flow.Advance(63, stuck).kind == Kind::kRemoveTitle);
  UninstallFacts done;
  done.removals_handled = 1;
  CHECK(flow.Advance(63, done).kind == Kind::kRemoveDownload);

  // One game's Cloud conflict stops only that game; the other goes on to its removal.
  CHECK(flow.Begin(64, false, true) && flow.Begin(65, false, true));
  CHECK(flow.TakeSyncRequests() == std::vector<GameId>({64, 65}));
  flow.SyncDispatched(64);
  flow.SyncDispatched(65);
  CHECK(flow.CloudSettled(64, CloudStatus::Conflict).kind == Kind::kStop);
  CHECK(!flow.Step(64) && flow.Step(65) == UninstallStep::kSync);
  CHECK(flow.CloudSettled(65, CloudStatus::Synced).kind == Kind::kNone);
  UninstallFacts after_sync;
  after_sync.has_title = true;
  CHECK(flow.Advance(65, after_sync).kind == Kind::kRemoveTitle);
  after_sync = {};
  after_sync.removals_handled = 1;
  CHECK(flow.Advance(65, after_sync).kind == Kind::kRemoveDownload && !flow.Step(65));
  std::puts("uninstall flow tests passed");
  return 0;
}
