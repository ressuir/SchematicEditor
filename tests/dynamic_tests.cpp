#include "DigitalSimulator.h"
#include "KiCadExport.h"
#include "ComponentCatalog.h"
#include "SchematicIO.h"
#include <wx/filename.h>
#include <wx/filefn.h>
#include <iostream>
#include <cstdlib>
using namespace se;
int checks=0;
void check(bool ok,const char* name){if(!ok){std::cerr<<"FAIL "<<name<<"\n";std::exit(1);}++checks;std::cout<<"PASS "<<name<<"\n";}
ComponentInstance make(const wxString& name,int id){const auto*d=ComponentCatalog::Find(name);if(!d)std::abort();ComponentInstance c;c.id=id;c.name=name;c.refdes=d->refPrefix+wxString::Format("%d",id);c.w=d->w;c.h=d->h;c.pins=MakePins(name,d->kind,d->inputs,d->outputs);c.footprint=DefaultFootprint(c.pins.size());return c;}
int main(int argc,char**argv){
 wxString error;DigitalSimulator sim;auto input=make(L"输入引脚",1),a=make(L"非门",2),b=make(L"非门",3);input.value="0";
 NetGraph g;g.nets.resize(3);g.pinNet={{0},{0,1},{1,2}};for(int n=0;n<3;++n)g.nets[n].name=wxString::Format("N%d",n+1);
 check(sim.Reset({input,a,b},g,error),"reset two-stage digital circuit");
 check(sim.Time()==0&&sim.Value(0)==0&&sim.Value(1)==-1,"source changes at t0, output has propagation delay");
 sim.Advance();check(sim.Time()==1&&sim.Value(1)==1&&sim.Value(2)==-1,"first gate changes after one tick");
 sim.Advance();check(sim.Value(2)==0,"second gate changes after second tick");
 check(sim.SetInput(1,1)&&sim.Value(0)==1&&sim.Value(1)==1&&sim.Value(2)==0,"live input toggle does not instantly settle gates");
 sim.Advance();check(sim.Value(1)==0&&sim.Value(2)==0,"first propagation step after live toggle");
 sim.Advance();check(sim.Value(2)==1&&sim.TransitionCount()>=5,"live change reaches output through second stage");
 a.delayTicks=3;g.nets.resize(2);g.pinNet={{0},{0,1}};
 check(sim.Reset({input,a},g,error),"gate delay configuration accepted");sim.Advance(3);check(sim.Value(1)==1,"three-tick delay initial settle");
 sim.SetInput(1,1);sim.Advance();sim.SetInput(1,0);sim.Advance(3);check(sim.Value(1)==1,"inertial delay rejects short pulse and cancels stale event");
 auto clock=make(L"时钟",1);clock.clockPeriodTicks=4;a.delayTicks=1;
 check(sim.Reset({clock,a},g,error),"clock circuit starts");sim.Advance();check(sim.Value(0)==0&&sim.Value(1)==1,"clock low phase propagates");
 sim.Advance();check(sim.Value(0)==1&&sim.Value(1)==1,"clock rising edge precedes output event");sim.Advance();check(sim.Value(1)==0,"clock edge propagates dynamically");sim.Advance();check(sim.Value(0)==0,"clock falling edge occurs at full period");
 auto high=make(L"VCC",2);g.pinNet={{0},{0}};g.nets.resize(1);check(sim.Reset({input,high},g,error)&&sim.Value(0)==2,"different driven sources produce ERR");
 auto resistor=make(L"电阻",1);g=NetBuilder::Build({resistor},{});check(!sim.Reset({resistor},g,error)&&!sim.Ready(),"unsupported analog device is rejected explicitly");
 auto custom=make(L"与门",5);custom.name="CUSTOM_XOR";custom.logicModel="truth";custom.truthTable="0110";custom.delayTicks=2;
 check(EvaluateDigital(custom,{1,0})==std::vector<int>{1}&&EvaluateDigital(custom,{1,1})==std::vector<int>{0},"self-contained custom truth model evaluates");
 SchematicDocument doc;doc.components={custom};doc.components[0].pins[0].number="8";doc.components[0].clockPeriodTicks=6;
 auto path=wxFileName::CreateTempFileName("se-dynamic-");check(SchematicIO::Save(path,doc,&error),"save new simulation metadata");SchematicDocument loaded;
 check(SchematicIO::Load(path,loaded,&error)&&loaded.components[0].logicModel=="truth"&&loaded.components[0].truthTable=="0110"&&loaded.components[0].delayTicks==2&&loaded.components[0].clockPeriodTicks==6&&loaded.components[0].pins[0].number=="8","simulation models, delays and PCB pad numbers survive reopening");
 auto xml=wxString::FromUTF8("<schematic><components><component id=\"1\" name=\"X\" refdes=\"U1\"><pin name=\"A\" dir=\"oops\"/></component></components></schematic>");
 SchematicIO::WriteTextFile(path,xml,&error);check(!SchematicIO::Load(path,loaded,&error)&&loaded.components.size()==1,"invalid pin direction rejected without replacing current document");
 SchematicIO::WriteTextFile(path,"<schematic><wires><wire id=\"1\" x2=\"10\" y2=\"10\"/></wires></schematic>",&error);check(!SchematicIO::Load(path,loaded,&error),"non-orthogonal file wire is rejected");wxRemoveFile(path);
 wxString text;g=NetBuilder::Build(doc.components,{});
 check(BuildKiCadNetlist(doc.components,g,text,error)&&text.StartsWith("(export (version E)")&&text.Contains("(pin \"8\")")&&text.Contains("(footprint \"Connector_PinHeader"),"KiCad exporter emits numeric pad mapping, footprint and native structure (not PCB import verification)");
 auto duplicate=doc.components;duplicate.push_back(duplicate[0]);check(!BuildKiCadNetlist(duplicate,NetBuilder::Build(duplicate,{}),text,error),"duplicate PCB reference rejected atomically");
 doc.components[0].footprint.clear();check(!BuildKiCadNetlist(doc.components,g,text,error)&&text.empty(),"missing footprint cannot silently produce incomplete PCB netlist");
 // Actual geometry: two editable inputs connected to an AND gate.
 auto x=make(L"输入引脚",1),y=make(L"输入引脚",2),gate=make(L"与门",3);gate.pos={200,0};x.h=y.h=40;x.pos={10,PinPosition(gate,0).y-20};y.pos={10,PinPosition(gate,1).y-20};x.value=y.value="0";
 doc.components={x,y,gate};doc.wires={{1,PinPosition(x,0),PinPosition(gate,0)},{2,PinPosition(y,0),PinPosition(gate,1)}};g=NetBuilder::Build(doc.components,doc.wires);int out=g.pinNet[2][2];
 check(sim.Reset(doc.components,g,error),"geometry-derived network starts dynamic engine");sim.Advance(2);check(sim.Value(out)==0,"geometry circuit initial output");sim.SetInput(1,1);sim.SetInput(2,1);check(sim.Value(out)==0,"geometry circuit retains pre-delay output");sim.Advance();check(sim.Value(out)==1,"geometry circuit input changes reach output after delay");
 if(argc>1){wxString dir=wxString::FromUTF8(argv[1]);check(SchematicIO::Save(dir+"/dynamic-and.ses",doc,&error),"write runnable dynamic example");check(BuildKiCadNetlist(doc.components,g,text,error)&&SchematicIO::WriteTextFile(dir+"/dynamic-and.net",text,&error),"write actual KiCad-format export fixture");
  doc.components[0]=clock;doc.components[0].pos=x.pos;doc.components[0].h=40;doc.wires[0].a=PinPosition(doc.components[0],0);doc.components[1].value="1";check(SchematicIO::Save(dir+"/clock-and.ses",doc,&error),"write live clock example");}
 std::cout<<"PASS "<<checks<<" dynamic/persistence/export checks\n";
}
