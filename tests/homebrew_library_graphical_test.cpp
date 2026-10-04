#include "homebrew/render.hpp"
#include "app/library_controller.h"
#include <chrono>
#include <thread>
#include "homebrew/simulation.hpp"
#include "gfx/gl_program.hpp"
#include <SDL.h>
#include <cassert>
#include <filesystem>

namespace {
using namespace playden;
struct Sessions final : SessionRepository {
  Result<std::optional<StoredSession>> Load() const override {
    return Result<std::optional<StoredSession>>::Success(StoredSession{123,"Synthetic account","synthetic-refresh","synthetic-access",{}});
  }
  Result<void> Save(const StoredSession&) const override { return Result<void>::Success(); }
  Result<void> Clear() const override { return Result<void>::Success(); }
};
struct Backend final : steam::LibraryBackend {
  std::vector<GameSummary> games;
  steam::LibraryResult Fetch(const StoredSession&,const std::atomic<bool>&) override {
    return {steam::LibraryError::kNone,games,false};
  }
};
struct Cache final : LibraryRepository {
  mutable std::optional<LibrarySnapshot> snapshot;
  Result<LibrarySnapshot> Load(std::uint64_t id) const override {
    assert(id==123);
    return snapshot ? Result<LibrarySnapshot>::Success(*snapshot)
        : Result<LibrarySnapshot>::Failure(StorageError::kNotFound,"missing");
  }
  Result<void> Save(std::uint64_t id,const LibrarySnapshot& value) const override {
    assert(id==123);snapshot=value;return Result<void>::Success();
  }
  Result<void> Clear(std::uint64_t) const override { snapshot.reset();return Result<void>::Success(); }
};
}

int main(int argc,char** argv) {
  using namespace playden::homebrew;
  if(argc!=2) return 2;
  assert(SDL_Init(SDL_INIT_VIDEO)==0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  auto* window=SDL_CreateWindow("Synthetic live library verification",0,0,1920,1080,
                                 SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
  assert(window);
  auto context=SDL_GL_CreateContext(window); assert(context);
  hui::gfx::set_glsl_prefix("#version 410 core\n");
  const std::filesystem::path output(argv[1]);
  std::filesystem::create_directories(output);
  {
    Simulation model({}, {},ModelMode::Live);
    model.SetWallClock(1700000000);
    model.UpdateAccount({AuthState::SignedIn,"123","Synthetic account",{},{},true});
    playden::GameSummary first{42,"Synthetic library title",{},"Known synthetic description","Example studio"};
    first.playtime_minutes=91; first.last_played_unix_seconds=1699827200;
    Sessions sessions;Backend backend;Cache cache;
    backend.games={first,{43,"Café on the coast",{},{},{}},{44,"Αθήνα",{},{},{}}};
    playden::LibraryService service(backend,sessions,&cache,[]{return 1700000000;});
    playden::app::LibraryController controller(model,service);
    auto settle=[&] {
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
      do { controller.Tick(); if(!model.state().library_loading) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while(std::chrono::steady_clock::now()<deadline);
      assert(false && "graphical library service deadline");
    };
    controller.Activate(123,false);settle();
    assert(model.state().games.size()==3 && model.state().account.cache_available);
    Render renderer(PLAYDEN_HOMEBREW_ASSETS_DIR);
    auto capture=[&](const char* name) {
      for(int i=0;i<16;++i) renderer.draw(model,1920,1080);
      assert(renderer.capture_png(output/name,1920,1080));
    };
    capture("home.png");
    assert(model.action("r1") && model.state().navigation.route==Route::Library);
    capture("library.png");
    // The first Library focus can be its sidebar. Move right into its grid.
    if(model.state().navigation.library_rail_focused) assert(model.action("right"));
    assert(model.action("cross") && model.state().navigation.route==Route::Details);
    capture("details.png");
    model.UpdateAccount({AuthState::Offline,"123","Synthetic account",{},{},true});
    controller.Activate(123,true);settle();
    assert(model.state().games.size()==3 && model.state().library_offline);
    capture("offline.png");
  }
  SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
}
