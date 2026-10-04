#pragma once
#include "app/preferences.h"
#include "ui/shell.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace playden {
class Application {
public:
    Application(Rml::Context* context, std::string assets_root, std::string storage_root);
    ~Application();
    bool Initialize(bool developer_fixtures = false);
    void Handle(const rf::InputEvent& event);
    void Tick(float dt_seconds);
    void Shutdown();
    bool ExitRequested() const;
private:
    void SaveLoop();
    Storage storage_;
    PreferencesStore preferences_store_;
    Shell shell_;
    Preferences preferences_;
    std::thread writer_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_=false, pending_=false, completion_=false;
    std::string diagnostic_;
};
}
