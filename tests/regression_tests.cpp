#include "ComponentCatalog.h"
#include "EditingTools.h"
#include "KiCadExport.h"
#include "SchematicIO.h"
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <iostream>
#include <set>
using namespace se;
namespace {
int checks=0,failures=0;
void check(bool condition,const char* name){
    ++checks;if(!condition)++failures;
    std::cout<<(condition?"PASS ":"FAIL ")<<name<<'\n';
}
ComponentInstance terminal(int id,wxPoint at){
    ComponentInstance c;c.id=id;c.name=L"输入引脚";c.refdes=wxString::Format("IN%d",id);
    c.pos={at.x+10,at.y-20};c.value="0";c.footprint=DefaultFootprint(1);
    c.pins={{"OUT",PinDir::Output,Side::Left,0,1}};return c;
}
wxString contents(const wxString& path){wxFile file(path);wxString text;file.ReadAll(&text);return text;}
}
int main(){
    wxString error,text;
    auto source=terminal(1,{0,0}),other=terminal(2,{500,0}),label=terminal(3,{0,0});
    label.name=L"网络标签";label.refdes="N3";label.value="N2";label.pins[0].dir=PinDir::Passive;
    std::vector<ComponentInstance> cs={source,other,label};
    auto graph=NetBuilder::Build(cs,{});std::set<wxString> names;
    for(const auto& net:graph.nets)names.insert(net.name);
    check(names.size()==graph.nets.size(),"automatic net names do not collide with explicit N2 label");
    check(graph.pinNet[0][0]!=graph.pinNet[1][0]&&graph.nets[graph.pinNet[0][0]].name=="N2","explicit label retains its name without shorting unrelated source");
    check(BuildKiCadNetlist(cs,graph,text,error),"valid labelled design exports despite reserved automatic-style label");

    SchematicDocument original;original.components={source};
    const auto path=wxFileName::CreateTempFileName("se-regression-");
    check(SchematicIO::Save(path,original,&error),"prepare existing valid design");const auto good=contents(path);
    auto invalid=original;invalid.components[0].refdes.clear();
    check(!SchematicIO::Save(path,invalid,&error)&&!error.empty(),"invalid in-memory document cannot be saved as unreadable design");
    check(contents(path)==good,"failed validation preserves existing file byte for byte");
    SchematicDocument reopened;check(SchematicIO::Load(path,reopened,&error)&&reopened.components.size()==1&&reopened.components[0].refdes=="IN1","existing design remains reopenable after refused save");
    SchematicIO::Save(path,original,&error);invalid=original;invalid.wires={{1,{0,0},{20,20}}};
    check(!SchematicIO::Save(path,invalid,&error)&&contents(path)==good,"diagonal wire is rejected before replacing saved design");
    SchematicIO::Save(path,original,&error);invalid=original;invalid.components[0].pins[0].dir=static_cast<PinDir>(99);
    check(!SchematicIO::Save(path,invalid,&error)&&contents(path)==good,"invalid pin enum cannot silently serialize as a different direction");
    wxRemoveFile(path);

    cs={terminal(1,{0,0}),terminal(2,{0,0}),terminal(3,{100,0})};
    std::vector<Wire> wires={{1,{0,0},{100,0}}};
    MoveConnectedComponents(cs,wires,{0},{40,40});graph=NetBuilder::Build(cs,wires);
    check(PinPosition(cs[0],0)==wxPoint(40,40)&&PinPosition(cs[1],0)==wxPoint(0,0),"drag displaces selected terminal and leaves coincident stationary terminal in place");
    check(graph.pinNet[0][0]==graph.pinNet[1][0]&&graph.pinNet[1][0]==graph.pinNet[2][0],"drag preserves connection to coincident stationary terminal");
    std::set<int> ids;bool orthogonal=true;
    for(const auto& w:wires){ids.insert(w.id);orthogonal&=w.IsAxisAligned()&&w.a!=w.b;}
    check(ids.size()==wires.size()&&orthogonal,"retained stationary connection has unique nonzero orthogonal wires");
    cs={terminal(1,{0,0}),terminal(2,{0,0}),terminal(3,{100,0})};wires={{1,{0,0},{100,0}}};
    MoveConnectedComponents(cs,wires,{0},{0,40});graph=NetBuilder::Build(cs,wires);
    check(wires.size()==2&&graph.pinNet[0][0]==graph.pinNet[1][0]&&graph.pinNet[1][0]==graph.pinNet[2][0],"axial drag retains existing path without duplicate bridge");
    std::cout<<(failures?"FAIL ":"PASS ")<<checks<<" regression checks; failures="<<failures<<'\n';
    return failures?1:0;
}
