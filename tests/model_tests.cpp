#include "CircuitTools.h"
#include "ComponentCatalog.h"
#include "SchematicIO.h"
#include <wx/filename.h>
#include <wx/filefn.h>
#include <iostream>
#include <cstdlib>
using namespace se;
int passed=0;
void check(bool v,const char* name){if(!v){std::cerr<<"FAIL "<<name<<"\n";std::exit(1);}++passed;}
ComponentInstance make(const wxString& name,int id=1){auto*def=ComponentCatalog::Find(name);check(def,"catalog lookup");ComponentInstance c;c.id=id;c.name=name;c.refdes=def->refPrefix+wxString::Format("%d",id);c.w=def->w;c.h=def->h;c.pins=MakePins(name,def->kind,def->inputs,def->outputs);return c;}
ComponentInstance pin(int id,int x,int y){auto c=make(L"输入引脚",id);c.pos={x+10,y-20};c.h=40;c.pins={{"A",PinDir::Passive,Side::Left,0,1}};return c;}
SimulationResult truth(const wxString& gate,int n,int mask,int overrideIndex=-1){
 std::vector<ComponentInstance> c;NetGraph g;
 for(int i=0;i<n;i++){auto s=make(L"输入引脚",i+1);s.value=i==overrideIndex?"X":((mask>>i)&1)?"1":"0";c.push_back(s);g.pinNet.push_back({i});}
 auto d=make(gate,n+1);c.push_back(d);std::vector<int> mapping;for(size_t k=0;k<d.pins.size();k++)mapping.push_back((int)k);g.pinNet.push_back(mapping);
 g.nets.resize(d.pins.size());for(size_t k=0;k<g.nets.size();k++)g.nets[k].name=wxString::Format("N%d",(int)k);
 return SimulateDigital(c,g);
}
int main(int argc,char**argv){
 check(argc==2,"sample path supplied"); SchematicDocument d;wxString error;
 check(SchematicIO::Load(wxString::FromUTF8(argv[1]),d,&error),"load sample");
 check(d.components.size()==3&&d.wires.size()==5&&d.PinCount()==9,"sample counts");
 auto g=NetBuilder::Build(d.components,d.wires);
 check(g.nets.size()==7&&g.junctions.size()==1,"sample nets and junction");
 check(g.pinNet[0][2]==g.pinNet[1][0]&&g.pinNet[0][2]==g.pinNet[2][0],"T sample topology");
 check(NetBuilder::RunErc(d.components,d.wires,g).Contains(L"共发现 7 处"),"sample ERC issues");
 d.components[0].value=L"测试 10k & <x>";d.components[0].footprint="DIP-14";
 const auto path=wxFileName::CreateTempFileName("schematic-test-");
 check(SchematicIO::Save(path,d,&error),"save XML");SchematicDocument r;
 check(SchematicIO::Load(path,r,&error),"reload XML");wxRemoveFile(path);
 check(r.components[0].value==d.components[0].value&&r.components[0].footprint=="DIP-14"&&r.wires.size()==5,"new attributes roundtrip with XML escaping");
 check(ComponentCatalog::ComponentCount()==26&&ComponentCatalog::Categories().size()==7,"catalog counts");
 std::vector<ComponentInstance> refs={make(L"与门"),make(L"或门"),make(L"非门")};
 AnnotateComponents(refs);check(refs[0].refdes=="U1"&&refs[1].refdes=="U2"&&refs[2].refdes=="U3","shared prefix annotation");
 check(NextReference(refs,L"异或门")=="U4","shared prefix next reference");
 std::vector<Wire> wires={{1,{0,0},{10,0}}};auto copied=std::vector<ComponentInstance>{refs[0]};auto copiedW=wires;
 AppendCopies(refs,wires,copied,copiedW,{20,20});
 check(refs.size()==4&&refs[3].refdes=="U4"&&refs[3].id==2&&refs[3].pos==wxPoint(20,20),"paste independent ID reference and translation");
 check(wires[1].id==2&&wires[1].a==wxPoint(20,20)&&wires[1].b==wxPoint(30,20),"paste wire translation");
 check(BuildBom(refs).Contains("\"U4\"")&&BuildBom(refs).Contains(L"参数"),"BOM content");
 std::vector<ComponentInstance> cp={pin(1,-10,0),pin(2,10,0),pin(3,0,-10),pin(4,0,10)};
 std::vector<Wire> cw={{1,{-10,0},{10,0}},{2,{0,-10},{0,10}}};auto cg=NetBuilder::Build(cp,cw);
 check(cg.pinNet[0][0]!=cg.pinNet[2][0],"crossing without point not joined");cw[1].a={0,0};cg=NetBuilder::Build(cp,cw);
 check(cg.pinNet[0][0]==cg.pinNet[3][0],"T endpoint joins");
 auto l1=make(L"网络标签"),l2=make(L"全局标签",2);l1.value=l2.value="DATA";l2.pos={200,200};
 cg=NetBuilder::Build({l1,l2},{});check(cg.nets.size()==1&&cg.nets[0].name=="DATA","named labels merge separated points");
 auto p1=make(L"GND"),p2=make(L"GND",2);p2.pos={200,200};cg=NetBuilder::Build({p1,p2},{});check(cg.nets.size()==1&&cg.nets[0].isPower,"same power name merges");
 for(int m=0;m<4;m++){
  int a=m&1,b=(m>>1)&1;
  check(truth(L"与门",2,m).values[2]==(a&b),"AND truth");
  check(truth(L"或门",2,m).values[2]==(a|b),"OR truth");
  check(truth(L"与非门",2,m).values[2]==!(a&b),"NAND truth");
  check(truth(L"或非门",2,m).values[2]==!(a|b),"NOR truth");
  check(truth(L"异或门",2,m).values[2]==(a^b),"XOR truth");
  check(truth(L"同或门",2,m).values[2]==!(a^b),"XNOR truth");
  auto h=truth(L"半加器",2,m);check(h.values[2]==(a^b)&&h.values[3]==(a&b),"half adder truth");
 }
 for(int m=0;m<2;m++)check(truth(L"非门",1,m).values[1]==!m,"NOT truth");
 for(int m=0;m<8;m++){int sum=(m&1)+((m>>1)&1)+((m>>2)&1);auto f=truth(L"全加器",3,m);check(f.values[3]==sum%2&&f.values[4]==(sum>=2),"full adder truth");}
 check(truth(L"与门",2,0,0).values[2]==0,"AND controlling zero with unknown");
 check(truth(L"或门",2,2,0).values[2]==1,"OR controlling one with unknown");
 check(truth(L"异或门",2,0,0).values[2]==-1,"XOR unknown stays unknown");
 auto a=make(L"常量"),b=make(L"常量",2);a.value="0";b.value="1";cg.pinNet={{0},{0}};cg.nets.resize(1);
 auto conflict=SimulateDigital({a,b},cg);check(conflict.values[0]==2,"conflicting sources report ERR");

 auto marker=make(L"输入引脚",2);marker.name=L"未连接标记";marker.refdes="NC1";marker.pins.clear();
 auto lone=make(L"与门");marker.pos=PinPosition(lone,0);auto mg=NetBuilder::Build({lone,marker},{});
 auto report=NetBuilder::RunErc({lone,marker},{},mg);
 check(!report.Contains(L"U1 的引脚 A 悬空") && report.Contains(L"U1 的引脚 B 悬空"),"NC suppresses only matching pin");
 auto repeated=std::vector<ComponentInstance>{make(L"与门"),make(L"或门")};
 check(NetBuilder::RunErc(repeated,{},NetBuilder::Build(repeated,{})).Contains(L"重复位号"),"ERC duplicate references");
 auto customPath=wxFileName::CreateTempFileName("library-test-");
 check(SchematicIO::WriteTextFile(customPath,L"<library><component name=\"TEST_CUSTOM\" prefix=\"U\" inputs=\"3\" outputs=\"1\"/></library>",&error),"write custom fixture");
 check(ComponentCatalog::LoadCustomXml(customPath,error)&&ComponentCatalog::Find("TEST_CUSTOM")->inputs==3,"import custom XML");
 int before=ComponentCatalog::ComponentCount();
 check(!ComponentCatalog::LoadCustomXml(customPath,error)&&ComponentCatalog::ComponentCount()==before,"duplicate import atomic rejection");
 wxRemoveFile(customPath);
 auto invalid=wxFileName::CreateTempFileName("invalid-test-");
 SchematicDocument untouched=d;
 check(SchematicIO::WriteTextFile(invalid,"<invalid/>",&error)&&!SchematicIO::Load(invalid,untouched,&error)&&untouched.components.size()==3,"invalid root preserves destination");
 check(SchematicIO::WriteTextFile(invalid,"<schematic version=\"1.0\"><view gridStep=\"0\"/></schematic>",&error)&&!SchematicIO::Load(invalid,untouched,&error),"reject zero grid");
 check(SchematicIO::WriteTextFile(invalid,"<schematic version=\"9\"/>",&error)&&!SchematicIO::Load(invalid,untouched,&error),"reject unsupported version");
 check(SchematicIO::WriteTextFile(invalid,"<schematic><view zoom=\"NaN\"/></schematic>",&error)&&!SchematicIO::Load(invalid,untouched,&error),"reject nonfinite zoom");
 check(SchematicIO::WriteTextFile(invalid,"<schematic><view gridStep=\"oops\"/></schematic>",&error)&&!SchematicIO::Load(invalid,untouched,&error),"reject invalid integer");
 wxRemoveFile(invalid);
 std::cout<<"PASS "<<passed<<" assertions; sample 3 components / 5 wires / 9 pins / 7 nets / 1 junction; ERC 7 issues.\n";
}
