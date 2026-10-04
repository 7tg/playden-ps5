#include "library/model.h"
#include "ui/library.h"
#include "ui/shell.h"

#include <RmlUi/Core.h>
#include <rmlfocus/rml_collection.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
class NullRender final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
};

void Check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

}

int main(int argc,char** argv){
 Check(argc==2,"assets root"); NullRender render;Rml::SetRenderInterface(&render);Check(Rml::Initialise(),"initialize");
 Check(Rml::LoadFontFace(std::string(argv[1])+"/fonts/Inter-Regular.ttf"),"font");
 auto* context=Rml::CreateContext("search",{3840,2160});context->SetDensityIndependentPixelRatio(2.f);
 {
  playden::Shell shell(context,argv[1]);shell.SetGames({{10,"Portal"},{20,"A Game"},{30,"A Puzzle"}});Check(shell.Initialize(),"shell");
  auto press=[&](rf::Command command){shell.Handle({command,true,false});shell.Handle({command,false,false});shell.Tick(1.f/60);context->Update();};
  press(rf::Command::NextTab);Check(shell.FocusGame(30),"focus opener game");
  press(rf::Command::Secondary);Check(shell.CurrentRoute()=="search","Triangle opens controller search");
  press(rf::Command::Down);press(rf::Command::Select); // a
  press(rf::Command::Down);press(rf::Command::Down); // Shift
  for(int i=0;i<4;++i)press(rf::Command::Right); // Done
  press(rf::Command::Select);
  Check(shell.CurrentRoute()=="library" && shell.Query()=="a","controller Done commits search and returns");
  Check(shell.LibraryLogicalCount()==3 && shell.SelectedGame()==30,"matching selected identity survives query");
  press(rf::Command::Secondary);press(rf::Command::Select);press(rf::Command::Back);
  Check(shell.CurrentRoute()=="library" && shell.Query()=="a","Cancel retains committed text");
  Check(shell.FocusedKey()=="game-30","Cancel restores exact opener focus");
  press(rf::Command::Secondary);press(rf::Command::Select); // append q
  for(int i=0;i<3;++i)press(rf::Command::Down);
  for(int i=0;i<4;++i)press(rf::Command::Right);
  press(rf::Command::Select);
  Check(shell.CurrentRoute()=="library" && shell.Query()=="aq","empty controller search returns to library");
  Check(shell.LibraryLogicalCount()==0,"no-results state does not invent games");
  // A no-results screen must provide a reachable recovery action.
  auto* clear=shell.Document()->GetElementById("library-status");Check(clear && clear->IsVisible(),"clear search action visible");
  Check(shell.FocusedKey()=="library-status","empty results focus the recovery action");
  press(rf::Command::Select);
  Check(shell.Query().empty() && shell.LibraryLogicalCount()==3,"clear search restores all games");
  shell.Shutdown();
 }
 Rml::RemoveContext("search");Rml::Shutdown();puts("search_test passed");
}
