// ============================================================
//  SchematicCanvas.h  —— 绘图区
//
//  文档内容由领域模型（NetModel.h）承载：
//      std::vector<ComponentInstance>  元件实例
//      std::vector<Wire>               导线
//  本类只负责：坐标变换、绘制、鼠标/键盘交互。
//
//  已实现：
//    * 世界坐标 / 屏幕坐标双向变换（缩放、平移）
//    * 网格（次网格 + 主网格）、原点标记
//    * 元件放置、选中（支持多选）、拖动移动、删除，位号自动编号
//    * 框选（橡皮筋）
//    * 撤销 / 重做（快照式，见下方说明）
//    * 导线绘制（点击式，正交折线，自动吸附到引脚/导线端点）
//    * 连接点（junction）自动判定与绘制
//    * 连通性求解 + 网络高亮（鼠标悬停即高亮整条网络）
//    * 网表生成 / 电气规则检查（委托 NetBuilder）
//
//  补充实现：应用内剪贴板、可编辑属性、文本与标记、PNG 视口导出。
//  CircuitTools 提供文档操作与组合逻辑静态求值，未提供模拟或时序模型。
// ============================================================

#pragma once

#include <wx/wx.h>
#include <wx/geometry.h>

#include <functional>
#include <map>
#include <vector>

#include "ComponentCatalog.h"
#include "NetModel.h"
#include "DigitalSimulator.h"
#include <wx/timer.h>

namespace se
{

class SchematicCanvas : public wxWindow
{
public:
    // 工具模式
    enum Tool
    {
        ToolSelect = 0,   // 选择 / 放置 / 拖动
        ToolWire,         // 画导线
        ToolText,
        ToolJunction,
        ToolNoConnect
    };

    explicit SchematicCanvas(wxWindow* parent);
    bool StartSimulation(wxString& error);
    bool StepSimulation(wxString& error);
    void StopSimulation();
    bool ToggleSimulationInput();
    bool SimulationActive() const { return m_simActive; }
    int SimulationValue(int net) const { return m_simulator.Value(net); }
    std::uint64_t SimulationTime() const {return m_simulator.Time();}
    wxPoint ScreenPoint(const wxPoint& world) const { auto p=ToScreen(wxPoint2DDouble(world.x,world.y));return wxPoint((int)p.m_x,(int)p.m_y); }


    // ---- 元件库联动 ----------------------------------------
    void                SetPendingComponent(const ComponentDef* def);
    const ComponentDef* GetPendingComponent() const { return m_pending; }

    // ---- 工具 ----------------------------------------------
    void SetTool(Tool tool);
    Tool GetTool() const { return m_tool; }

    // ---- 视图操作 ------------------------------------------
    void ZoomIn();
    void ZoomOut();
    void ZoomFit();
    void ZoomActual();

    bool IsGridVisible() const { return m_gridVisible; }
    bool IsSnapEnabled() const { return m_snapEnabled; }
    bool IsCrosshairVisible() const { return m_crosshair; }

    void SetGridVisible(bool on);
    void SetSnapEnabled(bool on);
    void SetCrosshairVisible(bool on);

    // ---- 状态查询 ------------------------------------------
    double GetZoom() const { return m_zoom; }
    int    GetGridStep() const { return m_gridStep; }
    int    GetComponentCount() const { return static_cast<int>(m_components.size()); }
    int    GetWireCount() const { return static_cast<int>(m_wires.size()); }

    bool IsDrawingWire() const { return m_wireDrawing; }
    bool IsDragging() const { return m_dragging; }

    // ---- 选中状态（支持多选）-------------------------------
    bool HasSelection() const { return !m_selComponents.empty() || !m_selWires.empty(); }
    int  GetSelectedComponentCount() const { return static_cast<int>(m_selComponents.size()); }
    int  GetSelectedWireCount() const { return static_cast<int>(m_selWires.size()); }
    int  GetSelectionCount() const { return GetSelectedComponentCount() + GetSelectedWireCount(); }

    // 仅在“恰好选中一个”时有效，否则返回 nullptr
    const ComponentInstance* GetSelectedComponent() const;
    const Wire*              GetSelectedWire() const;
    int                      GetSelectedComponentIndex() const;

    const std::vector<int>& GetSelectedComponents() const { return m_selComponents; }
    const std::vector<int>& GetSelectedWires() const { return m_selWires; }

    int  GetSelectedNetIndex() const;
    int  GetHighlightNetIndex() const { return m_highlightNet; }

    bool IsComponentSelected(int index) const;
    bool IsWireSelected(int index) const;

    // ---- 文档访问 / 派生数据 --------------------------------
    const std::vector<ComponentInstance>& GetComponents() const { return m_components; }
    const std::vector<Wire>&              GetWires() const { return m_wires; }

    const NetGraph& GetNetGraph();          // 需要时自动重算
    void            MarkNetGraphDirty() { m_netsDirty = true; }

    wxString BuildNetlist();
    wxString BuildErcReport();

    // ---- 文档装载 / 修改状态（任务 5）----------------------
    void SetDocument(const std::vector<ComponentInstance>& components,
                     const std::vector<Wire>&              wires,
                     int    gridStep,
                     bool   gridVisible,
                     bool   snapEnabled,
                     double zoom);

    bool IsModified() const { return m_modified; }
    void MarkSaved() { m_modified = false; }

    // ---- 编辑 ----------------------------------------------
    void CopySelection();
    void CutSelection();
    void PasteSelection();
    void Annotate();
    bool SetComponentProperty(const wxString& key, const wxString& value, wxString& error);
    void SetGridStep(int value);
    void SetWireBundle(int width);
    bool ExportPng(const wxString& path);
    void SelectAll();
    void DeleteSelection();
    void DeselectAll();
    void ClearAll(bool markModified = false);
    void ResetDocument();          // 新建：清内容 + 清撤销栈
    void FinishWire();

    // ---- 撤销 / 重做（快照式）-------------------------------
    //  每次“会改变文档的操作”之前先压一份完整快照。
    //  之所以不用 Command 模式：本工程元件量级只有几百，
    //  快照的开销可以忽略，而实现简单、不会漏掉任何一种操作。
    //  详见 README 第 6 节“多方案对比”。
    bool     CanUndo() const { return !m_undoStack.empty(); }
    bool     CanRedo() const { return !m_redoStack.empty(); }
    bool     Undo();
    bool     Redo();
    wxString GetUndoLabel() const;
    wxString GetRedoLabel() const;
    int      GetUndoDepth() const { return static_cast<int>(m_undoStack.size()); }
    int      GetRedoDepth() const { return static_cast<int>(m_redoStack.size()); }

    // ---- 回调（由 MainFrame 挂接，实现面板间联动）-----------
    std::function<void(const wxPoint&)>  OnCursorMoved;
    std::function<void()>                OnSelectionChanged;
    std::function<void()>                OnViewChanged;
    std::function<void(const wxString&)> OnStatusMessage;

private:
    // ---- 撤销快照 ------------------------------------------
    struct Snapshot
    {
        std::vector<ComponentInstance> components;
        std::vector<Wire>              wires;
        int                            nextComponentId = 0;
        int                            nextWireId      = 0;
        std::map<wxString, int>        refCount;
        wxString                       label;
    };

    // 事件
    void OnPaint(wxPaintEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnRightDown(wxMouseEvent& event);
    void OnLeftDClick(wxMouseEvent& event);
    void OnMiddleDown(wxMouseEvent& event);
    void OnMiddleUp(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnLeaveWindow(wxMouseEvent& event);

    // 坐标变换
    wxPoint2DDouble ToScreen(const wxPoint2DDouble& world) const;
    wxPoint2DDouble ToWorld(const wxPoint2DDouble& screen) const;
    wxPoint         SnapToGrid(const wxPoint& world) const;

    // 吸附：优先吸到引脚 / 导线端点，其次吸到网格
    wxPoint SnapForWiring(const wxPoint& world) const;
    bool    FindNearbyAttachPoint(const wxPoint& world, wxPoint& out) const;

    // 几何
    static wxRect BoxOf(const ComponentInstance& comp);
    int           HitTestComponent(const wxPoint& world) const;
    int           HitTestWire(const wxPoint& world) const;

    // 选中辅助
    void SetSelection(int componentIndex, bool add);
    void SetWireSelection(int wireIndex, bool add);
    void ClearSelectionInternal();

    // 编辑
    void PlaceComponent(const wxPoint& world);
    void HandleWireClick(const wxPoint& world);
    void AddWire(const wxPoint& a, const wxPoint& b);
    void BeginDrag(const wxPoint& world);
    void UpdateDrag(const wxPoint& world);
    void EndDrag();
    void BeginBandSelect(const wxPoint& world);
    void UpdateBandSelect(const wxPoint& world);
    void EndBandSelect();
    void Touch() { StopSimulation(); m_modified = true; }

    // 撤销
    void PushUndo(const wxString& label);
    void ApplySnapshot(const Snapshot& snapshot);

    // 绘制
    void DrawBackground(wxGraphicsContext* gc);
    void DrawGrid(wxGraphicsContext* gc);
    void DrawWires(wxGraphicsContext* gc);
    void DrawComponents(wxGraphicsContext* gc);
    void DrawSelectionBand(wxGraphicsContext* gc);
    void DrawGhost(wxGraphicsContext* gc);
    void DrawWirePreview(wxGraphicsContext* gc);
    void DrawCrosshair(wxGraphicsContext* gc);

    void SetZoomAt(double newZoom, const wxPoint2DDouble& screenAnchor);
    void EnsureOriginInitialised();
    void NotifyViewChanged();
    void NotifySelectionChanged();
    void UpdateHoverNet(const wxPoint& world);
    int  NetOfComponent(int compIndex);

    // 内容包围盒（元件 + 导线），空文档返回 false
    bool ComputeContentBounds(wxRect& bbox) const;
    void CenterOnContent();

    // ---- 文档修改状态 --------------------------------------
    bool m_modified = false;
    wxTimer m_simTimer;
    DigitalSimulator m_simulator;
    bool m_simActive = false;
    void OnSimTimer(wxTimerEvent& event);
    void SimulationChanged();


    // ---- 视图状态 ------------------------------------------
    double          m_zoom   = 1.0;
    wxPoint2DDouble m_origin = wxPoint2DDouble(0, 0);
    bool            m_originInit = false;
    wxSize          m_lastSize   = wxSize(0, 0);
    int             m_gridStep   = 10;

    bool m_gridVisible = true;
    bool m_snapEnabled = true;
    bool m_crosshair   = false;

    Tool                m_tool    = ToolSelect;
    const ComponentDef* m_pending = nullptr;

    std::vector<ComponentInstance> m_copiedComponents;
    std::vector<Wire> m_copiedWires;
    int m_pasteCount = 0;

    // ---- 文档内容 ------------------------------------------
    std::vector<ComponentInstance> m_components;
    std::vector<Wire>              m_wires;
    int                            m_nextComponentId = 0;
    int                            m_nextWireId      = 0;
    std::map<wxString, int>        m_refCount;

    // ---- 选中状态（多选，存下标，升序）---------------------
    std::vector<int> m_selComponents;
    std::vector<int> m_selWires;

    // ---- 拖动移动 ------------------------------------------
    //  除了移动元件本身，接在它引脚上的导线端点也要跟着走（橡皮筋连线），
    //  否则一拖动网络就断了。
    bool                 m_dragging   = false;
    bool                 m_dragMoved  = false;
    wxPoint              m_dragStartWorld;
    std::vector<ComponentInstance> m_dragBaseComponents;
    std::vector<Wire> m_dragBaseWires;

    // ---- 框选（橡皮筋）-------------------------------------
    bool    m_bandSelecting = false;
    bool    m_bandAdditive  = false;
    wxPoint m_bandStart;
    wxPoint m_bandEnd;

    // ---- 撤销 / 重做 ---------------------------------------
    std::vector<Snapshot> m_undoStack;
    std::vector<Snapshot> m_redoStack;

    // ---- 导线绘制状态 --------------------------------------
    int     m_bundleWidth = 1;
    bool    m_wireDrawing = false;
    wxPoint m_wireAnchor;       // 当前段起点（世界坐标）
    wxPoint m_wireCursor;       // 吸附后的光标位置

    // ---- 连通性缓存 ----------------------------------------
    NetGraph m_graph;
    bool     m_netsDirty   = true;
    int      m_highlightNet = -1;

    // ---- 交互状态 ------------------------------------------
    bool            m_panning = false;
    wxPoint         m_panStartScreen;
    wxPoint2DDouble m_panStartOrigin;
    wxPoint         m_cursorWorld;
    int             m_cursorScreenX = -1;
    int             m_cursorScreenY = -1;
    bool            m_hasCursor = false;
};

} // namespace se
