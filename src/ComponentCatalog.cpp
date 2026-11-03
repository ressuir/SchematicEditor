#include "ComponentCatalog.h"
#include <wx/xml/xml.h>
#include <set>
#include <algorithm>
#include "DigitalSimulator.h"

namespace se
{

// ------------------------------------------------------------
// 内置元件库
//
// 分类与任务描述 PPT / 现有菜单“放置”一栏保持一致：
//   数字逻辑 / 输入输出 / 无源器件 / 半导体器件 / 模拟器件 /
//   电源与地 / 连线与标签
//
// kind 决定引脚摆法：
//   Logic   输入在左、输出在右
//   Passive 左 1、右 2
//   Power   底部单引脚
//   Port    单引脚端口
//
// 注意：导线不在这里 —— 导线由“导线工具”直接绘制，
//       连接点由连通性算法自动产生，不需要作为元件放置。
// ------------------------------------------------------------
const std::vector<CategoryDef>& ComponentCatalog::Categories()
{
    static std::vector<CategoryDef> kCategories =
    {
        {
            L"数字逻辑",
            {
                { L"与门",   L"U", L"两输入与门，Y = A · B",        2, 1, ComponentKind::Logic, 60, 40 },
                { L"或门",   L"U", L"两输入或门，Y = A + B",        2, 1, ComponentKind::Logic, 60, 40 },
                { L"非门",   L"U", L"反相器，Y = /A",               1, 1, ComponentKind::Logic, 55, 40 },
                { L"与非门", L"U", L"两输入与非门，Y = /(A · B)",   2, 1, ComponentKind::Logic, 60, 40 },
                { L"或非门", L"U", L"两输入或非门，Y = /(A + B)",   2, 1, ComponentKind::Logic, 60, 40 },
                { L"异或门", L"U", L"两输入异或门，Y = A ⊕ B",      2, 1, ComponentKind::Logic, 60, 40 },
                { L"同或门", L"U", L"两输入同或门，Y = /(A ⊕ B)",   2, 1, ComponentKind::Logic, 60, 40 },
                { L"半加器", L"U", L"半加器：A/B 输入，S 和、C 进位", 2, 2, ComponentKind::Logic, 64, 46 },
                { L"全加器", L"U", L"全加器：A/B/Cin，S 和、C 进位", 3, 2, ComponentKind::Logic, 68, 52 },
            }
        },
        {
            L"输入输出",
            {
                { L"输入引脚", L"IN",  L"电路输入端口",          0, 1, ComponentKind::Port, 46, 34 },
                { L"输出引脚", L"OUT", L"电路输出端口",          1, 0, ComponentKind::Port, 46, 34 },
                { L"时钟",     L"CLK", L"时钟信号源",            0, 1, ComponentKind::Port, 46, 34 },
                { L"常量",     L"V",   L"逻辑常量 0 / 1",        0, 1, ComponentKind::Port, 46, 34 },
                { L"探针",     L"P",   L"观察某条网络的逻辑值",  1, 1, ComponentKind::Port, 46, 34 },
            }
        },
        {
            L"无源器件",
            {
                { L"电阻", L"R", L"理想电阻（两端器件）", 1, 1, ComponentKind::Passive, 34, 60 },
                { L"电容", L"C", L"理想电容（两端器件）", 1, 1, ComponentKind::Passive, 34, 60 },
                { L"电感", L"L", L"理想电感（两端器件）", 1, 1, ComponentKind::Passive, 34, 60 },
            }
        },
        {
            L"半导体器件",
            {
                { L"二极管",     L"D", L"PN 结二极管", 1, 1, ComponentKind::Passive, 34, 60 },
                { L"LED",        L"D", L"发光二极管",  1, 1, ComponentKind::Passive, 34, 60 },
                { L"NPN 晶体管", L"Q", L"NPN 三极管：B 输入，C/E 输出", 1, 2, ComponentKind::Logic, 60, 50 },
                { L"PNP 晶体管", L"Q", L"PNP 三极管：B 输入，C/E 输出", 1, 2, ComponentKind::Logic, 60, 50 },
            }
        },
        {
            L"模拟器件",
            {
                { L"运算放大器", L"U", L"理想运放：两输入一输出", 2, 1, ComponentKind::Logic, 62, 46 },
            }
        },
        {
            L"电源与地",
            {
                { L"VCC", L"#PWR", L"电源正极", 0, 1, ComponentKind::Power, 36, 34 },
                { L"GND", L"#PWR", L"电源地",   0, 1, ComponentKind::Power, 36, 34 },
            }
        },
        {
            L"连线与标签",
            {
                { L"网络标签", L"N", L"局部网络命名（贴在导线上）", 1, 1, ComponentKind::Port, 46, 34 },
                { L"全局标签", L"G", L"跨页全局网络命名",           1, 1, ComponentKind::Port, 46, 34 },
            }
        },
    };

    return kCategories;
}

const ComponentDef* ComponentCatalog::Find(const wxString& name)
{
    for (const CategoryDef& cat : Categories())
    {
        for (const ComponentDef& item : cat.items)
        {
            if (item.name == name)
                return &item;
        }
    }

    return nullptr;
}

int ComponentCatalog::ComponentCount()
{
    int total = 0;

    for (const CategoryDef& cat : Categories())
        total += static_cast<int>(cat.items.size());

    return total;
}

} // namespace se
namespace se {
bool ComponentCatalog::LoadCustomXml(const wxString& path, wxString& error) {
    wxXmlDocument xml;
    if(!xml.Load(path) || !xml.GetRoot() || xml.GetRoot()->GetName()!="library") {error=L"根节点应为 <library>";return false;}
    std::vector<ComponentDef> pending;std::set<wxString> names;
    for(const auto& category:Categories())for(const auto& item:category.items)names.insert(item.name);
    for(auto* node=xml.GetRoot()->GetChildren();node;node=node->GetNext()) {
        if(node->GetType()!=wxXML_ELEMENT_NODE || node->GetName()!="component")continue;
        ComponentDef c;c.name=node->GetAttribute("name");c.refPrefix=node->GetAttribute("prefix","U");
        c.description=node->GetAttribute("description",L"用户自定义元件");
        long ni=0,no=1,w=60,h=40;
        bool valid=node->GetAttribute("inputs","0").ToLong(&ni)&&node->GetAttribute("outputs","1").ToLong(&no)&&node->GetAttribute("w","60").ToLong(&w)&&node->GetAttribute("h","40").ToLong(&h);
        if(!valid || c.name.empty() || c.refPrefix.empty() || names.count(c.name) || ni<0 || no<0 || ni>64 || no>64 || ni+no<1 || ni+no>64 || w<20 || h<20 || w>1000 || h>1000) {error=L"名称重复或元件参数非法，整个库未导入";return false;}
        for(auto ch:c.refPrefix)if(ch<'A'||ch>'Z'){error=L"位号前缀应为大写英文字母";return false;}
        c.inputs=(int)ni;c.outputs=(int)no;c.w=(int)w;c.h=(int)h;c.kind=ComponentKind::Logic;
        c.logicModel=node->GetAttribute("model");c.truthTable=node->GetAttribute("truth");
        long delay=1;
        if(!node->GetAttribute("delay","1").ToLong(&delay)||delay<1||delay>1000){error=L"传播延迟必须为1至1000步";return false;}
        c.delayTicks=(int)delay;
        if(!c.logicModel.empty()) {
            ComponentInstance probe;probe.id=1;probe.name=c.name;probe.logicModel=c.logicModel;probe.truthTable=c.truthTable;
            probe.delayTicks=c.delayTicks;probe.pins=MakePins(c.name,c.kind,c.inputs,c.outputs);
            auto graph=NetBuilder::Build({probe},{});DigitalSimulator sim;
            if(!sim.Reset({probe},graph,error))return false;
        }

        names.insert(c.name);pending.push_back(c);
    }
    if(pending.empty()){error=L"库中没有有效的元件";return false;}
    auto& categories=const_cast<std::vector<CategoryDef>&>(Categories());
    // Categories owns mutable static storage; the public read API remains const.
    auto found=std::find_if(categories.begin(),categories.end(),[](const CategoryDef& c){return c.name==L"自定义";});
    if(found==categories.end())categories.push_back({L"自定义",pending});
    else found->items.insert(found->items.end(),pending.begin(),pending.end());
    return true;
}
}
