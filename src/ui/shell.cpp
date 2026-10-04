#include "shell.h"
#include <utility>
namespace playden {
Shell::Shell(Rml::Context* context, std::string assets):context_(context),assets_root_(std::move(assets)) {}
Shell::~Shell(){ Shutdown(); }
bool Shell::Initialize(){
    Shutdown(); exit_requested_=false;
    router_=std::make_unique<rf::RmlRouter>(context_,[this](const rf::Route& route){
        auto* doc=context_->LoadDocument(assets_root_+"/ui/"+route.document);
        if(doc) doc->AddEventListener(Rml::EventId::Click,this);
        return doc;
    });
    router_->SetReduceMotion(reduce_motion_);
    router_->exit_request=[this]{router_->Confirmation("exit","exit.rml","cancel");};
    router_->changed=[this](const rf::NavigationResult&){Refresh();};
    if(!router_->SetTabs({
        {"home-tab",{"home","home.rml","open-library"}},
        {"library-tab",{"library","library.rml","library-status"}},
        {"settings-tab",{"settings","settings.rml","reduce-motion"}}
    }) || !router_->OpenRoot({"home","home.rml","open-library"})) {Shutdown();return false;}
    Refresh(); return true;
}
void Shell::Handle(const rf::InputEvent& event){if(router_) router_->HandleCommand(event);}
void Shell::Tick(float dt){if(router_) router_->Update(dt);}
void Shell::Shutdown(){
    if (!router_) return;
    router_.reset();
    // RmlUi defers document destruction. Drain it while this event listener is alive.
    // Shell must be shut down before the platform removes its context.
    context_->Update();
}
void Shell::SetReduceMotion(bool enabled){reduce_motion_=enabled;if(router_)router_->SetReduceMotion(enabled);Refresh();}
void Shell::SetDiagnostic(const std::string& value){diagnostic_=value;Refresh();}
std::string Shell::CurrentRoute() const{return router_?router_->Model().Current().key:"";}
std::string Shell::FocusedKey() const{return router_&&router_->CurrentFocus()?router_->CurrentFocus()->Current():"";}
Rml::ElementDocument* Shell::Document() const{return router_?router_->CurrentDocument():nullptr;}
void Shell::Refresh(){
    auto* doc=Document();if(!doc)return;
    if(auto* label=doc->GetElementById("motion-value"))label->SetInnerRML(reduce_motion_?"On":"Off");
    if(auto* label=doc->GetElementById("diagnostic")){
        // Diagnostics are text, never markup from an adapter or a filesystem error.
        label->SetInnerRML("");
        label->AppendChild(doc->CreateTextNode(diagnostic_));
    }
}
void Shell::ProcessEvent(Rml::Event& event){
    auto* element=event.GetTargetElement();
    while(element && element->GetTagName()!="button")element=element->GetParentNode();
    if(!element || !router_ || element->GetOwnerDocument()!=Document())return;
    const auto id=element->GetId();
    if(id=="home-tab"||id=="library-tab"||id=="settings-tab")router_->SelectTab(id);
    else if(id=="open-library")router_->SelectTab("library-tab");
    else if(id=="library-status"||id=="about")router_->Sheet("about","about.rml","dismiss");
    else if(id=="dismiss"||id=="cancel")router_->Dismiss();
    else if(id=="exit-confirm")exit_requested_=true;
    else if(id=="exit-button")router_->Confirmation("exit","exit.rml","cancel");
    else if(id=="reduce-motion"){
        SetReduceMotion(!reduce_motion_);
        if(preference_changed)preference_changed(reduce_motion_);
    }
}
}
