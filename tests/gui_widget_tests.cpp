#include "MainFrame.h"
#include "Ids.h"
#include <wx/propgrid/propgrid.h>
#include <wx/filename.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <iostream>
#include <cstdlib>
class TestApp:public wxApp {public:bool OnInit()override{wxInitAllImageHandlers();return true;}};
wxIMPLEMENT_APP_NO_MAIN(TestApp);
void pump(){for(int i=0;i<6;++i){wxTheApp->ProcessPendingEvents();wxYield();wxMilliSleep(10);}}
void check(bool v,const char* s){if(!v){std::cerr<<"FAIL "<<s<<"\n";std::exit(1);}std::cout<<"PASS "<<s<<"\n";}
void mouse(se::SchematicCanvas* c,wxEventType kind,wxPoint p,bool down=false){wxMouseEvent e(kind);e.SetPosition(p);e.SetLeftDown(down);c->GetEventHandler()->ProcessEvent(e);pump();}
void click(se::SchematicCanvas*c,wxPoint p){mouse(c,wxEVT_LEFT_DOWN,p,true);mouse(c,wxEVT_LEFT_UP,p);}
void command(se::MainFrame* f,int id){wxCommandEvent e(wxEVT_MENU,id);f->GetEventHandler()->ProcessEvent(e);pump();}
int main(int argc,char**argv){
 if(argc!=3){std::cerr<<"expected examples and output directories\n";return 2;}
 wxString examples=wxString::FromUTF8(argv[1]),out=wxString::FromUTF8(argv[2]);
 int wxargc=1;
 if(!wxEntryStart(wxargc,argv)){std::cerr<<"SKIP: GTK display initialization failed; no GUI checks or screenshots executed\n";return 77;}
 if(!wxTheApp->CallOnInit())return 1;
 auto* frame=new se::MainFrame;frame->Show();pump();auto* canvas=frame->Canvas();
 auto* tree=dynamic_cast<wxTreeCtrl*>(frame->FindWindow(se::ID_LIBRARY_TREE));check(tree,"library tree exists in real wx window");
 wxTreeItemIdValue cookie;auto cat=tree->GetFirstChild(tree->GetRootItem(),cookie);wxTreeItemIdValue leafCookie;auto leaf=tree->GetFirstChild(cat,leafCookie);tree->SelectItem(leaf);pump();
 check(canvas->GetPendingComponent()!=nullptr,"library selection reaches canvas through widget event");
 click(canvas,canvas->ScreenPoint({0,0}));check(canvas->GetComponentCount()==1,"mouse placement inserts component");
 canvas->SetTool(se::SchematicCanvas::ToolSelect);canvas->SetPendingComponent(nullptr);auto original=canvas->GetComponents()[0].pos;
 wxPoint center=canvas->ScreenPoint(original+wxPoint(30,20));mouse(canvas,wxEVT_LEFT_DOWN,center,true);mouse(canvas,wxEVT_MOTION,center+wxPoint(60,40),true);mouse(canvas,wxEVT_LEFT_UP,center+wxPoint(60,40));
 auto moved=canvas->GetComponents()[0].pos;check(moved!=original,"mouse drag moves component");command(frame,wxID_UNDO);check(canvas->GetComponents()[0].pos==original,"menu undo restores original geometry");command(frame,se::ID_EDIT_REDO);check(canvas->GetComponents()[0].pos==moved,"menu redo restores dragged geometry");
 command(frame,se::ID_PLACE_WIRE);auto pin=se::PinPosition(canvas->GetComponents()[0],2);click(canvas,canvas->ScreenPoint(pin));click(canvas,canvas->ScreenPoint(pin+wxPoint(100,0)));canvas->FinishWire();check(canvas->GetWireCount()>0,"wire tool creates a line from a real pin");
 command(frame,se::ID_EDIT_SELECT_ALL);command(frame,wxID_COPY);command(frame,wxID_PASTE);check(canvas->GetComponentCount()==2,"copy/paste menu creates independent component");command(frame,wxID_UNDO);check(canvas->GetComponentCount()==1,"undo reverses paste as one transaction");
 const auto saved=out+"/gui-roundtrip.ses";check(wxCopyFile(examples+"/dynamic-and.ses",saved,true),"prepare private GUI file fixture");check(frame->OpenDocument(saved),"open real design in frame");pump();
 click(canvas,canvas->ScreenPoint(canvas->GetComponents()[0].pos+wxPoint(20,20)));
 auto* grid=dynamic_cast<wxPropertyGrid*>(frame->FindWindow(se::ID_PROPERTY_GRID));check(grid&&grid->ChangePropertyValue("value",wxVariant("1")),"property grid change runs validation and edit callback");pump();check(canvas->GetComponents()[0].value=="1","property edit changes persistent model");command(frame,wxID_SAVE);
 command(frame,se::ID_FILE_CLOSE_PROJECT);check(canvas->GetComponentCount()==0,"close project clears canvas and history");check(frame->OpenDocument(saved)&&canvas->GetComponents()[0].value=="1","reopen restores saved edited value");
 command(frame,se::ID_SIM_STEP);check(canvas->SimulationActive()&&canvas->SimulationTime()==1,"GUI single step initializes and advances live engine");
 auto graph=canvas->GetNetGraph();check(canvas->SimulationValue(graph.pinNet[2][2])==0,"initial GUI simulation output is low");
 click(canvas,canvas->ScreenPoint(canvas->GetComponents()[1].pos+wxPoint(20,20)));command(frame,se::ID_SIM_TOGGLE_INPUT);check(canvas->SimulationValue(graph.pinNet[2][2])==0,"GUI input toggle leaves output unchanged before propagation");command(frame,se::ID_SIM_STEP);check(canvas->SimulationValue(graph.pinNet[2][2])==1,"GUI step propagates changed input to output");
 wxClientDC source(frame);auto size=frame->GetClientSize();wxBitmap snapshot(size.x,size.y);wxMemoryDC target(snapshot);target.Blit(0,0,size.x,size.y,&source,0,0);target.SelectObject(wxNullBitmap);
 check(snapshot.ConvertToImage().SaveFile(out+"/gui-widget-real.png",wxBITMAP_TYPE_PNG),"capture actual displayed test window");
 command(frame,se::ID_SIM_STOP);canvas->MarkSaved();frame->Destroy();pump();wxTheApp->OnExit();wxEntryCleanup();
 std::cout<<"PASS GUI widget integration (native input/file-dialog interaction requires separate end-to-end acceptance)\n";return 0;
}
