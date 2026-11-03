#include "ComponentCatalog.h"
#include "DigitalSimulator.h"
#include "SchematicIO.h"
#include <wx/filefn.h>
#include <wx/filename.h>
#include <cstdlib>
#include <iostream>
using namespace se;
namespace {
int checks=0;
void check(bool ok,const char* name){
    if(!ok){std::cerr<<"FAIL "<<name<<'\n';std::exit(1);}
    ++checks;std::cout<<"PASS "<<name<<'\n';
}
ComponentInstance component(const wxString& name,int id){
    const auto* def=ComponentCatalog::Find(name);if(!def)std::abort();
    ComponentInstance c;c.id=id;c.name=name;c.value="0";
    c.refdes=def->refPrefix+wxString::Format("%d",id);
    c.pins=MakePins(name,def->kind,def->inputs,def->outputs);return c;
}
NetGraph graph(std::vector<std::vector<int>> pins,int nets){
    NetGraph g;g.pinNet=std::move(pins);g.nets.resize(nets);return g;
}
}
int main(){
    DigitalSimulator sim;wxString error;
    auto a=component(L"输入引脚",1),b=component(L"输入引脚",2);
    auto adder=component(L"半加器",3);adder.delayTicks=2;
    auto g=graph({{0},{1},{0,1,2,3}},4);
    check(sim.Reset({a,b,adder},g,error),"multi-output adder starts");
    sim.Advance(2);check(sim.Value(2)==0&&sim.Value(3)==0,"both adder outputs settle at configured delay");
    sim.SetInput(1,1);check(sim.Value(0)==1&&sim.Value(2)==0,"runtime input changes without restarting the simulation");
    sim.Advance();check(sim.Value(2)==0&&sim.Value(3)==0,"adder retains both outputs before due time");
    sim.Advance();check(sim.Time()==4&&sim.Value(2)==1&&sim.Value(3)==0,"sum changes after two ticks");
    sim.SetInput(2,1);sim.Advance(2);
    check(sim.Value(2)==0&&sim.Value(3)==1,"second live input updates sum and carry");
    sim.SetInput(1,-1);sim.Advance(2);
    check(sim.Value(2)==-1&&sim.Value(3)==-1,"unknown live input propagates to both outputs");
    sim.SetInput(1,0);sim.Advance(2);
    check(sim.Value(2)==1&&sim.Value(3)==0,"known input restores outputs after unknown state");
    const auto before=sim.Values();const auto time=sim.Time();
    check(!sim.SetInput(3,1)&&!sim.SetInput(999,0)&&!sim.SetInput(1,2)&&sim.Values()==before&&sim.Time()==time,"invalid input edits do not mutate runtime state");

    // Reconvergent fanout: the fast branch must change one tick before the slow branch.
    auto fast=component(L"非门",2),slow=component(L"非门",3),join=component(L"异或门",4);
    slow.delayTicks=2;g=graph({{0},{0,1},{0,2},{1,2,3}},4);
    check(sim.Reset({a,fast,slow,join},g,error),"reconvergent fanout starts");sim.Advance(3);
    check(sim.Value(1)==1&&sim.Value(2)==1&&sim.Value(3)==0,"fanout initial state settles through both branches");
    sim.SetInput(1,1);sim.Advance();
    check(sim.Value(1)==0&&sim.Value(2)==1&&sim.Value(3)==0,"only fast branch changes on first propagation tick");
    sim.Advance();check(sim.Value(2)==0&&sim.Value(3)==1,"reconvergent gate exposes one-tick path-delay pulse");
    sim.Advance();check(sim.Value(3)==0,"reconvergent output returns after slow path propagates");
    sim.SetInput(1,0); // Queue old-circuit transitions, then replace the circuit.
    a.value="1";g=graph({{0},{0,1}},2);
    check(sim.Reset({a,fast},g,error)&&sim.Time()==0&&sim.Value(1)==-1,"reset clears time and old circuit events");
    sim.Advance(4);check(sim.Value(1)==0,"new circuit remains independent of queued old events");

    // Competing drivers recover when the runtime input agrees again.
    a.value="0";auto high=component(L"VCC",2);g=graph({{0},{0}},1);
    check(sim.Reset({a,high},g,error)&&sim.Value(0)==2,"conflicting source drivers produce ERR");
    check(sim.SetInput(1,1)&&sim.Value(0)==1,"runtime input resolves driver conflict without reset");
    check(sim.SetInput(1,0)&&sim.Value(0)==2,"later live change can recreate conflict");

    // Persist a self-contained custom gate, then execute it through the same delayed engine.
    a.value="0";b.value="0";auto custom=component(L"与门",3);
    custom.name="PERSISTED_XOR";custom.logicModel="truth";custom.truthTable="0110";custom.delayTicks=3;
    SchematicDocument doc;doc.components={a,b,custom};auto path=wxFileName::CreateTempFileName("se-scenario-");
    check(SchematicIO::Save(path,doc,&error),"custom delayed circuit saves");SchematicDocument reopened;
    check(SchematicIO::Load(path,reopened,&error),"custom delayed circuit reopens");wxRemoveFile(path);
    g=graph({{0},{1},{0,1,2}},3);
    check(sim.Reset(reopened.components,g,error),"reopened custom model starts without library registration");
    sim.Advance(3);check(sim.Value(2)==0,"persisted custom output settles");sim.SetInput(2,1);
    sim.Advance(2);check(sim.Value(2)==0,"persisted custom delay retains previous output for two ticks");
    sim.Advance();check(sim.Value(2)==1,"persisted custom gate dynamically propagates at third tick");
    sim.SetInput(1,1);sim.Advance(3);check(sim.Value(2)==0,"second persisted custom input change updates output again");
    std::cout<<"PASS "<<checks<<" temporal simulation scenario checks\n";
}
