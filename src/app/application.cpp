#include "application.h"
#include <cstdio>
#include <utility>
namespace playden {
#ifdef PLAYDEN_DEVELOPER_FIXTURES
std::vector<GameSummary> DeveloperLibraryFixture();
#endif
Application::Application(Rml::Context* context,std::string assets,std::string storage):
 storage_(std::move(storage)),preferences_store_(storage_),shell_(context,std::move(assets)){}
Application::~Application(){Shutdown();}
bool Application::Initialize(bool developer_fixtures){
#ifdef PLAYDEN_DEVELOPER_FIXTURES
    if(developer_fixtures){shell_.SetGames(DeveloperLibraryFixture());shell_.SetDeveloperFixtures(true);}
#else
    if(developer_fixtures){std::fprintf(stderr,"Developer fixtures were not compiled into this build.\n");return false;}
#endif
    // Startup read precedes the frame loop. Writes run on a bounded, coalescing worker.
    auto loaded=preferences_store_.Load();preferences_=loaded.preferences;
    shell_.SetReduceMotion(preferences_.reduce_motion);
    shell_.SetDiagnostic(loaded.diagnostic);
    if(!shell_.Initialize())return false;
    stopping_=false;
    writer_=std::thread(&Application::SaveLoop,this);
    shell_.preference_changed=[this](bool value){
        {std::lock_guard<std::mutex> lock(mutex_);preferences_.reduce_motion=value;pending_=true;}
        shell_.SetDiagnostic("Saving preference...");wake_.notify_one();
    };
    std::printf("preferences loaded reduce_motion=%d status=%d\n",preferences_.reduce_motion,static_cast<int>(loaded.status));
    return true;
}
void Application::SaveLoop(){
    std::unique_lock<std::mutex> lock(mutex_);
    for(;;){
        wake_.wait(lock,[this]{return stopping_||pending_;});
        if(!pending_&&stopping_)break;
        auto value=preferences_;pending_=false;
        lock.unlock();auto saved=preferences_store_.Save(value);lock.lock();
        diagnostic_=saved.ok()?"Preference saved.":"Changes are not saved. Toggle the preference to retry. "+saved.message();
        completion_=true;
        std::printf("preferences save success=%d reduce_motion=%d\n",saved.ok(),value.reduce_motion);
    }
}
void Application::Handle(const rf::InputEvent& event){shell_.Handle(event);}
void Application::Tick(float dt){
    {std::lock_guard<std::mutex> lock(mutex_);if(completion_&&!pending_){shell_.SetDiagnostic(diagnostic_);completion_=false;}}
    shell_.Tick(dt);
}
void Application::Shutdown(){
    shell_.preference_changed={};
    {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}
    wake_.notify_one();if(writer_.joinable())writer_.join();
    shell_.Shutdown();
}
bool Application::ExitRequested() const{return shell_.ExitRequested();}
}
