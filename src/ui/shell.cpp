#include "shell.h"
#include <rmlfocus/rml_collection.h>
#include <rmlfocus/rml_image.h>
#include <rmlfocus/rml_keyboard.h>
#include <unordered_map>
#include <utility>

namespace playden {
Shell::Shell(Rml::Context* context, std::string assets):context_(context),assets_root_(std::move(assets)) {}
Shell::~Shell(){ Shutdown(); }

bool Shell::Initialize(){
    Shutdown(); exit_requested_=false;
    rf::RegisterCollections(); rf::RegisterImageElement(images_); rf::RegisterKeyboard();
    router_=std::make_unique<rf::RmlRouter>(context_,[this](const rf::Route& route){return LoadRoute(route);});
    router_->SetReduceMotion(reduce_motion_);
    router_->exit_request=[this]{router_->Confirmation("exit","exit.rml","cancel");};
    router_->changed=[this](const rf::NavigationResult& result){
        if(CurrentRoute()=="library") {
            library_.Bind(Document());
            if(result.action==rf::NavigationAction::Pop&&visible_games_.empty()&&router_->CurrentFocus())router_->CurrentFocus()->Set("library-status");
        }
        else library_.Bind(nullptr);
        ConfigureCollectionNavigation();Refresh();
    };
    const std::string library_focus=visible_games_.empty()?"library-status":LibraryView::FocusKey(visible_games_.front().id);
    if(!router_->SetTabs({
        {"home-tab",{"home","home.rml","open-library"}},
        {"library-tab",{"library","library.rml",library_focus}},
        {"settings-tab",{"settings","settings.rml","reduce-motion"}}
    })||!router_->OpenRoot({"home","home.rml","open-library"})){Shutdown();return false;}
    Refresh();return true;
}

Rml::ElementDocument* Shell::LoadRoute(const rf::Route& route){
    auto* document=context_->LoadDocument(assets_root_+"/ui/"+route.document);
    if(!document)return nullptr;
    document->AddEventListener(Rml::EventId::Click,this);
    for (const char* name : {"rf:keyboardchange", "rf:keyboarddone", "rf:keyboardcancel"}) document->AddEventListener(name,this);
    Populate(document,route);
    return document;
}

void Shell::Populate(Rml::ElementDocument* document,const rf::Route& route){
    if(route.key=="library"){library_.Bind(document);library_.SetGames(visible_games_);}
    else if(route.key=="search") {
        if(auto* keyboard=document->GetElementById("search-keyboard")) keyboard->SetAttribute("value",query_text_);
        if(auto* value=document->GetElementById("search-value")) value->AppendChild(document->CreateTextNode(query_text_.empty()?"Enter a title":query_text_));
    }
    else if(route.key=="details")details_.Populate(document,library_.Find(selected_game_),install_fixture_.Enabled());
    else if(route.key=="install")install_fixture_.PopulateInstall(document);
    else if(route.key=="confirmation")install_fixture_.PopulateConfirmation(document);
    else if(route.key=="queue")install_fixture_.PopulateQueue(document);
    if(route.key=="library")
        if(auto* notice=document->GetElementById("library-fixture"))
            notice->SetProperty("display",install_fixture_.Enabled()?"block":"none");
}

void Shell::Handle(const rf::InputEvent& event){
    if(!router_) return;
    if(CurrentRoute()=="search" && rf::HandleKeyboardCommand(context_->GetFocusElement(),event)) return;
    if(CurrentRoute()=="library" && event.command==rf::Command::Secondary && event.pressed && !event.repeat){OpenSearch();return;}
    router_->HandleCommand(event);
}
void Shell::OpenSearch(){if(router_)router_->Push({"search","search.rml","search-keyboard-text-0-0"});}
void Shell::SetQuery(const std::string& text){query_text_=text;ApplyQuery();Refresh();}
void Shell::ApplyQuery(){
    const auto ids=QueryLibrary(games_,{query_text_},LibrarySort::AppId);
    std::unordered_map<GameId,const GameSummary*> by_id;
    for(const auto& game:games_)by_id.emplace(game.id,&game);
    visible_games_.clear();visible_games_.reserve(ids.size());
    for(GameId id:ids)visible_games_.push_back(*by_id.at(id));
    library_.SetGames(visible_games_);
    if(router_ && CurrentRoute()=="library" && router_->CurrentFocus()) {
        if(visible_games_.empty())router_->CurrentFocus()->Set("library-status");
        else FocusGame(library_.SelectedGame()?library_.SelectedGame():visible_games_.front().id);
    }
}
void Shell::Tick(float dt){
    images_.Pump(16);install_fixture_.Tick(dt);
    if(router_){router_->Update(dt);ConfigureCollectionNavigation();if(CurrentRoute()=="queue")install_fixture_.PopulateQueue(Document());}
}
void Shell::Shutdown(){
    if(!router_)return;
    library_.Bind(nullptr);router_.reset();
    // RmlUi defers document destruction. Drain it while this event listener is alive.
    // Shell must be shut down before the platform removes its context.
    context_->Update();
}
void Shell::SetReduceMotion(bool enabled){
    reduce_motion_=enabled;rf::SetCollectionsReduceMotion(enabled);rf::SetImagesReduceMotion(enabled);
    if(router_)router_->SetReduceMotion(enabled);Refresh();
}
void Shell::SetDiagnostic(const std::string& value){diagnostic_=value;Refresh();}
void Shell::SetGames(const std::vector<GameSummary>& games){
    games_=games;ApplyQuery();
    if(router_){
        const std::string library_focus=visible_games_.empty()?"library-status":LibraryView::FocusKey(visible_games_.front().id);
        router_->SetTabs({
            {"home-tab",{"home","home.rml","open-library"}},
            {"library-tab",{"library","library.rml",library_focus}},
            {"settings-tab",{"settings","settings.rml","reduce-motion"}}
        });
        if(CurrentRoute()=="library"&&!visible_games_.empty()&&FocusedKey().compare(0,5,"game-")!=0)
            FocusGame(library_.SelectedGame()?library_.SelectedGame():visible_games_.front().id);
    }
}
void Shell::SetDeveloperFixtures(bool enabled){
    install_fixture_.SetEnabled(enabled);
    if(CurrentRoute()=="library")
        if(auto* notice=Document()->GetElementById("library-fixture"))notice->SetProperty("display",enabled?"block":"none");
}
bool Shell::FocusGame(GameId id){
    if(CurrentRoute()!="library"||!library_.FocusGame(id))return false;
    if(router_&&router_->CurrentFocus())router_->CurrentFocus()->Set(LibraryView::FocusKey(id));
    return true;
}
GameId Shell::SelectedGame() const{return CurrentRoute()=="library"?library_.SelectedGame():selected_game_;}
std::size_t Shell::LibraryLogicalCount() const{return library_.LogicalCount();}
std::size_t Shell::LibraryMaterializedCount() const{return library_.MaterializedCount();}
float Shell::LibraryScrollTarget() const{return library_.ScrollTarget();}
rf::ImageStats Shell::ImageStats() const{return images_.Stats();}
std::string Shell::CurrentRoute() const{return router_?router_->Model().Current().key:"";}
std::string Shell::FocusedKey() const{return router_&&router_->CurrentFocus()?router_->CurrentFocus()->Current():"";}
Rml::ElementDocument* Shell::Document() const{return router_?router_->CurrentDocument():nullptr;}

void Shell::ConfigureCollectionNavigation(){
    if(!router_||!router_->CurrentFocus())return;
    router_->CurrentFocus()->container_move=[](Rml::Element* source,rf::Direction direction){
        for(auto* element=source;element;element=element->GetParentNode())
            if(auto* collection=dynamic_cast<rf::CollectionElement*>(element))return collection->Move(direction);
        return false;
    };
}

void Shell::Refresh(){
    auto* doc=Document();if(!doc)return;
    if(CurrentRoute()=="library"){
        if(auto* label=doc->GetElementById("library-empty-message")){
            label->SetInnerRML("");label->AppendChild(doc->CreateTextNode(query_text_.empty()?"Your library is empty. Connect Steam to load your games.":"No games match your search. Change or clear the search to try again."));
        }
        if(auto* button=doc->GetElementById("library-status")){
            button->SetInnerRML("");button->AppendChild(doc->CreateTextNode(query_text_.empty()?"Connection status":"Clear search"));
        }
        if(auto* label=doc->GetElementById("search-button")){
            label->SetInnerRML("");label->AppendChild(doc->CreateTextNode(query_text_.empty()?"Search":"Search: "+query_text_));
        }
    }
    if(auto* label=doc->GetElementById("motion-value"))label->SetInnerRML(reduce_motion_?"On":"Off");
    if(auto* label=doc->GetElementById("diagnostic")){label->SetInnerRML("");label->AppendChild(doc->CreateTextNode(diagnostic_));}
}

std::string Shell::EventKey(Rml::Element* element){
    for(;element;element=element->GetParentNode()){
        const std::string focus=element->GetAttribute<Rml::String>("focus-key","");
        if(!focus.empty())return focus;
        if(!element->GetId().empty())return element->GetId();
    }
    return {};
}

void Shell::ProcessEvent(Rml::Event& event){
    if(!router_)return;
    auto* target=event.GetTargetElement();if(!target||target->GetOwnerDocument()!=Document())return;
    if(event.GetType()=="rf:keyboardchange"){
        if(auto* label=Document()->GetElementById("search-value")){label->SetInnerRML("");label->AppendChild(Document()->CreateTextNode(event.GetParameter<Rml::String>("value","")));}
        return;
    }
    if(event.GetType()=="rf:keyboarddone"){SetQuery(event.GetParameter<Rml::String>("value",""));router_->Back();return;}
    if(event.GetType()=="rf:keyboardcancel"){router_->Back();return;}
    const std::string id=EventKey(target);
    if(id=="home-tab"||id=="library-tab"||id=="settings-tab")router_->SelectTab(id);
    else if(id=="open-library")router_->SelectTab("library-tab");
    else if(id=="search-button")OpenSearch();
    else if(id=="library-status"&&!query_text_.empty()){SetQuery("");if(!visible_games_.empty())FocusGame(visible_games_.front().id);}
    else if(id=="library-status"||id=="about")router_->Sheet("about","about.rml","dismiss");
    else if(id=="dismiss"||id=="cancel"||id=="details-back"||id=="queue-close")router_->Back();
    else if(id=="exit-confirm")exit_requested_=true;
    else if(id=="exit-button")router_->Confirmation("exit","exit.rml","cancel");
    else if(id=="reduce-motion"){SetReduceMotion(!reduce_motion_);if(preference_changed)preference_changed(reduce_motion_);}
    else if(id.compare(0,5,"game-")==0){
        selected_game_=library_.SelectedGame();if(!selected_game_)return;
        router_->Push({"details","details.rml",install_fixture_.Enabled()?"details-install":"details-back"});
    }else if(id=="details-install"&&install_fixture_.Enabled()){
        if(const GameSummary* game=library_.Find(selected_game_))install_fixture_.Begin(game->id,game->title);
        router_->Sheet("install","install.rml","install-continue");
    }else if(id=="install-continue"&&install_fixture_.Enabled())router_->Confirmation("confirmation","confirmation.rml","confirm-start");
    else if(id=="confirm-start"&&install_fixture_.Enabled()){install_fixture_.Confirm();router_->Dismiss();install_fixture_.PopulateInstall(Document());}
    else if(id=="view-queue"&&install_fixture_.Enabled())router_->Sheet("queue","queue.rml","queue-close");
}
}
