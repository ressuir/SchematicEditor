// ============================================================
//  ComponentCatalog.h  —— 元件库数据模型
//
//  这是“任务 3：元件库模块”的最小可用模型：
//    * 分类（Category）→ 元件（ComponentDef）两级结构，
//      与元件库面板的树形控件一一对应；
//    * 元件定义是只读的静态数据，属性表、绘图区都从这里取信息；
//    * ComponentKind 决定引脚怎么摆（逻辑门 / 无源 / 电源 / 端口）；
//    * 后续要支持“用户自定义元件”，只需把 Categories() 的数据源
//      从硬编码改成读取 XML/JSON 元件库文件即可，调用方无需改动。
// ============================================================

#pragma once

#include <wx/string.h>

#include <vector>

#include "NetModel.h"

namespace se
{

// ------------------------------------------------------------
// 元件定义
// ------------------------------------------------------------
struct ComponentDef
{
    wxString      name;          // 显示名，例如 “与门”
    wxString      refPrefix;     // 位号前缀，例如 “U” → U1 / U2
    wxString      description;   // 说明文字
    int           inputs  = 0;   // 输入引脚数
    int           outputs = 0;   // 输出引脚数
    ComponentKind kind    = ComponentKind::Logic;  // 电气形态（决定引脚摆法）
    int           w       = 60;  // 元件框宽（世界坐标）
    int           h       = 40;  // 元件框高
    wxString      logicModel;
    wxString      truthTable;
    int           delayTicks = 1;
};

// ------------------------------------------------------------
// 分类
// ------------------------------------------------------------
struct CategoryDef
{
    wxString                  name;
    std::vector<ComponentDef> items;
};

// ------------------------------------------------------------
// 元件目录（全部静态数据）
// ------------------------------------------------------------
class ComponentCatalog
{
public:
    // 全部分类
    static const std::vector<CategoryDef>& Categories();

    // 按名称查找元件，找不到返回 nullptr
    static const ComponentDef* Find(const wxString& name);

    // 元件总数
    static int ComponentCount();
    static bool LoadCustomXml(const wxString& path, wxString& error);
};

} // namespace se
