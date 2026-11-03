#include "SchematicIO.h"
#include "ComponentCatalog.h"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/xml/xml.h>
#include <set>
#include <cmath>
#include <functional>
#include <climits>

namespace se
{

// ============================================================
//  局部工具
// ============================================================
namespace
{

// 整数 / 布尔属性（wxXmlAttribute 只有字符串，统一在这里转换）
void AddIntAttr(wxXmlNode* node, const char* name, int value)
{
    node->AddAttribute(name, wxString::Format(L"%d", value));
}

void AddBoolAttr(wxXmlNode* node, const char* name, bool value)
{
    node->AddAttribute(name, value ? L"1" : L"0");
}

int GetIntAttr(const wxXmlNode* node, const char* name, int defaultValue)
{
    wxString text;

    if (!node->GetAttribute(name, &text))
        return defaultValue;

    long value = 0;

    if (!text.ToLong(&value))
        return defaultValue;

    return static_cast<int>(value);
}

bool GetBoolAttr(const wxXmlNode* node, const char* name, bool defaultValue)
{
    wxString text;

    if (!node->GetAttribute(name, &text))
        return defaultValue;

    return text == L"1" || text.Lower() == L"true";
}

wxString GetStrAttr(const wxXmlNode* node, const char* name, const wxString& defaultValue = wxEmptyString)
{
    wxString text;

    if (!node->GetAttribute(name, &text))
        return defaultValue;

    return text;
}

// 枚举 ↔ 字符串
const char* DirToStr(PinDir dir)
{
    switch (dir)
    {
        case PinDir::Input:   return "input";
        case PinDir::Output:  return "output";
        case PinDir::Bidir:   return "bidir";
        case PinDir::Passive: return "passive";
    }

    return "passive";
}

PinDir StrToDir(const wxString& text)
{
    if (text == L"output")  return PinDir::Output;
    if (text == L"bidir")   return PinDir::Bidir;
    if (text == L"passive") return PinDir::Passive;

    return PinDir::Input;
}

const char* SideToStr(Side side)
{
    switch (side)
    {
        case Side::Left:   return "left";
        case Side::Right:  return "right";
        case Side::Top:    return "top";
        case Side::Bottom: return "bottom";
    }

    return "left";
}

Side StrToSide(const wxString& text)
{
    if (text == L"right")  return Side::Right;
    if (text == L"top")    return Side::Top;
    if (text == L"bottom") return Side::Bottom;

    return Side::Left;
}

void SetError(wxString* error, const wxString& message)
{
    if (error)
        *error = message;
}

} // namespace

// ============================================================
//  通配符
// ============================================================

wxString SchematicIO::SchematicWildcard()
{
    return L"原理图文件 (*.ses)|*.ses|XML 文件 (*.xml)|*.xml|所有文件 (*.*)|*.*";
}

wxString SchematicIO::NetlistWildcard()
{
    return L"网络表文件 (*.net)|*.net|文本文件 (*.txt)|*.txt|所有文件 (*.*)|*.*";
}

// ============================================================
//  保存
// ============================================================

bool SchematicIO::Save(const wxString& path, const SchematicDocument& doc, wxString* error)
{
    wxXmlDocument xml;
    xml.SetFileEncoding(L"UTF-8");

    wxXmlNode* root = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"schematic");
    root->AddAttribute(L"version", L"1.1");
    root->AddAttribute(L"format", L"circuit-schematic");
    root->AddAttribute(L"generator", L"SchematicEditor 1.0");

    // ---- 视图设置 ----
    wxXmlNode* view = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"view");
    AddIntAttr(view, "gridStep", doc.gridStep);
    AddBoolAttr(view, "gridVisible", doc.gridVisible);
    AddBoolAttr(view, "snapEnabled", doc.snapEnabled);
    view->AddAttribute(L"zoom", wxString::Format(L"%.4f", doc.zoom));
    root->AddChild(view);

    // ---- 元件 ----
    wxXmlNode* components = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"components");
    components->AddAttribute(L"count", wxString::Format(L"%d", static_cast<int>(doc.components.size())));

    for (const ComponentInstance& comp : doc.components)
    {
        wxXmlNode* node = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"component");
        AddIntAttr(node, "id", comp.id);
        node->AddAttribute(L"name", comp.name);
        node->AddAttribute(L"refdes", comp.refdes);
        node->AddAttribute(L"value", comp.value);
        node->AddAttribute(L"footprint", comp.footprint);
        node->AddAttribute("model",comp.logicModel);node->AddAttribute("truth",comp.truthTable);
        AddIntAttr(node,"delay",comp.delayTicks);AddIntAttr(node,"clockPeriod",comp.clockPeriodTicks);
        AddIntAttr(node, "x", comp.pos.x);
        AddIntAttr(node, "y", comp.pos.y);
        AddIntAttr(node, "w", comp.w);
        AddIntAttr(node, "h", comp.h);

        // 引脚一起存：文件自包含，不依赖当前元件库版本
        for (const Pin& pin : comp.pins)
        {
            wxXmlNode* pinNode = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"pin");
            pinNode->AddAttribute(L"name", pin.name);
            pinNode->AddAttribute("number",pin.number);
            pinNode->AddAttribute(L"dir", DirToStr(pin.dir));
            pinNode->AddAttribute(L"side", SideToStr(pin.side));
            AddIntAttr(pinNode, "slot", pin.slot);
            AddIntAttr(pinNode, "count", pin.count);

            node->AddChild(pinNode);
        }

        components->AddChild(node);
    }

    root->AddChild(components);

    // ---- 导线 ----
    wxXmlNode* wires = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"wires");
    wires->AddAttribute(L"count", wxString::Format(L"%d", static_cast<int>(doc.wires.size())));

    for (const Wire& wire : doc.wires)
    {
        wxXmlNode* node = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"wire");
        AddIntAttr(node, "id", wire.id);
        AddIntAttr(node, "x1", wire.a.x);
        AddIntAttr(node, "y1", wire.a.y);
        AddIntAttr(node, "x2", wire.b.x);
        AddIntAttr(node, "y2", wire.b.y);

        wires->AddChild(node);
    }

    root->AddChild(wires);

    xml.SetRoot(root);

    // 确保目录存在
    const wxFileName fileName(path);

    if (!fileName.GetPath().IsEmpty() && !wxFileName::DirExists(fileName.GetPath()))
    {
        SetError(error, wxString::Format(L"目录不存在：%s", fileName.GetPath()));
        return false;
    }

    const wxString temp=wxFileName::CreateTempFileName(fileName.GetPathWithSep()+".schematic-save-");
    if(temp.empty() || !xml.Save(temp) || !wxRenameFile(temp,path,true)) {
        if(!temp.empty() && wxFileExists(temp))wxRemoveFile(temp);
        SetError(error,L"保存失败，无法写入或替换目标文件");return false;
    }

    return true;
}

// ============================================================
//  加载
// ============================================================

bool SchematicIO::Load(const wxString& path, SchematicDocument& doc, wxString* error)
{
    wxXmlDocument xml;

    if (!xml.Load(path))
    {
        SetError(error, L"无法读取文件，或 XML 格式有误");
        return false;
    }

    wxXmlNode* root = xml.GetRoot();

    if (!root)
    {
        SetError(error, L"文件是空的");
        return false;
    }

    if (root->GetName() != L"schematic")
    {
        SetError(error, wxString::Format(L"根节点是 <%s>，不是本程序的原理图文件",
                                         root->GetName()));
        return false;
    }

    if (root->GetAttribute("version", "1.0") != "1.0" && root->GetAttribute("version", "1.0") != "1.1") {
        SetError(error, L"不支持的文件版本"); return false;
    }
    // Validate numeric lexical forms before GetIntAttr can apply missing-field defaults.
    std::function<bool(const wxXmlNode*)> numericValid = [&](const wxXmlNode* node) {
        static const std::set<wxString> integers={"id","x","y","w","h","slot","count","gridStep","x1","y1","x2","y2","delay","clockPeriod"};
        for(auto* attr=node->GetAttributes();attr;attr=attr->GetNext()) {
            if(integers.count(attr->GetName())) {
                long value=0;
                if(!attr->GetValue().ToLong(&value) || value<INT_MIN || value>INT_MAX)return false;
            }
            if(attr->GetName()=="zoom") {double value=0;if(!attr->GetValue().ToDouble(&value)||!std::isfinite(value)||value<0.2||value>8.0)return false;}
        }
        for(auto* child=node->GetChildren();child;child=child->GetNext())if(child->GetType()==wxXML_ELEMENT_NODE&&!numericValid(child))return false;
        return true;
    };
    if(!numericValid(root)){SetError(error,L"数值字段格式错误或超出范围");return false;}
    SchematicDocument loaded;

    for (wxXmlNode* section = root->GetChildren(); section; section = section->GetNext())
    {
        if (section->GetType() != wxXML_ELEMENT_NODE)
            continue;

        const wxString sectionName = section->GetName();

        // ---------------- view ----------------
        if (sectionName == L"view")
        {
            loaded.gridStep    = GetIntAttr(section, "gridStep", 10);
            loaded.gridVisible = GetBoolAttr(section, "gridVisible", true);
            loaded.snapEnabled = GetBoolAttr(section, "snapEnabled", true);

            wxString zoomText = GetStrAttr(section, "zoom", L"1.0");
            double   zoom     = 1.0;

            if (zoomText.ToDouble(&zoom) && zoom > 0.01)
                loaded.zoom = zoom;
        }
        // ---------------- components ----------------
        else if (sectionName == L"components")
        {
            for (wxXmlNode* node = section->GetChildren(); node; node = node->GetNext())
            {
                if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != L"component")
                    continue;

                ComponentInstance comp;
                comp.id     = GetIntAttr(node, "id", 0);
                comp.name   = GetStrAttr(node, "name");
                comp.refdes = GetStrAttr(node, "refdes");
                comp.value = GetStrAttr(node, "value");
                comp.footprint = GetStrAttr(node, "footprint");
                comp.logicModel=GetStrAttr(node,"model");comp.truthTable=GetStrAttr(node,"truth");
                comp.delayTicks=GetIntAttr(node,"delay",1);comp.clockPeriodTicks=GetIntAttr(node,"clockPeriod",10);
                comp.pos    = wxPoint(GetIntAttr(node, "x", 0), GetIntAttr(node, "y", 0));
                comp.w      = GetIntAttr(node, "w", 60);
                comp.h      = GetIntAttr(node, "h", 40);

                for (wxXmlNode* pinNode = node->GetChildren(); pinNode; pinNode = pinNode->GetNext())
                {
                    if (pinNode->GetType() != wxXML_ELEMENT_NODE || pinNode->GetName() != L"pin")
                        continue;

                    Pin pin;
                    pin.name  = GetStrAttr(pinNode, "name");
                    pin.number = GetStrAttr(pinNode,"number");
                    const auto direction=GetStrAttr(pinNode,"dir","input"),side=GetStrAttr(pinNode,"side","left");
                    if((direction!="input"&&direction!="output"&&direction!="passive"&&direction!="bidir") ||
                       (side!="left"&&side!="right"&&side!="top"&&side!="bottom")){SetError(error,L"未知引脚方向或边" );return false;}
                    pin.dir   = StrToDir(GetStrAttr(pinNode, "dir", L"input"));
                    pin.side  = StrToSide(GetStrAttr(pinNode, "side", L"left"));
                    pin.slot  = GetIntAttr(pinNode, "slot", 0);
                    pin.count = GetIntAttr(pinNode, "count", 1);

                    comp.pins.push_back(pin);
                }

                // 引脚定义缺失时兜底：按名字重建（老文件 / 手写文件容错）
                if (comp.pins.empty() && comp.name != L"文本" && comp.name != L"未连接标记") {
                    const auto* def=ComponentCatalog::Find(comp.name);
                    if (!def) {SetError(error,L"未知元件缺少引脚定义");return false;}
                    comp.pins=MakePins(comp.name,def->kind,def->inputs,def->outputs);
                }

                loaded.components.push_back(comp);
            }
        }
        // ---------------- wires ----------------
        else if (sectionName == L"wires")
        {
            for (wxXmlNode* node = section->GetChildren(); node; node = node->GetNext())
            {
                if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != L"wire")
                    continue;

                Wire wire;
                wire.id = GetIntAttr(node, "id", 0);
                wire.a  = wxPoint(GetIntAttr(node, "x1", 0), GetIntAttr(node, "y1", 0));
                wire.b  = wxPoint(GetIntAttr(node, "x2", 0), GetIntAttr(node, "y2", 0));

                loaded.wires.push_back(wire);
            }
        }
    }

    if (loaded.gridStep < 1 || loaded.gridStep > 100 || !std::isfinite(loaded.zoom) || loaded.zoom < 0.2 || loaded.zoom > 8.0) {
        SetError(error,L"视图参数超出范围");return false;
    }
    std::set<int> componentIds, wireIds;
    for (const auto& c : loaded.components) {
        if (c.id<=0 || c.id>1000000000 || !componentIds.insert(c.id).second || c.name.empty() || c.refdes.empty() || c.w<0 || c.h<0 || c.w>10000 || c.h>10000 || c.delayTicks<1 || c.delayTicks>1000 || c.clockPeriodTicks<2 || c.clockPeriodTicks>10000 || c.clockPeriodTicks%2 ||
            std::abs((long long)c.pos.x)>10000000 || std::abs((long long)c.pos.y)>10000000) {
            SetError(error,L"元件 ID、名称、位号或几何尺寸非法");return false;
        }
        for(const auto& pin:c.pins) if(pin.name.empty() || pin.count<1 || pin.slot<0 || pin.slot>=pin.count || pin.count>1000) {
            SetError(error,L"引脚定义非法");return false;
        }
    }
    for(const auto& w:loaded.wires) if(w.id<=0 || w.id>1000000000 || !wireIds.insert(w.id).second || w.a==w.b || !w.IsAxisAligned() ||
        std::abs((long long)w.a.x)>10000000 || std::abs((long long)w.a.y)>10000000 ||
        std::abs((long long)w.b.x)>10000000 || std::abs((long long)w.b.y)>10000000) {
        SetError(error,L"导线 ID 或坐标非法");return false;
    }
    doc = loaded;
    return true;
}

// ============================================================
//  纯文本写出
// ============================================================

bool SchematicIO::WriteTextFile(const wxString& path, const wxString& text, wxString* error)
{
    wxFile file;

    if (!file.Create(path, true))
    {
        SetError(error, L"无法创建文件（路径不存在或没有写权限？）");
        return false;
    }

    // 统一按 UTF-8 写出（网表里有中文注释；不加 BOM，便于 SPICE 类工具直接读取）
    if (!file.Write(text, wxConvUTF8))
    {
        SetError(error, L"写入过程中出错");
        file.Close();
        return false;
    }

    file.Close();
    return true;
}

} // namespace se
