#include "LibraryPanel.h"

#include <wx/artprov.h>
#include <wx/statline.h>

#include "Ids.h"

namespace se
{

namespace
{

// ------------------------------------------------------------
// 给树节点挂的数据：指向上文静态元件定义（生命周期与程序相同）
// ------------------------------------------------------------
class ComponentItemData : public wxTreeItemData
{
public:
    explicit ComponentItemData(const ComponentDef* def) : m_def(def) {}

    const ComponentDef* Get() const { return m_def; }

private:
    const ComponentDef* m_def;
};

} // namespace

// ============================================================

LibraryPanel::LibraryPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(wxColour(248, 249, 251));

    // ---- 标题 ----
    wxStaticText* title = new wxStaticText(this, wxID_ANY, L"元件库");
    wxFont titleFont = title->GetFont();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);
    title->SetForegroundColour(wxColour(40, 60, 100));

    wxStaticText* subtitle = new wxStaticText(
        this, wxID_ANY,
        wxString::Format(L"共 %d 个元件 · 选中后到画布单击放置",
                         ComponentCatalog::ComponentCount()));
    subtitle->SetForegroundColour(wxColour(130, 135, 145));

    m_summary=subtitle;
    m_search=new wxTextCtrl(this,wxID_ANY);
    m_search->SetHint(L"搜索元件名称");
    wxStaticLine* line = new wxStaticLine(this);

    // ---- 树形控件 ----
    m_tree = new wxTreeCtrl(this,
                            ID_LIBRARY_TREE,
                            wxDefaultPosition,
                            wxDefaultSize,
                            wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT |
                            wxTR_SINGLE | wxTR_HIDE_ROOT | wxBORDER_NONE);

    m_tree->SetBackgroundColour(wxColour(255, 255, 255));

    // 用系统标准图标做分类 / 元件的图标
    m_images = new wxImageList(16, 16, true);
    m_images->Add(wxArtProvider::GetBitmap(wxART_FOLDER, wxART_LIST, wxSize(16, 16)));       // 0 = 分类
    m_images->Add(wxArtProvider::GetBitmap(wxART_NORMAL_FILE, wxART_LIST, wxSize(16, 16)));  // 1 = 元件
    m_tree->AssignImageList(m_images);   // Assign 由控件负责释放

    // ---- 布局 ----
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(title,    0, wxLEFT | wxRIGHT | wxTOP, 8);
    sizer->Add(subtitle, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);
    sizer->Add(m_search,0,wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM,8);
    sizer->Add(line,     0, wxEXPAND);
    sizer->Add(m_tree,   1, wxEXPAND | wxALL, 2);

    SetSizer(sizer);

    // ---- 事件 ----
    m_tree->Bind(wxEVT_TREE_SEL_CHANGED,  &LibraryPanel::OnSelectionChanged, this);
    m_tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &LibraryPanel::OnItemActivated, this);

    m_search->Bind(wxEVT_TEXT,[this](wxCommandEvent&){ClearSelection();BuildTree();});
    BuildTree();
    SetMinSize(wxSize(180, 200));
}

// ------------------------------------------------------------
void LibraryPanel::BuildTree()
{
    m_tree->DeleteAllItems();

    const wxTreeItemId root = m_tree->AddRoot(L"元件库");

    for (const CategoryDef& category : ComponentCatalog::Categories())
    {
        const wxTreeItemId categoryId =
            m_tree->AppendItem(root, category.name, 0, 0);

        for (const ComponentDef& item : category.items)
        {
            if(!m_search->GetValue().empty() && !item.name.Lower().Contains(m_search->GetValue().Lower())) continue;
            m_tree->AppendItem(categoryId,
                               item.name,
                               1, 1,
                               new ComponentItemData(&item));
        }

        m_tree->Expand(categoryId);
    }
}

// ------------------------------------------------------------
void LibraryPanel::OnSelectionChanged(wxTreeEvent& event)
{
    const wxTreeItemId item = event.GetItem();

    m_selected = nullptr;

    if (item.IsOk())
    {
        if (ComponentItemData* data =
                static_cast<ComponentItemData*>(m_tree->GetItemData(item)))
        {
            m_selected = data->Get();
        }
    }

    if (m_onSelect)
        m_onSelect(m_selected);
}

// ------------------------------------------------------------
// 双击元件 = 与单击选中同效（后续可改为“直接进入放置模式”）
// ------------------------------------------------------------
void LibraryPanel::OnItemActivated(wxTreeEvent& event)
{
    if (ComponentItemData* data =
            static_cast<ComponentItemData*>(m_tree->GetItemData(event.GetItem())))
    {
        m_selected = data->Get();

        if (m_onSelect)
            m_onSelect(m_selected);
    }
}

// ------------------------------------------------------------
void LibraryPanel::ClearSelection()
{
    m_tree->UnselectAll();
    m_selected = nullptr;

    if (m_onSelect)
        m_onSelect(nullptr);
}

} // namespace se
namespace se {
void LibraryPanel::RefreshCatalog() {
    ClearSelection();BuildTree();
    m_summary->SetLabel(wxString::Format(L"共 %d 个元件 · 选中后到画布单击放置",ComponentCatalog::ComponentCount()));
    Layout();
}
}
