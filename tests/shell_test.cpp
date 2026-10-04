#include "ui/shell.h"
#include "app/application.h"
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

class NullRender final : public Rml::RenderInterface {
public:
 Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,Rml::Span<const int>) override{return ++id;}
 void RenderGeometry(Rml::CompiledGeometryHandle,Rml::Vector2f,Rml::TextureHandle) override{}
 void ReleaseGeometry(Rml::CompiledGeometryHandle) override{}
 void EnableScissorRegion(bool) override{}
 void SetScissorRegion(Rml::Rectanglei) override{}
 Rml::TextureHandle LoadTexture(Rml::Vector2i&,const Rml::String&) override{return 0;}
 Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,Rml::Vector2i) override{return ++id;}
 void ReleaseTexture(Rml::TextureHandle) override{}
 uintptr_t id=0;
};
static void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(int argc,char** argv){
 Check(argc==2,"asset root argument");
 NullRender render; Rml::SetRenderInterface(&render); Check(Rml::Initialise(),"Rml initialise");
 Check(Rml::LoadFontFace(std::string(argv[1])+"/fonts/Inter-Regular.ttf"),"test font loads");
 auto* context=Rml::CreateContext("shell-test",{1920,1080});
 {
 playden::Shell shell(context,argv[1]);
 Check(shell.Initialize(),"shell opens Home");
 shell.SetReduceMotion(true);
 auto tick=[&]{shell.Tick(1.f/60);context->Update();};
 auto press=[&](rf::Command c){shell.Handle({c,true,false});shell.Handle({c,false,false});tick();};
 tick();
 Check(shell.CurrentRoute()=="home","starts on Home");
 auto* title=shell.Document()->GetElementById("page-title");
 auto* intro=shell.Document()->GetElementById("page-intro");
 Check(title && intro && intro->GetAbsoluteOffset().y >= title->GetAbsoluteOffset().y + title->GetBox().GetSize(Rml::BoxArea::Border).y,"intro lays out below title without overlap");
 Check(shell.FocusedKey()=="open-library","Home has initial focus");
 Check(shell.Document()->GetElementById("home-tab")->IsClassSet("selected"),"selected section marked independently of content focus");
 press(rf::Command::Right);Check(shell.FocusedKey()=="about","D-pad moves between Home actions");
 press(rf::Command::Left);Check(shell.FocusedKey()=="open-library","D-pad returns to initial action");
 press(rf::Command::NextTab);Check(shell.CurrentRoute()=="library","R1 opens Library");
 press(rf::Command::NextTab);Check(shell.CurrentRoute()=="settings","R1 opens Settings");
 bool changed=false; shell.preference_changed=[&](bool value){changed=!value;};
 press(rf::Command::Select);Check(changed,"Cross changes Reduce motion preference");
 press(rf::Command::PreviousTab);Check(shell.CurrentRoute()=="library","L1 returns Library");
 press(rf::Command::Select);Check(shell.CurrentRoute()=="about","Library status opens sheet");
 Check(shell.FocusedKey()=="dismiss","sheet owns focus");
 press(rf::Command::NextTab);Check(shell.CurrentRoute()=="about","sheet traps tab navigation");
 press(rf::Command::Back);Check(shell.CurrentRoute()=="library","Circle dismisses one layer");
 Check(shell.FocusedKey()=="library-status","dismiss restores exact opener");
 shell.SetDiagnostic("Preferences were reset because the file is corrupt.");tick();
 Check(shell.Document()->GetElementById("diagnostic")->GetInnerRML().find("corrupt")!=std::string::npos,"storage diagnostic visible");
 press(rf::Command::Back);Check(shell.FocusedKey()=="library-tab","Circle returns to selected tab bar");
 press(rf::Command::Back);Check(shell.CurrentRoute()=="exit","root Back asks to exit");
 press(rf::Command::Back);Check(!shell.ExitRequested(),"cancel exit keeps shell running");
 Check(shell.FocusedKey()=="library-tab","exit cancel restores tab opener");
 shell.Shutdown();context->Update();Check(context->GetNumDocuments()==0,"shutdown closes every document");
 }
 {
 auto owned_shell=std::make_unique<playden::Shell>(context,argv[1]);
 Check(owned_shell->Initialize(),"owned shell starts");
 owned_shell.reset(); // Caller must not need to flush deferred documents before destroying Shell.
 context->Update();
 Check(context->GetNumDocuments()==0,"destroying Shell drains its documents safely");
 }
 char root[]="/tmp/playden-app-test-XXXXXX";
 Check(mkdtemp(root)!=nullptr,"temporary preferences root");
 {
 playden::Application app(context,argv[1],root);Check(app.Initialize(),"application initialises");
 for(int i=0;i<2;++i)app.Handle({rf::Command::NextTab,true,false});
 app.Handle({rf::Command::Select,true,false});app.Handle({rf::Command::Select,false,false});
 app.Shutdown();context->Update();
 }
 {
 playden::Application app(context,argv[1],root);Check(app.Initialize(),"application restarts");
 for(int i=0;i<2;++i)app.Handle({rf::Command::NextTab,true,false});
 app.Tick(1.f/60);context->Update();
 auto* doc=context->GetDocument("rf-route-settings");
 Check(doc && doc->GetElementById("motion-value")->GetInnerRML()=="On","preference survives app shutdown and restart");
 app.Shutdown();context->Update();
 }
 auto path=std::string(root)+"/preferences.conf";
 auto* corrupt=std::fopen(path.c_str(),"wb");Check(corrupt!=nullptr,"corruption fixture opens");std::fputs("broken",corrupt);std::fclose(corrupt);
 {
 playden::Application app(context,argv[1],root);Check(app.Initialize(),"corruption permits launch");app.Tick(1.f/60);context->Update();
 auto* doc=context->GetDocument("rf-route-home");
 Check(doc && doc->GetElementById("diagnostic")->GetInnerRML().find("corrupt")!=std::string::npos,"corrupt preference visibly reported by application");
 app.Shutdown();context->Update();
 }
 std::remove(path.c_str());rmdir(root);
 Rml::RemoveContext("shell-test");Rml::Shutdown();puts("shell_test passed");
}
