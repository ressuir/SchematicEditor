#include "MainFrame.h"
#include "Ids.h"
#include <wx/uiaction.h>
#include <wx/dcscreen.h>
#include <wx/dcmemory.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/timer.h>
#include <wx/stopwatch.h>
#include <cstdlib>
#include <functional>
#include <iostream>

class NativeTestApp:public wxApp {
public:bool OnInit()override{wxInitAllImageHandlers();return true;}
};
wxIMPLEMENT_APP_NO_MAIN(NativeTestApp);
namespace {
se::MainFrame* frame=nullptr;
wxString artifacts;
wxUIActionSimulator* input=nullptr;
int checks=0;
void pump(int milliseconds=120){
    wxStopWatch watch;
    while(watch.Time()<milliseconds){wxYield();wxTheApp->ProcessPendingEvents();wxMilliSleep(10);}
}
bool screenshot(const wxString& filename){
    if(!frame)return false;
    wxScreenDC screen;const auto r=frame->GetScreenRect();wxBitmap bitmap(r.width,r.height);
    wxMemoryDC memory(bitmap);memory.Blit(0,0,r.width,r.height,&screen,r.x,r.y);
    memory.SelectObject(wxNullBitmap);
    return bitmap.ConvertToImage().SaveFile(artifacts+"/"+filename,wxBITMAP_TYPE_PNG);
}
void check(bool condition,const char* name){
    if(!condition){screenshot("failure.png");std::cerr<<"FAIL "<<name<<'\n';std::exit(1);}
    ++checks;std::cout<<"PASS "<<name<<std::endl;
}
void key(int code,int modifiers=wxMOD_NONE){check(input->Char(code,modifiers),"native keyboard event delivered");pump();}
void click(wxWindow* window,wxPoint client){
    check(input->MouseMove(window->ClientToScreen(client))&&input->MouseClick(),"native mouse click delivered");pump();
}
struct Deferred:wxTimer {
    std::function<void()> action;
    explicit Deferred(std::function<void()> fn):action(std::move(fn)){}
    void Notify()override{action();}
};
// Timers keep working inside wxFileDialog's nested modal event loop. All file
// chooser interaction still uses native Ctrl+L/type/Enter, not Save/Open APIs.
void fileCommand(int modifiers,int letter,const wxString& path){
    bool typed=false;
    Deferred fill([&]{
        typed=input->Char('L',wxMOD_CONTROL)&&input->Char('A',wxMOD_CONTROL)&&input->Text(path.utf8_str())&&input->Char(WXK_RETURN);
    });
    Deferred accept([&]{input->Char(WXK_RETURN);});
    Deferred timeout([]{screenshot("file-dialog-timeout.png");std::cerr<<"FAIL native file dialog timed out\n";std::exit(1);});
    fill.StartOnce(650);accept.StartOnce(1300);timeout.StartOnce(12000);
    check(input->Char(letter,modifiers),"native file command delivered");pump(1600);
    timeout.Stop();fill.Stop();accept.Stop();check(typed,"native file chooser accepted path typing");
}
}
int main(int argc,char** argv){
    if(argc!=3){std::cerr<<"expected examples and output directories\n";return 2;}
    const wxString examples=wxFileName(wxString::FromUTF8(argv[1]),"").GetFullPath();
    artifacts=wxString::FromUTF8(argv[2]);wxFileName::Mkdir(artifacts,wxS_DIR_DEFAULT,wxPATH_MKDIR_FULL);
    int wxargc=1;
    if(!wxEntryStart(wxargc,argv)){std::cerr<<"FAIL native GUI requires an accessible display; no checks executed\n";return 1;}
    if(!wxTheApp->CallOnInit())return 1;
    wxUIActionSimulator simulator;input=&simulator;
    frame=new se::MainFrame;frame->Show();frame->Raise();pump(600);
    auto* canvas=frame->Canvas();auto* tree=dynamic_cast<wxTreeCtrl*>(frame->FindWindow(se::ID_LIBRARY_TREE));
    check(tree,"real library widget exists");
    wxTreeItemIdValue cookie,leafCookie;const auto category=tree->GetFirstChild(tree->GetRootItem(),cookie);
    const auto gateItem=tree->GetFirstChild(category,leafCookie);wxRect itemRect;
    check(tree->GetBoundingRect(gateItem,itemRect,true),"gate row is visible");
    click(tree,itemRect.GetPosition()+wxPoint(12,itemRect.height/2));
    check(canvas->GetPendingComponent()!=nullptr,"native library selection activates placement");
    click(canvas,canvas->ScreenPoint({-160,-80}));
    click(canvas,canvas->ScreenPoint({160,80}));
    check(canvas->GetComponentCount()==2,"native clicks place two gates");key(WXK_ESCAPE);
    const auto original=canvas->GetComponents()[0].pos;
    const auto from=se::PinPosition(canvas->GetComponents()[0],2),to=se::PinPosition(canvas->GetComponents()[1],0);
    key('W');check(canvas->GetTool()==se::SchematicCanvas::ToolWire,"native W shortcut activates wire tool");
    click(canvas,canvas->ScreenPoint(from));click(canvas,canvas->ScreenPoint(to));key(WXK_ESCAPE);
    check(canvas->GetTool()==se::SchematicCanvas::ToolSelect,"native Escape ends wiring and restores selection tool");
    auto connected=[&]{const auto& graph=canvas->GetNetGraph();return graph.pinNet[0][2]==graph.pinNet[1][0];};
    check(canvas->GetWireCount()>0&&connected(),"native wire tool connects output to destination input");
    const auto start=canvas->ScreenPoint(original+wxPoint(30,20));
    check(input->MouseMove(canvas->ClientToScreen(start))&&input->MouseDown(),"native drag begins");pump();
    check(input->MouseMove(canvas->ClientToScreen(start+wxPoint(40,40))),"native drag moves");pump();
    check(input->MouseUp(),"native drag ends");pump();
    const auto moved=canvas->GetComponents()[0].pos;
    check(moved!=original&&connected(),"native drag moves gate while retaining its wire connection");
    key('Z',wxMOD_CONTROL);check(canvas->GetComponents()[0].pos==original&&connected(),"native undo restores geometry and connectivity");
    key('Y',wxMOD_CONTROL);check(canvas->GetComponents()[0].pos==moved&&connected(),"native redo restores moved geometry and connectivity");
    key('Z',wxMOD_CONTROL);
    const wxString saved=wxFileName(artifacts,wxString::Format("native-roundtrip-%ld.ses",wxGetProcessId())).GetFullPath();
    fileCommand(wxMOD_CONTROL|wxMOD_SHIFT,'S',saved);
    check(wxFileExists(saved)&&!canvas->IsModified(),"native save-as dialog writes design and clears dirty state");
    const auto wireCount=canvas->GetWireCount();key('N',wxMOD_CONTROL);
    check(canvas->GetComponentCount()==0,"native new command closes saved design");
    fileCommand(wxMOD_CONTROL,'O',saved);
    check(canvas->GetComponentCount()==2&&canvas->GetWireCount()==wireCount&&canvas->GetComponents()[0].pos==original&&connected(),"native open dialog restores saved components, geometry and connectivity");
    check(screenshot("native-edit-roundtrip.png"),"capture real displayed editing result");
    fileCommand(wxMOD_CONTROL,'O',wxFileName(examples,"dynamic-and.ses").GetFullPath());
    click(canvas,canvas->ScreenPoint(canvas->GetComponents()[0].pos+wxPoint(20,5)));key(WXK_F6);
    const auto output=canvas->GetNetGraph().pinNet[2][2];
    check(canvas->SimulationActive()&&canvas->SimulationValue(output)==0,"native single-step starts engine and settles initial AND output");
    key(WXK_SPACE);click(canvas,canvas->ScreenPoint(canvas->GetComponents()[1].pos+wxPoint(20,20)));key(WXK_SPACE);
    check(canvas->SimulationValue(output)==0,"native input changes retain output until propagation step");
    key(WXK_F6);check(canvas->SimulationValue(output)==1,"native step propagates changed inputs to output");
    const auto paused=canvas->SimulationTime();key(WXK_F5);pump(400);key(WXK_F6);
    check(canvas->SimulationTime()>paused+1&&canvas->SimulationValue(output)==1,"real wx timer advances running simulation");
    check(screenshot("native-live-simulation.png"),"capture real displayed live signal values");
    key(WXK_F5,wxMOD_SHIFT);check(!canvas->SimulationActive()&&!canvas->IsModified(),"native stop preserves saved design initial values");
    frame->Destroy();pump();frame=nullptr;wxTheApp->OnExit();wxEntryCleanup();
    std::cout<<"PASS "<<checks<<" native GUI checks; placement/wiring/drag/undo/redo/save/reopen/live simulation\n";
}
