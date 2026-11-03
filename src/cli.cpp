#include "DigitalSimulator.h"
#include "SchematicIO.h"
#include "KiCadExport.h"
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#ifndef SE_SOURCE_REV
#define SE_SOURCE_REV "unversioned"
#endif
namespace {
std::string utf8(const wxString& s){return std::string(s.utf8_str());}
std::string csv(const wxString& s){std::string value=utf8(s),out="\"";for(char c:value){if(c=='\"')out+='\"';out+=c;}return out+'\"';}
}
int main(int argc,char**argv){
 using namespace se;
 try {
    if(argc==2&&std::string(argv[1])=="--version"){std::cout<<"SchematicEditor 2.0 source "<<SE_SOURCE_REV<<"\n";return 0;}
    if(argc<3){std::cerr<<"Usage:\n  schematic-cli inspect design.ses\n  schematic-cli simulate design.ses ticks [tick:id:0|1|X ...]\n  schematic-cli export-kicad design.ses output.net\n  schematic-cli roundtrip design.ses output.ses\n";return 2;}
    SchematicDocument doc;wxString error;
    if(!SchematicIO::Load(wxString::FromUTF8(argv[2]),doc,&error))throw std::runtime_error(utf8(error));
    const auto graph=NetBuilder::Build(doc.components,doc.wires);const std::string action=argv[1];
    if(action=="inspect"){
        std::cout<<"components="<<doc.components.size()<<" wires="<<doc.wires.size()<<" pins="<<doc.PinCount()<<" nets="<<graph.nets.size()<<"\n";return 0;
    }
    if((action=="export-kicad"||action=="roundtrip")&&argc==4){
        bool ok=false;
        if(action=="roundtrip")ok=SchematicIO::Save(wxString::FromUTF8(argv[3]),doc,&error);
        else {wxString text;ok=BuildKiCadNetlist(doc.components,graph,text,error)&&SchematicIO::WriteTextFile(wxString::FromUTF8(argv[3]),text,&error);}
        if(!ok)throw std::runtime_error(utf8(error));return 0;
    }
    if(action!="simulate"||argc<4)throw std::runtime_error("invalid arguments");
    size_t used=0;std::string count=argv[3];unsigned long long steps=std::stoull(count,&used);
    if(used!=count.size()||count.empty()||count[0]=='-'||steps>100000)throw std::runtime_error("ticks must be 0..100000");
    struct Change{int id,value;};std::multimap<unsigned long long,Change> changes;
    for(int i=4;i<argc;++i){std::string raw=argv[i];if(std::count(raw.begin(),raw.end(),':')!=2)throw std::runtime_error("invalid event fields");std::istringstream spec(argv[i]);std::string t,id,v,extra;std::getline(spec,t,':');std::getline(spec,id,':');std::getline(spec,v,':');
        if(t.empty()||id.empty()||(v!="0"&&v!="1"&&v!="X")||std::getline(spec,extra,':'))throw std::runtime_error("event must be tick:id:0|1|X");
        size_t n=0;auto at=std::stoull(t,&n);if(n!=t.size()||t[0]=='-'||at>steps)throw std::runtime_error("event time out of range");
        size_t m=0;int ci=std::stoi(id,&m);if(m!=id.size()||ci<=0)throw std::runtime_error("invalid component id");
        changes.emplace(at,Change{ci,v=="X"?-1:v=="1"?1:0});
    }
    DigitalSimulator sim;if(!sim.Reset(doc.components,graph,error))throw std::runtime_error(utf8(error));
    std::cout<<"tick";for(const auto& n:graph.nets)std::cout<<","<<csv(n.name);std::cout<<"\n";
    for(unsigned long long t=0;t<=steps;++t){
        if(t&&!sim.Advance())throw std::runtime_error(utf8(sim.Error()));
        auto range=changes.equal_range(t);for(auto i=range.first;i!=range.second;++i)if(!sim.SetInput(i->second.id,i->second.value))throw std::runtime_error("event targets a non-input component");
        std::cout<<sim.Time();for(int v:sim.Values())std::cout<<","<<utf8(LogicValueText(v));std::cout<<"\n";
    }
    return 0;
 }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}
}
