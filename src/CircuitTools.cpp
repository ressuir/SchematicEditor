#include "CircuitTools.h"
#include "ComponentCatalog.h"
#include <algorithm>
#include <map>
#include <set>
namespace se {
namespace {
wxString Prefix(const wxString& name) {
    const auto* def = ComponentCatalog::Find(name);
    return def ? def->refPrefix : wxString("U");
}
wxString Csv(wxString v) { v.Replace("\"", "\"\""); return "\"" + v + "\""; }
int Merge(int a, int b) { if (a == 2 || b == 2) return 2; if (a < 0) return b; if (b < 0) return a; return a == b ? a : 2; }
wxString LogicText(int v) { return v < 0 ? "X" : v == 2 ? "ERR" : wxString::Format("%d", v); }
}
wxString NextReference(const std::vector<ComponentInstance>& components, const wxString& name) {
    auto prefix = Prefix(name);
    if (prefix.StartsWith("#")) return name;
    std::set<wxString> used;
    for (const auto& c : components) used.insert(c.refdes);
    for (int n = 1;; ++n) {
        auto ref = prefix + wxString::Format("%d", n);
        if (!used.count(ref)) return ref;
    }
}
void AnnotateComponents(std::vector<ComponentInstance>& components) {
    std::map<wxString, int> counts;
    for (auto& c : components) {
        auto prefix = Prefix(c.name);
        c.refdes = prefix.StartsWith("#") ? c.name : prefix + wxString::Format("%d", ++counts[prefix]);
    }
}
void AppendCopies(std::vector<ComponentInstance>& components, std::vector<Wire>& wires,
                  const std::vector<ComponentInstance>& copiedComponents,
                  const std::vector<Wire>& copiedWires, const wxPoint& offset) {
    int componentId = 0, wireId = 0;
    for (const auto& c : components) componentId = std::max(componentId, c.id);
    for (const auto& w : wires) wireId = std::max(wireId, w.id);
    for (auto c : copiedComponents) {
        c.id = ++componentId; c.refdes = NextReference(components, c.name); c.pos += offset;
        components.push_back(c);
    }
    for (auto w : copiedWires) { w.id = ++wireId; w.a += offset; w.b += offset; wires.push_back(w); }
}
wxString BuildBom(const std::vector<ComponentInstance>& components) {
    wxString text = wxString::FromUTF8("位号,元件,参数,封装\n");
    for (const auto& c : components)
        if (!Prefix(c.name).StartsWith("#") && c.name!=L"文本" && c.name!=L"连接点" && c.name!=L"未连接标记") text += Csv(c.refdes)+","+Csv(c.name)+","+Csv(c.value)+","+Csv(c.footprint)+"\n";
    return text;
}
SimulationResult SimulateDigital(const std::vector<ComponentInstance>& components, const NetGraph& graph) {
    SimulationResult r; r.values.assign(graph.nets.size(), -1);
    std::set<wxString> unsupported;
    auto net = [&](size_t c, size_t k) {
        return c < graph.pinNet.size() && k < graph.pinNet[c].size() ? graph.pinNet[c][k] : -1;
    };
    for (int iteration = 0; iteration < 256; ++iteration) {
        std::vector<std::vector<int>> drivers(graph.nets.size());
        auto drive = [&](int n, int v) { if (n >= 0 && n < static_cast<int>(drivers.size())) drivers[n].push_back(v); };
        for (size_t ci = 0; ci < components.size(); ++ci) {
            const auto& c = components[ci];
            auto in = [&](size_t k) { int n=net(ci,k); return n>=0 ? r.values[n] : -1; };
            if (c.name == L"GND" || c.name == L"VCC" || c.name == L"常量" || c.name == L"输入引脚") {
                int v = c.name == L"GND" ? 0 : c.name == L"VCC" ? 1 : c.value == "1" ? 1 : c.value == "0" || c.value.empty() ? 0 : -1;
                if (!c.pins.empty()) drive(net(ci,0),v);
                continue;
            }
            if (c.name == L"输出引脚" || c.name == L"探针" || c.name == L"网络标签" || c.name == L"全局标签" || c.name == L"连接点" || c.name == L"未连接标记" || c.name == L"文本") continue;
            bool supported = c.name == L"与门" || c.name == L"与非门" || c.name == L"或门" || c.name == L"或非门" || c.name == L"非门" || c.name == L"异或门" || c.name == L"同或门" || c.name == L"半加器" || c.name == L"全加器";
            if (!supported) { unsupported.insert(c.name); continue; }
            std::vector<int> inputs; std::vector<size_t> outputs;
            for (size_t k=0;k<c.pins.size();++k) {
                if (c.pins[k].dir == PinDir::Input) inputs.push_back(in(k));
                if (c.pins[k].dir == PinDir::Output) outputs.push_back(k);
            }
            int v=-1; bool known=!inputs.empty(), error=false;
            for(int a:inputs) { known &= a>=0 && a<=1; error |= a==2; }
            if (error) v=2;
            else if (c.name==L"与门" || c.name==L"与非门") {
                if (std::find(inputs.begin(),inputs.end(),0)!=inputs.end()) v=0;
                else if(known) v=1;
                if(c.name==L"与非门" && v>=0) v=1-v;
            } else if (c.name==L"或门" || c.name==L"或非门") {
                if(std::find(inputs.begin(),inputs.end(),1)!=inputs.end()) v=1;
                else if(known) v=0;
                if(c.name==L"或非门" && v>=0) v=1-v;
            } else if(known) {
                int sum=0; for(int a:inputs) sum+=a;
                v = c.name==L"非门" ? 1-inputs[0] : sum%2;
                if(c.name==L"同或门") v=1-v;
            }
            for(size_t o=0;o<outputs.size();++o) {
                int out=v;
                if(o==1 && (c.name==L"半加器" || c.name==L"全加器") && known) { int sum=0;for(int a:inputs)sum+=a;out=sum>=2?1:0; }
                drive(net(ci,outputs[o]),out);
            }
        }
        std::vector<int> next(drivers.size(),-1);
        for(size_t n=0;n<drivers.size();++n) {
            bool unknown=false;int value=-1;
            for(int v:drivers[n]) { unknown |= v<0; value=Merge(value,v); }
            next[n]= value==2 ? 2 : unknown ? -1 : value;
        }
        if(next==r.values) {r.converged=true;break;}
        r.values=next;
    }
    r.report=wxString::FromUTF8("数字组合电路静态求值\n支持 0 / 1 / X（未知）/ ERR（驱动冲突）；不包含模拟电路、时钟和传播延时。\n\n");
    for(size_t n=0;n<graph.nets.size();++n) r.report+=graph.nets[n].name+" = "+LogicText(r.values[n])+"\n";
    r.report+=r.converged?wxString::FromUTF8("\n固定点迭代结束；X 不代表有效逻辑结果。\n"):wxString::FromUTF8("\n256 次迭代未收敛，可能存在反馈。\n");
    for(const auto& name:unsupported) r.report+=wxString::FromUTF8("不支持的器件模型：")+name+"\n";
    return r;
}
}
