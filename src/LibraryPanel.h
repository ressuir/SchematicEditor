// ============================================================
//  LibraryPanel.h  —— 元件库面板（左侧分区）
//
//  用 wxTreeCtrl 展示「分类 → 元件」两级树形结构，
//  选中元件后通过回调通知 MainFrame 更新绘图区待放置元件与属性表。
//
//  支持按名称搜索；自定义元件库由 ComponentCatalog::LoadCustomXml 导入。
// ============================================================

#pragma once

#include <wx/wx.h>
#include <wx/treectrl.h>

#include <functional>

#include "ComponentCatalog.h"

namespace se
{

class LibraryPanel : public wxPanel
{
public:
    explicit LibraryPanel(wxWindow* parent);

    // 选中元件变化时的回调（传 nullptr 表示取消选中）
    void SetOnSelect(std::function<void(const ComponentDef*)> callback)
    {
        m_onSelect = std::move(callback);
    }

    // 当前选中的元件，未选中返回 nullptr
    const ComponentDef* GetSelected() const { return m_selected; }

    void ClearSelection();
    void RefreshCatalog();

private:
    void BuildTree();
    void OnSelectionChanged(wxTreeEvent& event);
    void OnItemActivated(wxTreeEvent& event);

    wxTextCtrl* m_search = nullptr;
    wxStaticText* m_summary = nullptr;
    wxTreeCtrl* m_tree     = nullptr;
    wxImageList* m_images  = nullptr;

    const ComponentDef* m_selected = nullptr;

    std::function<void(const ComponentDef*)> m_onSelect;
};

} // namespace se
