#include "EditingTools.h"
#include "SchematicIO.h"
#include <iostream>
#include <cstdlib>
#include <set>
using namespace se;
void check(bool ok,const char* s){if(!ok){std::cerr<<"FAIL "<<s<<"\n";std::exit(1);}std::cout<<"PASS "<<s<<"\n";}
ComponentInstance pin(int id,wxPoint at){ComponentInstance c;c.id=id;c.name="TEST";c.refdes=wxString::Format("P%d",id);c.pos={at.x+10,at.y-20};c.pins={{"P",PinDir::Passive,Side::Left,0,1}};return c;}
bool sameTopology(const NetGraph& a,const NetGraph& b){if(a.pinNet.size()!=b.pinNet.size())return false;for(size_t i=0;i<a.pinNet.size();++i)for(size_t j=0;j<a.pinNet.size();++j)if((a.pinNet[i][0]==a.pinNet[j][0])!=(b.pinNet[i][0]==b.pinNet[j][0]))return false;return true;}
int main(){
 std::vector<ComponentInstance> cs={pin(1,{-100,0}),pin(2,{100,0}),pin(3,{0,100}),pin(4,{0,0})};
 std::vector<Wire> ws={{1,{-100,0},{100,0}},{2,{0,0},{0,100}}};auto before=NetBuilder::Build(cs,ws);
 MoveConnectedComponents(cs,ws,{3},{40,40});auto after=NetBuilder::Build(cs,ws);
 check(sameTopology(before,after),"moving a pin on a T intersection preserves every pair of connected terminals");
 bool orthogonal=true;for(auto w:ws)orthogonal&=w.IsAxisAligned()&&w.a!=w.b;check(orthogonal,"rerouted T branches contain only nonzero orthogonal segments");
 std::set<int> ids;for(auto w:ws)ids.insert(w.id);check(ids.size()==ws.size(),"split and elbow wire IDs remain unique");
 check(PinPosition(cs[3],0)==wxPoint(40,40),"moved component pin follows requested displacement");
 cs={pin(1,{-100,0}),pin(2,{100,0}),pin(3,{0,-100}),pin(4,{0,100})};ws={{1,{-100,0},{100,0}},{2,{0,-100},{0,100}}};before=NetBuilder::Build(cs,ws);
 MoveConnectedComponents(cs,ws,{0},{-20,20});after=NetBuilder::Build(cs,ws);check(sameTopology(before,after),"drag does not turn an unconnected crossing into a junction");
 auto originalCs=cs;auto originalWs=ws;MoveConnectedComponents(cs,ws,{0},{0,0});check(cs[0].pos==originalCs[0].pos&&ws.size()==originalWs.size(),"zero drag does not alter document or subdivide wires");
 std::cout<<"PASS 6 editing model checks (not GUI end-to-end)\n";
}
