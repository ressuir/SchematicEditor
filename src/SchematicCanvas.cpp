#include "EditingTools.h"
#include "SchematicCanvas.h"
#include "CircuitTools.h"
#include "KiCadExport.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace se
{

// ============================================================
//  局部工具
// ============================================================
namespace
{

// 点到线段的距离
double DistanceToSegment(const wxPoint& p, const wxPoint& a, const wxPoint& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;

    if (len2 <= 0.0)
        return std::hypot(static_cast<double>(p.x - a.x), static_cast<double>(p.y - a.y));

    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    t = std::max(0.0, std::min(1.0, t));

    const double px = a.x + t * dx;
    const double py = a.y + t * dy;

    return std::hypot(p.x - px, p.y - py);
}

const wxColour kWireColour    (20, 130, 60);
const wxColour kWireHotColour (230, 120, 20);
const wxColour kWireSelColour (200, 40, 40);
const wxColour kDanglingColour(220, 60, 60);

} // namespace

// ============================================================
//  构造 / 事件绑定
// ============================================================

SchematicCanvas::SchematicCanvas(wxWindow* parent)
    : wxWindow(parent,
               wxID_ANY,
               wxDefaultPosition,
               wxDefaultSize,
               wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS)
{
    SetName("schematic-canvas");
    m_simTimer.SetOwner(this);
    Bind(wxEVT_TIMER,&SchematicCanvas::OnSimTimer,this,m_simTimer.GetId());
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(wxColour(250, 250, 252));
    SetCursor(wxCursor(wxCURSOR_CROSS));
    SetMinSize(wxSize(200, 150));

    Bind(wxEVT_PAINT,             &SchematicCanvas::OnPaint,          this);
    Bind(wxEVT_ERASE_BACKGROUND,  &SchematicCanvas::OnEraseBackground, this);
    Bind(wxEVT_SIZE,              &SchematicCanvas::OnSize,           this);
    Bind(wxEVT_MOTION,            &SchematicCanvas::OnMouseMove,      this);
    Bind(wxEVT_LEFT_DOWN,         &SchematicCanvas::OnLeftDown,       this);
    Bind(wxEVT_LEFT_UP,           &SchematicCanvas::OnLeftUp,         this);
    Bind(wxEVT_RIGHT_DOWN,        &SchematicCanvas::OnRightDown,      this);
    Bind(wxEVT_LEFT_DCLICK,       &SchematicCanvas::OnLeftDClick,     this);
    Bind(wxEVT_MIDDLE_DOWN,       &SchematicCanvas::OnMiddleDown,     this);
    Bind(wxEVT_MIDDLE_UP,         &SchematicCanvas::OnMiddleUp,       this);
    Bind(wxEVT_MOUSEWHEEL,        &SchematicCanvas::OnMouseWheel,     this);
    Bind(wxEVT_KEY_DOWN,          &SchematicCanvas::OnKeyDown,        this);
    Bind(wxEVT_LEAVE_WINDOW,      &SchematicCanvas::OnLeaveWindow,    this);
}

// ============================================================
//  坐标变换
// ============================================================

wxPoint2DDouble SchematicCanvas::ToScreen(const wxPoint2DDouble& world) const
{
    return wxPoint2DDouble(m_origin.m_x + world.m_x * m_zoom,
                           m_origin.m_y + world.m_y * m_zoom);
}

wxPoint2DDouble SchematicCanvas::ToWorld(const wxPoint2DDouble& screen) const
{
    return wxPoint2DDouble((screen.m_x - m_origin.m_x) / m_zoom,
                           (screen.m_y - m_origin.m_y) / m_zoom);
}

wxPoint SchematicCanvas::SnapToGrid(const wxPoint& world) const
{
    if (!m_snapEnabled || m_gridStep <= 0)
        return world;

    const int g = m_gridStep;

    return wxPoint(static_cast<int>(std::lround(static_cast<double>(world.x) / g)) * g,
                   static_cast<int>(std::lround(static_cast<double>(world.y) / g)) * g);
}

// ------------------------------------------------------------
// 吸附优先级：引脚外端 > 导线端点 > 网格
// 吸附半径按屏幕像素换算，保证任何缩放下手感一致
// ------------------------------------------------------------
bool SchematicCanvas::FindNearbyAttachPoint(const wxPoint& world, wxPoint& out) const
{
    const double radius = std::max(4.0, 10.0 / m_zoom);

    double bestDist = radius;
    bool   found    = false;

    // 1) 引脚
    for (const ComponentInstance& comp : m_components)
    {
        for (size_t k = 0; k < comp.pins.size(); ++k)
        {
            const wxPoint p = PinPosition(comp, static_cast<int>(k));
            const double d = std::hypot(static_cast<double>(world.x - p.x),
                                        static_cast<double>(world.y - p.y));

            if (d < bestDist)
            {
                bestDist = d;
                out      = p;
                found    = true;
            }
        }
    }

    if (found)
        return true;

    // 2) 导线端点
    for (const Wire& w : m_wires)
    {
        for (const wxPoint& p : { w.a, w.b })
        {
            const double d = std::hypot(static_cast<double>(world.x - p.x),
                                        static_cast<double>(world.y - p.y));

            if (d < bestDist)
            {
                bestDist = d;
                out      = p;
                found    = true;
            }
        }
    }

    return found;
}

wxPoint SchematicCanvas::SnapForWiring(const wxPoint& world) const
{
    wxPoint attach;

    if (FindNearbyAttachPoint(world, attach))
        return attach;

    return SnapToGrid(world);
}

// ============================================================
//  命中测试
// ============================================================

wxRect SchematicCanvas::BoxOf(const ComponentInstance& comp)
{
    return wxRect(comp.pos.x, comp.pos.y, comp.w, comp.h);
}

int SchematicCanvas::HitTestComponent(const wxPoint& world) const
{
    for (int i = static_cast<int>(m_components.size()) - 1; i >= 0; --i)
    {
        wxRect box = BoxOf(m_components[i]);
        box.Inflate(kPinLen, kPinLen);

        if (box.Contains(world))
            return i;
    }

    return -1;
}

int SchematicCanvas::HitTestWire(const wxPoint& world) const
{
    const double tolerance = std::max(3.0, 6.0 / m_zoom);

    int    best     = -1;
    double bestDist = tolerance;

    for (size_t i = 0; i < m_wires.size(); ++i)
    {
        const double d = DistanceToSegment(world, m_wires[i].a, m_wires[i].b);

        if (d < bestDist)
        {
            bestDist = d;
            best     = static_cast<int>(i);
        }
    }

    return best;
}

void SchematicCanvas::EnsureOriginInitialised()
{
    if (m_originInit)
        return;

    const wxSize size = GetClientSize();

    if (size.x <= 0 || size.y <= 0)
        return;

    m_origin     = wxPoint2DDouble(size.x / 2.0, size.y / 2.0);
    m_originInit = true;
}

// ============================================================
//  连通性 / 网络
// ============================================================

const NetGraph& SchematicCanvas::GetNetGraph()
{
    if (m_netsDirty)
    {
        m_graph    = NetBuilder::Build(m_components, m_wires);
        m_netsDirty = false;
    }

    return m_graph;
}

int SchematicCanvas::NetOfComponent(int compIndex)
{
    GetNetGraph();

    if (compIndex < 0 || compIndex >= static_cast<int>(m_graph.pinNet.size()))
        return -1;

    for (int netIdx : m_graph.pinNet[compIndex])
    {
        if (netIdx >= 0)
            return netIdx;
    }

    return -1;
}

void SchematicCanvas::UpdateHoverNet(const wxPoint& world)
{
    GetNetGraph();

    int netIdx = -1;

    // 已有选中对象时，优先高亮选中对象所在的网络
    if (!m_selWires.empty())
        netIdx = m_graph.NetOfWire(m_selWires.front());
    else if (!m_selComponents.empty())
        netIdx = NetOfComponent(m_selComponents.front());

    // 否则高亮鼠标悬停位置的网络
    if (netIdx < 0)
    {
        const int wireIndex = HitTestWire(world);

        if (wireIndex >= 0)
        {
            netIdx = m_graph.NetOfWire(wireIndex);
        }
        else
        {
            const int compIndex = HitTestComponent(world);

            if (compIndex >= 0)
                netIdx = NetOfComponent(compIndex);
        }
    }

    if (netIdx != m_highlightNet)
    {
        m_highlightNet = netIdx;
        Refresh();
    }
}

// ------------------------------------------------------------
int SchematicCanvas::GetSelectedNetIndex() const
{
    if (!m_selWires.empty())
        return m_graph.NetOfWire(m_selWires.front());

    if (!m_selComponents.empty())
    {
        // NetOfComponent 是非 const 的（会触发重算），这里用缓存版本
        const int index = m_selComponents.front();

        if (index >= 0 && index < static_cast<int>(m_graph.pinNet.size()))
        {
            for (int netIdx : m_graph.pinNet[index])
            {
                if (netIdx >= 0)
                    return netIdx;
            }
        }
    }

    return -1;
}

// ------------------------------------------------------------
//  只有“恰好选中一个”时才有意义，供属性表显示详情
// ------------------------------------------------------------
const ComponentInstance* SchematicCanvas::GetSelectedComponent() const
{
    if (m_selComponents.size() != 1)
        return nullptr;

    const int index = m_selComponents.front();

    if (index < 0 || index >= static_cast<int>(m_components.size()))
        return nullptr;

    return &m_components[index];
}

const Wire* SchematicCanvas::GetSelectedWire() const
{
    if (m_selWires.size() != 1 || !m_selComponents.empty())
        return nullptr;

    const int index = m_selWires.front();

    if (index < 0 || index >= static_cast<int>(m_wires.size()))
        return nullptr;

    return &m_wires[index];
}

int SchematicCanvas::GetSelectedComponentIndex() const
{
    if (m_selComponents.size() != 1)
        return -1;

    const int index = m_selComponents.front();

    if (index < 0 || index >= static_cast<int>(m_components.size()))
        return -1;

    return index;
}

bool SchematicCanvas::IsComponentSelected(int index) const
{
    return std::find(m_selComponents.begin(), m_selComponents.end(), index) != m_selComponents.end();
}

bool SchematicCanvas::IsWireSelected(int index) const
{
    return std::find(m_selWires.begin(), m_selWires.end(), index) != m_selWires.end();
}

// ------------------------------------------------------------
//  选中状态维护
// ------------------------------------------------------------
void SchematicCanvas::ClearSelectionInternal()
{
    m_selComponents.clear();
    m_selWires.clear();
}

void SchematicCanvas::SetSelection(int componentIndex, bool add)
{
    if (!add)
        ClearSelectionInternal();

    if (componentIndex < 0)
        return;

    if (!IsComponentSelected(componentIndex))
    {
        m_selComponents.push_back(componentIndex);
        std::sort(m_selComponents.begin(), m_selComponents.end());
    }
}

void SchematicCanvas::SetWireSelection(int wireIndex, bool add)
{
    if (!add)
        ClearSelectionInternal();

    if (wireIndex < 0)
        return;

    if (!IsWireSelected(wireIndex))
    {
        m_selWires.push_back(wireIndex);
        std::sort(m_selWires.begin(), m_selWires.end());
    }
}

void SchematicCanvas::NotifySelectionChanged()
{
    if (OnSelectionChanged)
        OnSelectionChanged();
}

void SchematicCanvas::SelectAll()
{
    ClearSelectionInternal();

    for (size_t i = 0; i < m_components.size(); ++i)
        m_selComponents.push_back(static_cast<int>(i));

    for (size_t i = 0; i < m_wires.size(); ++i)
        m_selWires.push_back(static_cast<int>(i));

    UpdateHoverNet(m_cursorWorld);
    NotifySelectionChanged();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已全选：%d 个元件、%d 条线段",
                                         GetSelectedComponentCount(),
                                         GetSelectedWireCount()));

    Refresh();
}

wxString SchematicCanvas::BuildNetlist()
{
    return NetBuilder::ToNetlist(m_components, GetNetGraph());
}

wxString SchematicCanvas::BuildErcReport()
{
    return NetBuilder::RunErc(m_components, m_wires, GetNetGraph());
}

// ============================================================
//  绘制
// ============================================================

void SchematicCanvas::OnEraseBackground(wxEraseEvent&)
{
    // 空实现：双缓冲绘制已覆盖整个客户区
}

void SchematicCanvas::OnSize(wxSizeEvent& event)
{
    const wxSize size = GetClientSize();

    if (!m_originInit)
    {
        m_originInit = true;
        m_origin     = wxPoint2DDouble(size.x / 2.0, size.y / 2.0);
    }
    else if (m_lastSize.x > 0 && m_lastSize.y > 0)
    {
        // 窗口尺寸变化时让“可见中心”保持不动，内容不会漂移
        m_origin.m_x += (size.x - m_lastSize.x) / 2.0;
        m_origin.m_y += (size.y - m_lastSize.y) / 2.0;
    }

    m_lastSize = size;

    Refresh();
    event.Skip();
}

void SchematicCanvas::OnPaint(wxPaintEvent&)
{
    EnsureOriginInitialised();
    GetNetGraph();          // 必要时重算连通性

    wxAutoBufferedPaintDC dc(this);

    dc.SetBackground(wxBrush(wxColour(250, 250, 252)));
    dc.Clear();

    wxGraphicsContext* raw = wxGraphicsContext::Create(dc);

    if (!raw)
        return;

    std::unique_ptr<wxGraphicsContext> gc(raw);
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    DrawBackground(gc.get());
    DrawGrid(gc.get());
    DrawWires(gc.get());
    DrawComponents(gc.get());
    DrawSelectionBand(gc.get());
    DrawGhost(gc.get());
    DrawWirePreview(gc.get());
    DrawCrosshair(gc.get());
    if(m_simActive){
        gc->SetFont(wxFont(wxFontInfo(10)),wxColour(20,60,100));
        gc->DrawText(wxString::Format(L"数字仿真  t=%llu步（100ms/步）   0=蓝  1=绿  X=灰  ERR=红   双击输入或按空格切换",(unsigned long long)m_simulator.Time()),8,8);
    }
}

// ------------------------------------------------------------
//  框选橡皮筋
// ------------------------------------------------------------
void SchematicCanvas::DrawSelectionBand(wxGraphicsContext* gc)
{
    if (!m_bandSelecting)
        return;

    const wxPoint2DDouble a = ToScreen(wxPoint2DDouble(m_bandStart.x, m_bandStart.y));
    const wxPoint2DDouble b = ToScreen(wxPoint2DDouble(m_bandEnd.x, m_bandEnd.y));

    const double x = std::min(a.m_x, b.m_x);
    const double y = std::min(a.m_y, b.m_y);
    const double w = std::fabs(b.m_x - a.m_x);
    const double h = std::fabs(b.m_y - a.m_y);

    gc->SetBrush(wxBrush(wxColour(40, 110, 220, 40)));
    gc->SetPen(wxPen(wxColour(40, 110, 220), 1, wxPENSTYLE_SHORT_DASH));
    gc->DrawRectangle(x, y, w, h);
}

// ------------------------------------------------------------
void SchematicCanvas::DrawBackground(wxGraphicsContext* gc)
{
    const wxSize size = GetClientSize();

    // 世界坐标原点
    const wxPoint2DDouble origin = ToScreen(wxPoint2DDouble(0, 0));

    gc->SetPen(wxPen(wxColour(200, 205, 215), 1));
    gc->StrokeLine(origin.m_x - 6, origin.m_y, origin.m_x + 6, origin.m_y);
    gc->StrokeLine(origin.m_x, origin.m_y - 6, origin.m_x, origin.m_y + 6);

    if (!m_components.empty() || !m_wires.empty())
        return;

    gc->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_SWISS)), wxColour(150, 155, 165));

    const wxString line1 = L"从左侧「元件库」选元件，在画布上单击放置；按 W 切到导线工具画线";
    const wxString line2 = L"（滚轮缩放 · 中键拖动平移 · Delete 删除 · Esc 结束画线/取消选择）";

    wxDouble lw = 0, lh = 0, ld = 0, ll = 0;
    gc->GetTextExtent(line1, &lw, &lh, &ld, &ll);

    const double cx = size.x / 2.0;
    const double cy = size.y / 2.0;

    gc->DrawText(line1, cx - lw / 2.0, cy - lh * 1.2);
    gc->DrawText(line2, cx - lw / 2.0, cy + lh * 0.2);
}

// ------------------------------------------------------------
void SchematicCanvas::DrawGrid(wxGraphicsContext* gc)
{
    if (!m_gridVisible)
        return;

    const wxSize size = GetClientSize();

    const double minorPx = m_gridStep * m_zoom;
    const double majorPx = minorPx * 5.0;

    if (majorPx < 5.0)
        return;

    const wxPoint2DDouble topLeft     = ToWorld(wxPoint2DDouble(0, 0));
    const wxPoint2DDouble bottomRight = ToWorld(wxPoint2DDouble(size.x, size.y));

    const int maxLines = 2000;

    // ---- 主网格 ----
    {
        const double step = m_gridStep * 5.0;

        gc->SetPen(wxPen(wxColour(222, 227, 236), 1));

        const double x0 = std::floor(topLeft.m_x / step) * step;
        const double y0 = std::floor(topLeft.m_y / step) * step;

        int lines = 0;

        for (double x = x0; x <= bottomRight.m_x && lines < maxLines; x += step, ++lines)
        {
            const double sx = ToScreen(wxPoint2DDouble(x, 0)).m_x;
            gc->StrokeLine(sx, 0, sx, size.y);
        }

        lines = 0;

        for (double y = y0; y <= bottomRight.m_y && lines < maxLines; y += step, ++lines)
        {
            const double sy = ToScreen(wxPoint2DDouble(0, y)).m_y;
            gc->StrokeLine(0, sy, size.x, sy);
        }
    }

    // ---- 次网格 ----
    if (minorPx < 6.0)
        return;

    gc->SetPen(wxPen(wxColour(238, 241, 247), 1));

    const double step = m_gridStep;

    const double x0 = std::floor(topLeft.m_x / step) * step;
    const double y0 = std::floor(topLeft.m_y / step) * step;

    int lines = 0;

    for (double x = x0; x <= bottomRight.m_x && lines < maxLines; x += step, ++lines)
    {
        const double sx = ToScreen(wxPoint2DDouble(x, 0)).m_x;
        gc->StrokeLine(sx, 0, sx, size.y);
    }

    lines = 0;

    for (double y = y0; y <= bottomRight.m_y && lines < maxLines; y += step, ++lines)
    {
        const double sy = ToScreen(wxPoint2DDouble(0, y)).m_y;
        gc->StrokeLine(0, sy, size.x, sy);
    }
}

// ------------------------------------------------------------
//  导线 / 连接点 / 悬空端点
// ------------------------------------------------------------
void SchematicCanvas::DrawWires(wxGraphicsContext* gc)
{
    if (m_wires.empty())
        return;

    // ---- 导线本体 ----
    for (size_t i = 0; i < m_wires.size(); ++i)
    {
        const Wire& w = m_wires[i];

        const wxPoint2DDouble a = ToScreen(wxPoint2DDouble(w.a.x, w.a.y));
        const wxPoint2DDouble b = ToScreen(wxPoint2DDouble(w.b.x, w.b.y));

        const bool selected = IsWireSelected(static_cast<int>(i));
        const bool inNet    = (m_highlightNet >= 0 && m_graph.NetOfWire(static_cast<int>(i)) == m_highlightNet);

        wxColour colour = kWireColour;
        if(m_simActive){int v=m_simulator.Value(m_graph.NetOfWire((int)i));colour=v==0?wxColour(45,90,210):v==1?wxColour(0,155,70):v==2?wxColour(215,30,30):wxColour(140,140,140);}
        double   width  = std::max(1.0, 2.0 * m_zoom);

        if (inNet)
        {
            colour = kWireHotColour;
            width  = std::max(2.0, 3.0 * m_zoom);
        }

        if (selected)
        {
            colour = kWireSelColour;
            width  = std::max(2.0, 3.0 * m_zoom);
        }

        gc->SetPen(wxPen(colour, width));
        gc->StrokeLine(a.m_x, a.m_y, b.m_x, b.m_y);
    }

    // ---- 连接点（实心圆）----
    const double jr = std::max(2.0, 3.5 * m_zoom);

    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(kWireColour));

    for (const wxPoint& j : m_graph.junctions)
    {
        const wxPoint2DDouble s = ToScreen(wxPoint2DDouble(j.x, j.y));
        gc->DrawEllipse(s.m_x - jr, s.m_y - jr, jr * 2, jr * 2);
    }

    // ---- 悬空端点（空心小方块，ERC 的视觉提示）----
    const double dr = std::max(2.0, 3.0 * m_zoom);

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(kDanglingColour, std::max(1.0, 1.4 * m_zoom)));

    for (size_t i = 0; i < m_wires.size(); ++i)
    {
        const Wire& w = m_wires[i];

        for (const wxPoint& p : { w.a, w.b })
        {
            if (IsEndpointConnected(m_components, m_wires, p, static_cast<int>(i)))
                continue;

            const wxPoint2DDouble s = ToScreen(wxPoint2DDouble(p.x, p.y));
            gc->DrawRectangle(s.m_x - dr, s.m_y - dr, dr * 2, dr * 2);
        }
    }
}

// ------------------------------------------------------------
//  元件
// ------------------------------------------------------------
void SchematicCanvas::DrawComponents(wxGraphicsContext* gc)
{
    wxFont font(wxFontInfo(8).Family(wxFONTFAMILY_SWISS));
    wxFont smallFont(wxFontInfo(7).Family(wxFONTFAMILY_SWISS));

    const bool showText    = m_zoom >= 0.45;
    const bool showPinName = m_zoom >= 0.75;

    for (size_t i = 0; i < m_components.size(); ++i)
    {
        const ComponentInstance& comp = m_components[i];
        if(comp.name==L"连接点" || comp.name==L"未连接标记") {
            const auto at=ToScreen(wxPoint2DDouble(comp.pos.x,comp.pos.y));
            gc->SetPen(wxPen(comp.name==L"连接点"?kWireColour:kDanglingColour,2));
            if(comp.name==L"连接点") {gc->SetBrush(wxBrush(kWireColour));gc->DrawEllipse(at.m_x-3,at.m_y-3,6,6);}
            else {gc->StrokeLine(at.m_x-4,at.m_y-4,at.m_x+4,at.m_y+4);gc->StrokeLine(at.m_x+4,at.m_y-4,at.m_x-4,at.m_y+4);}
            continue;
        }
        const wxPoint2DDouble tl = ToScreen(wxPoint2DDouble(comp.pos.x, comp.pos.y));
        const double w = comp.w * m_zoom;
        const double h = comp.h * m_zoom;

        const bool selected = IsComponentSelected(static_cast<int>(i));
        const bool inNet    = (m_highlightNet >= 0 && NetOfComponent(static_cast<int>(i)) == m_highlightNet);

        // ---- 引脚 ----
        for (size_t k = 0; k < comp.pins.size(); ++k)
        {
            const wxPoint anchorW = PinAnchor(comp, static_cast<int>(k));
            const wxPoint outerW  = PinPosition(comp, static_cast<int>(k));

            const wxPoint2DDouble anchor = ToScreen(wxPoint2DDouble(anchorW.x, anchorW.y));
            const wxPoint2DDouble outer  = ToScreen(wxPoint2DDouble(outerW.x, outerW.y));

            const int netIdx = (i < m_graph.pinNet.size() && k < m_graph.pinNet[i].size())
                                   ? m_graph.pinNet[i][k]
                                   : -1;

            const bool pinHot = (m_highlightNet >= 0 && netIdx == m_highlightNet);
            if(m_simActive){int v=m_simulator.Value(netIdx);gc->SetFont(smallFont,v==1?wxColour(0,135,50):v==2?wxColour(200,0,0):wxColour(35,75,160));gc->DrawText(LogicValueText(v),outer.m_x+4,outer.m_y-14);}

            gc->SetPen(wxPen(pinHot ? kWireHotColour : wxColour(90, 100, 120),
                             std::max(1.0, (pinHot ? 2.4 : 1.5) * m_zoom)));
            gc->StrokeLine(anchor.m_x, anchor.m_y, outer.m_x, outer.m_y);

            if (pinHot)
            {
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(wxBrush(kWireHotColour));
                gc->DrawEllipse(outer.m_x - 3.0, outer.m_y - 3.0, 6.0, 6.0);
            }

            if (!showPinName)
                continue;

            const Pin& pin = comp.pins[k];

            // 注意：必须先 SetFont 再 GetTextExtent，
            // 否则 wxGDIPlusContext 会断言 "no valid font set"
            gc->SetFont(smallFont, wxColour(110, 120, 135));

            wxDouble tw = 0, th = 0, td = 0, tel = 0;
            gc->GetTextExtent(pin.name, &tw, &th, &td, &tel);

            switch (pin.side)
            {
                case Side::Left:
                    gc->DrawText(pin.name, anchor.m_x + 3.0, anchor.m_y - th / 2.0);
                    break;

                case Side::Right:
                    gc->DrawText(pin.name, anchor.m_x - tw - 3.0, anchor.m_y - th / 2.0);
                    break;

                case Side::Top:
                    gc->DrawText(pin.name, anchor.m_x - tw / 2.0, anchor.m_y + 3.0);
                    break;

                case Side::Bottom:
                default:
                    gc->DrawText(pin.name, anchor.m_x - tw / 2.0, anchor.m_y - th - 3.0);
                    break;
            }
        }

        // ---- 本体 ----
        wxColour fill = wxColour(255, 252, 235);

        if (inNet)
            fill = wxColour(255, 238, 210);

        if (selected)
            fill = wxColour(255, 244, 200);

        gc->SetBrush(wxBrush(fill));
        gc->SetPen(wxPen(selected ? wxColour(210, 120, 0) : wxColour(70, 80, 100),
                         std::max(1.0, (selected ? 2.5 : 1.5) * m_zoom)));
        gc->DrawRectangle(tl.m_x, tl.m_y, w, h);

        // ---- 选中虚框 ----
        if (selected)
        {
            gc->SetPen(wxPen(wxColour(40, 110, 220), 1, wxPENSTYLE_SHORT_DASH));
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->DrawRectangle(tl.m_x - kPinLen * m_zoom - 2, tl.m_y - 2,
                              w + 2 * kPinLen * m_zoom + 4, h + 4);
        }

        if (!showText)
            continue;

        // ---- 位号 ----
        gc->SetFont(smallFont, wxColour(40, 90, 170));

        wxDouble tw = 0, th = 0, td = 0, tel = 0;
        gc->GetTextExtent(comp.refdes, &tw, &th, &td, &tel);
        gc->DrawText(comp.refdes, tl.m_x + w / 2.0 - tw / 2.0, tl.m_y - th - 1);

        // ---- 元件名 ----
        gc->SetFont(font, wxColour(45, 50, 60));

        const wxString displayName = comp.name == L"文本" ? comp.value :
            ((comp.name == L"网络标签" || comp.name == L"全局标签") && !comp.value.empty() ? comp.value : comp.name);
        gc->GetTextExtent(displayName, &tw, &th, &td, &tel);

        // 有引脚名的元件（逻辑门）把名字往下挪一点，避免和引脚名重叠
        const double nameY = comp.pins.size() > 2
                                 ? tl.m_y + h * 0.5 - th / 2.0
                                 : tl.m_y + h * 0.5 - th / 2.0;

        gc->DrawText(displayName, tl.m_x + w / 2.0 - tw / 2.0, nameY);
    }
}

// ------------------------------------------------------------
//  待放置元件的“幽灵”预览
// ------------------------------------------------------------
void SchematicCanvas::DrawGhost(wxGraphicsContext* gc)
{
    if (!m_pending || !m_hasCursor || m_tool != ToolSelect)
        return;

    const int w = m_pending->w;
    const int h = m_pending->h;

    const wxPoint center  = SnapToGrid(m_cursorWorld);
    const wxPoint topLeft = SnapToGrid(wxPoint(center.x - w / 2, center.y - h / 2));

    const wxPoint2DDouble tl = ToScreen(wxPoint2DDouble(topLeft.x, topLeft.y));

    const double sw = w * m_zoom;
    const double sh = h * m_zoom;

    gc->SetBrush(wxBrush(wxColour(210, 235, 255, 110)));
    gc->SetPen(wxPen(wxColour(40, 110, 220), 1, wxPENSTYLE_SHORT_DASH));
    gc->DrawRectangle(tl.m_x, tl.m_y, sw, sh);

    gc->SetFont(wxFont(wxFontInfo(8).Family(wxFONTFAMILY_SWISS)), wxColour(40, 110, 220));

    wxDouble tw = 0, th = 0, td = 0, tel = 0;
    gc->GetTextExtent(m_pending->name, &tw, &th, &td, &tel);
    gc->DrawText(m_pending->name, tl.m_x + sw / 2.0 - tw / 2.0, tl.m_y + sh / 2.0 - th / 2.0);
}

// ------------------------------------------------------------
//  导线绘制预览（正交 L 形）+ 网络名提示
// ------------------------------------------------------------
void SchematicCanvas::DrawWirePreview(wxGraphicsContext* gc)
{
    if (m_wireDrawing)
    {
        const wxPoint2DDouble a = ToScreen(wxPoint2DDouble(m_wireAnchor.x, m_wireAnchor.y));
        const wxPoint2DDouble c = ToScreen(wxPoint2DDouble(m_wireCursor.x, m_wireCursor.y));

        gc->SetPen(wxPen(kWireColour, std::max(2.0, 2.4 * m_zoom), wxPENSTYLE_SHORT_DASH));
        gc->StrokeLine(a.m_x, a.m_y, c.m_x, a.m_y);   // 先横
        gc->StrokeLine(c.m_x, a.m_y, c.m_x, c.m_y);   // 后竖

        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(kWireColour));
        gc->DrawEllipse(a.m_x - 3.0, a.m_y - 3.0, 6.0, 6.0);
    }

    // 高亮网络时在光标旁显示网络名
    if (m_highlightNet >= 0 && m_highlightNet < static_cast<int>(m_graph.nets.size()) && m_hasCursor)
    {
        const Net& net = m_graph.nets[m_highlightNet];

        const wxString label = wxString::Format(L"%s (%d 引脚)", net.name, static_cast<int>(net.pins.size()));

        gc->SetFont(wxFont(wxFontInfo(8).Family(wxFONTFAMILY_SWISS)), kWireHotColour);

        wxDouble tw = 0, th = 0, td = 0, tel = 0;
        gc->GetTextExtent(label, &tw, &th, &td, &tel);

        const double x = m_cursorScreenX + 14.0;
        const double y = m_cursorScreenY + 14.0;

        gc->SetBrush(wxBrush(wxColour(255, 255, 255, 220)));
        gc->SetPen(wxPen(kWireHotColour, 1));
        gc->DrawRectangle(x - 4, y - 2, tw + 8, th + 4);

        gc->DrawText(label, x, y);
    }
}

// ------------------------------------------------------------
void SchematicCanvas::DrawCrosshair(wxGraphicsContext* gc)
{
    if (!m_crosshair || !m_hasCursor)
        return;

    const wxSize size = GetClientSize();

    gc->SetPen(wxPen(wxColour(120, 170, 230), 1, wxPENSTYLE_DOT));
    gc->StrokeLine(0, m_cursorScreenY, size.x, m_cursorScreenY);
    gc->StrokeLine(m_cursorScreenX, 0, m_cursorScreenX, size.y);
}

// ============================================================
//  鼠标 / 键盘
// ============================================================

void SchematicCanvas::OnMouseMove(wxMouseEvent& event)
{
    if (m_panning)
    {
        const wxPoint delta = event.GetPosition() - m_panStartScreen;

        m_origin.m_x = m_panStartOrigin.m_x + delta.x;
        m_origin.m_y = m_panStartOrigin.m_y + delta.y;

        Refresh();
    }

    const wxPoint2DDouble world = ToWorld(wxPoint2DDouble(event.GetX(), event.GetY()));

    const wxPoint raw(static_cast<int>(std::lround(world.m_x)),
                      static_cast<int>(std::lround(world.m_y)));

    m_cursorScreenX = event.GetX();
    m_cursorScreenY = event.GetY();
    m_hasCursor     = true;

    if (m_dragging)
    {
        UpdateDrag(raw);
    }
    else if (m_bandSelecting)
    {
        UpdateBandSelect(raw);
    }
    else if (m_wireDrawing)
    {
        m_wireCursor  = SnapForWiring(raw);
        m_cursorWorld = m_wireCursor;
        Refresh();
    }
    else
    {
        m_cursorWorld = SnapToGrid(raw);

        if (m_tool == ToolSelect)
            UpdateHoverNet(raw);
    }

    if (OnCursorMoved)
        OnCursorMoved(m_cursorWorld);

    if (m_pending && m_tool == ToolSelect)
        Refresh();
}

void SchematicCanvas::OnLeftDown(wxMouseEvent& event)
{
    SetFocus();

    const wxPoint2DDouble world = ToWorld(wxPoint2DDouble(event.GetX(), event.GetY()));
    const wxPoint point(static_cast<int>(std::lround(world.m_x)),
                        static_cast<int>(std::lround(world.m_y)));

    const bool add = event.ControlDown() || event.ShiftDown();

    if (m_tool == ToolJunction || m_tool == ToolNoConnect) {
        PushUndo(L"放置标记");ComponentInstance c;c.id=++m_nextComponentId;
        c.name=m_tool==ToolJunction?L"连接点":L"未连接标记";
        c.refdes=NextReference(m_components,c.name);c.pos=SnapForWiring(point);c.w=0;c.h=0;
        if(m_tool==ToolJunction)c.pins={{"J",PinDir::Passive,Side::Left,0,1}};
        m_components.push_back(c);m_netsDirty=true;Touch();NotifySelectionChanged();Refresh();return;
    }
    if (m_tool == ToolText) {
        const wxString value = wxGetTextFromUser(L"输入文本", L"放置文本", wxEmptyString, this);
        if (!value.empty()) {
            PushUndo(L"放置文本");
            ComponentInstance c; c.id=++m_nextComponentId; c.name=L"文本";
            c.refdes=NextReference(m_components,c.name); c.value=value;
            c.pos=SnapToGrid(point); c.w=std::max(60, static_cast<int>(value.length())*14); c.h=30;
            m_components.push_back(c); m_netsDirty=true; Touch();
            NotifySelectionChanged(); Refresh();
        }
        return;
    }
    // ---- 导线工具 ----
    if (m_tool == ToolWire)
    {
        HandleWireClick(point);
        return;
    }

    // ---- 点到已有元件：选中并开始拖动 ----
    const int compIndex = HitTestComponent(point);

    if (compIndex >= 0)
    {
        if (add)
        {
            // Ctrl/Shift 点击：切换该元件的选中状态
            if (IsComponentSelected(compIndex))
            {
                m_selComponents.erase(
                    std::remove(m_selComponents.begin(), m_selComponents.end(), compIndex),
                    m_selComponents.end());
            }
            else
            {
                SetSelection(compIndex, true);
            }
        }
        else if (!IsComponentSelected(compIndex))
        {
            SetSelection(compIndex, false);
        }

        UpdateHoverNet(point);
        NotifySelectionChanged();
        Refresh();

        // 按住已选中的元件 → 准备拖动（Shift/Ctrl 时只做加减选，不拖动）
        if (!add && IsComponentSelected(compIndex))
            BeginDrag(point);

        return;
    }

    // ---- 点到已有导线 ----
    const int wireIndex = HitTestWire(point);

    if (wireIndex >= 0)
    {
        if (add && IsWireSelected(wireIndex))
        {
            m_selWires.erase(std::remove(m_selWires.begin(), m_selWires.end(), wireIndex),
                             m_selWires.end());
        }
        else
        {
            SetWireSelection(wireIndex, add);
        }

        UpdateHoverNet(point);
        NotifySelectionChanged();
        Refresh();
        return;
    }

    // ---- 放置待放置元件 ----
    if (m_pending)
    {
        PlaceComponent(point);
        return;
    }

    // ---- 空白处：开始框选 ----
    BeginBandSelect(point);
}

// ------------------------------------------------------------
//  左键抬起：结束拖动 / 框选
// ------------------------------------------------------------
void SchematicCanvas::OnLeftUp(wxMouseEvent&)
{
    if (m_dragging)
    {
        EndDrag();
        return;
    }

    if (m_bandSelecting)
        EndBandSelect();
}

void SchematicCanvas::OnRightDown(wxMouseEvent&)
{
    // 右键结束当前导线
    if (m_wireDrawing)
        FinishWire();
}

void SchematicCanvas::OnLeftDClick(wxMouseEvent&)
{
    if(m_simActive && ToggleSimulationInput())return;
    if (m_wireDrawing)
        FinishWire();
}

void SchematicCanvas::OnMiddleDown(wxMouseEvent& event)
{
    m_panning        = true;
    m_panStartScreen = event.GetPosition();
    m_panStartOrigin = m_origin;

    SetCursor(wxCursor(wxCURSOR_SIZING));
}

void SchematicCanvas::OnMiddleUp(wxMouseEvent&)
{
    m_panning = false;
    SetCursor(wxCursor(wxCURSOR_CROSS));
}

void SchematicCanvas::OnMouseWheel(wxMouseEvent& event)
{
    const wxPoint2DDouble anchor(event.GetX(), event.GetY());

    const double factor = (event.GetWheelRotation() > 0) ? 1.15 : (1.0 / 1.15);

    SetZoomAt(m_zoom * factor, anchor);
}

void SchematicCanvas::OnLeaveWindow(wxMouseEvent& event)
{
    m_hasCursor = false;

    if (m_highlightNet != -1)
        m_highlightNet = -1;

    Refresh();
    event.Skip();
}

void SchematicCanvas::OnKeyDown(wxKeyEvent& event)
{
    if (event.ControlDown()) {
        const int key = event.GetKeyCode();
        if (key == 'C') { CopySelection(); return; }
        if (key == 'X') { CutSelection(); return; }
        if (key == 'V') { PasteSelection(); return; }
    }
    switch (event.GetKeyCode())
    {
        case WXK_SPACE: if(m_simActive){ToggleSimulationInput();return;}break;
        case WXK_DELETE:
            DeleteSelection();
            return;

        case WXK_ESCAPE:
            if (m_wireDrawing)
            {
                FinishWire();
                return;
            }

            m_pending = nullptr;
            m_tool    = ToolSelect;
            DeselectAll();

            if (OnSelectionChanged)
                OnSelectionChanged();

            NotifyViewChanged();
            return;

        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
            if (m_wireDrawing)
            {
                FinishWire();
                return;
            }
            break;

        case WXK_ADD:
        case WXK_NUMPAD_ADD:
            ZoomIn();
            return;

        case WXK_SUBTRACT:
        case WXK_NUMPAD_SUBTRACT:
            ZoomOut();
            return;

        case WXK_HOME:
            ZoomFit();
            return;

        case WXK_LEFT:
        case WXK_RIGHT:
        case WXK_UP:
        case WXK_DOWN:
        {
            const int step = event.ShiftDown() ? 80 : 20;

            if (event.GetKeyCode() == WXK_LEFT)  m_origin.m_x += step;
            if (event.GetKeyCode() == WXK_RIGHT) m_origin.m_x -= step;
            if (event.GetKeyCode() == WXK_UP)    m_origin.m_y += step;
            if (event.GetKeyCode() == WXK_DOWN)  m_origin.m_y -= step;

            Refresh();
            return;
        }

        default:
            break;
    }

    event.Skip();
}

// ============================================================
//  撤销 / 重做（快照式）
// ============================================================

void SchematicCanvas::PushUndo(const wxString& label)
{
    Snapshot snapshot;
    snapshot.components    = m_components;
    snapshot.wires         = m_wires;
    snapshot.nextComponentId = m_nextComponentId;
    snapshot.nextWireId      = m_nextWireId;
    snapshot.refCount        = m_refCount;
    snapshot.label           = label;

    m_undoStack.push_back(std::move(snapshot));

    // 只保留最近 100 步
    if (m_undoStack.size() > 100)
        m_undoStack.erase(m_undoStack.begin());

    // 新的操作让“重做”失效
    m_redoStack.clear();
}

void SchematicCanvas::ApplySnapshot(const Snapshot& snapshot)
{
    StopSimulation();
    m_components    = snapshot.components;
    m_wires         = snapshot.wires;
    m_nextComponentId = snapshot.nextComponentId;
    m_nextWireId      = snapshot.nextWireId;
    m_refCount        = snapshot.refCount;

    ClearSelectionInternal();
    m_wireDrawing  = false;
    m_dragging     = false;
    m_bandSelecting = false;
    m_highlightNet = -1;
    m_netsDirty    = true;
    m_modified     = true;

    GetNetGraph();
    UpdateHoverNet(m_cursorWorld);
    NotifySelectionChanged();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已恢复：%s（元件 %d、导线 %d）",
                                         snapshot.label,
                                         GetComponentCount(),
                                         GetWireCount()));

    Refresh();
}

bool SchematicCanvas::Undo()
{
    if (m_undoStack.empty())
        return false;

    // 先把“当前状态”压进重做栈
    Snapshot current;
    current.components    = m_components;
    current.wires         = m_wires;
    current.nextComponentId = m_nextComponentId;
    current.nextWireId      = m_nextWireId;
    current.refCount        = m_refCount;
    current.label           = GetUndoLabel();
    m_redoStack.push_back(std::move(current));

    const Snapshot snapshot = m_undoStack.back();
    m_undoStack.pop_back();

    ApplySnapshot(snapshot);
    return true;
}

bool SchematicCanvas::Redo()
{
    if (m_redoStack.empty())
        return false;

    Snapshot current;
    current.components    = m_components;
    current.wires         = m_wires;
    current.nextComponentId = m_nextComponentId;
    current.nextWireId      = m_nextWireId;
    current.refCount        = m_refCount;
    current.label           = GetRedoLabel();
    m_undoStack.push_back(std::move(current));

    const Snapshot snapshot = m_redoStack.back();
    m_redoStack.pop_back();

    ApplySnapshot(snapshot);
    return true;
}

wxString SchematicCanvas::GetUndoLabel() const
{
    if (m_undoStack.empty())
        return wxEmptyString;

    return m_undoStack.back().label;
}

wxString SchematicCanvas::GetRedoLabel() const
{
    if (m_redoStack.empty())
        return wxEmptyString;

    return m_redoStack.back().label;
}

// ============================================================
//  编辑操作
// ============================================================

void SchematicCanvas::PlaceComponent(const wxPoint& world)
{
    if (!m_pending)
        return;

    ComponentInstance comp;
    PushUndo(wxString::Format(L"放置 %s", m_pending->name));
    comp.id   = ++m_nextComponentId;
    comp.name = m_pending->name;
    comp.w    = m_pending->w;
    comp.h    = m_pending->h;
    comp.pins = MakePins(m_pending->name, m_pending->kind,
                         m_pending->inputs, m_pending->outputs);
    comp.logicModel=m_pending->logicModel;comp.truthTable=m_pending->truthTable;comp.delayTicks=m_pending->delayTicks;
    comp.footprint=DefaultFootprint(comp.pins.size());

    // 以光标为中心；中心与左上角都对齐到网格，
    // 这样引脚（PinAnchor 内部也对齐到 10）一定落在网格上，画线不会产生小折角
    const wxPoint center  = SnapToGrid(world);
    const wxPoint topLeft = SnapToGrid(wxPoint(center.x - comp.w / 2, center.y - comp.h / 2));

    comp.pos = topLeft;

    comp.refdes = NextReference(m_components, comp.name);
    if (comp.name == L"输入引脚" || comp.name == L"常量") comp.value = "0";

    m_components.push_back(comp);

    SetSelection(static_cast<int>(m_components.size()) - 1, false);
    m_netsDirty = true;
    Touch();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已放置 %s（位号 %s，%d 个引脚）",
                                         comp.name, comp.refdes,
                                         static_cast<int>(comp.pins.size())));

    UpdateHoverNet(comp.pos);
    NotifySelectionChanged();

    Refresh();
}

// ------------------------------------------------------------
//  拖动移动
// ------------------------------------------------------------
void SchematicCanvas::BeginDrag(const wxPoint& world)
{
    if(m_selComponents.empty())return;
    m_dragging=true;m_dragMoved=false;m_dragStartWorld=world;
    m_dragBaseComponents=m_components;m_dragBaseWires=m_wires;
    SetCursor(wxCursor(wxCURSOR_SIZING));
}
void SchematicCanvas::UpdateDrag(const wxPoint& world)
{
    if(!m_dragging)return;
    const auto delta=SnapToGrid(world-m_dragStartWorld);
    if(delta==wxPoint(0,0)&&!m_dragMoved)return;
    if(!m_dragMoved){StopSimulation();PushUndo(L"移动元件及连接线");m_dragMoved=true;}
    m_components=m_dragBaseComponents;m_wires=m_dragBaseWires;
    MoveConnectedComponents(m_components,m_wires,m_selComponents,delta);
    m_nextWireId=0;for(const auto& w:m_wires)m_nextWireId=std::max(m_nextWireId,w.id);
    m_selWires.clear();m_netsDirty=true;GetNetGraph();m_cursorWorld=world;
    if(OnCursorMoved)OnCursorMoved(world);Refresh();
}
void SchematicCanvas::EndDrag()
{
    if(!m_dragging)return;m_dragging=false;SetCursor(wxCursor(wxCURSOR_CROSS));
    if(m_dragMoved){Touch();NotifySelectionChanged();if(OnStatusMessage)OnStatusMessage(L"已移动元件并保持连接节点；可用Ctrl+Z撤销");}
    m_dragMoved=false;m_dragBaseComponents.clear();m_dragBaseWires.clear();Refresh();
}

// ------------------------------------------------------------
//  框选（橡皮筋）
// ------------------------------------------------------------
void SchematicCanvas::BeginBandSelect(const wxPoint& world)
{
    m_bandSelecting = true;
    m_bandAdditive  = false;      // 由 OnLeftDown 决定是否累加，这里默认替换
    m_bandStart     = world;
    m_bandEnd       = world;

    if (!m_selComponents.empty() || !m_selWires.empty())
    {
        ClearSelectionInternal();
        UpdateHoverNet(world);
        NotifySelectionChanged();
    }

    Refresh();
}

void SchematicCanvas::UpdateBandSelect(const wxPoint& world)
{
    if (!m_bandSelecting)
        return;

    m_bandEnd = world;
    Refresh();
}

void SchematicCanvas::EndBandSelect()
{
    if (!m_bandSelecting)
        return;

    m_bandSelecting = false;

    const wxRect band(std::min(m_bandStart.x, m_bandEnd.x),
                      std::min(m_bandStart.y, m_bandEnd.y),
                      std::abs(m_bandEnd.x - m_bandStart.x),
                      std::abs(m_bandEnd.y - m_bandStart.y));

    // 太小就当成“空白处单击”→ 取消选中
    if (band.width < 3 && band.height < 3)
    {
        ClearSelectionInternal();
        UpdateHoverNet(m_bandEnd);
        NotifySelectionChanged();
        Refresh();
        return;
    }

    ClearSelectionInternal();

    for (size_t i = 0; i < m_components.size(); ++i)
    {
        if (band.Intersects(BoxOf(m_components[i])))
            m_selComponents.push_back(static_cast<int>(i));
    }

    for (size_t i = 0; i < m_wires.size(); ++i)
    {
        const wxRect wireBox(std::min(m_wires[i].a.x, m_wires[i].b.x),
                             std::min(m_wires[i].a.y, m_wires[i].b.y),
                             std::abs(m_wires[i].b.x - m_wires[i].a.x) + 1,
                             std::abs(m_wires[i].b.y - m_wires[i].a.y) + 1);

        if (band.Intersects(wireBox))
            m_selWires.push_back(static_cast<int>(i));
    }

    UpdateHoverNet(m_bandEnd);

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"框选到 %d 个元件、%d 条线段",
                                         GetSelectedComponentCount(),
                                         GetSelectedWireCount()));

    NotifySelectionChanged();
    Refresh();
}

// ------------------------------------------------------------
void SchematicCanvas::AddWire(const wxPoint& a, const wxPoint& b)
{
    if (a == b)
        return;

    Wire wire;
    wire.id = ++m_nextWireId;
    wire.a  = a;
    wire.b  = b;

    m_wires.push_back(wire);
    for(int lane=1;lane<m_bundleWidth;++lane) {
        Wire copy=wire;copy.id=++m_nextWireId;const wxPoint shift(lane*m_gridStep,lane*m_gridStep);
        copy.a+=shift;copy.b+=shift;m_wires.push_back(copy);
    }
    m_netsDirty = true;
    Touch();
}

void SchematicCanvas::HandleWireClick(const wxPoint& world)
{
    const wxPoint p = SnapForWiring(world);

    if (!m_wireDrawing)
    {
        m_wireDrawing = true;
        m_wireAnchor  = p;
        m_wireCursor  = p;

        if (OnStatusMessage)
            OnStatusMessage(wxString::Format(
                L"导线起点 (%d, %d) — 再次单击放折点，右键 / Esc / 回车结束", p.x, p.y));

        Refresh();
        return;
    }

    if (p == m_wireAnchor)
    {
        FinishWire();
        return;
    }

    // 一次单击可能产生 1~2 条线段，压一次快照（撤销时整步回退）
    PushUndo(wxString::Format(L"绘制导线到 (%d, %d)", p.x, p.y));

    // 正交折线：先水平后垂直（和预览一致）
    if (p.x != m_wireAnchor.x)
    {
        AddWire(m_wireAnchor, wxPoint(p.x, m_wireAnchor.y));
        m_wireAnchor = wxPoint(p.x, m_wireAnchor.y);
    }

    if (p.y != m_wireAnchor.y)
    {
        AddWire(m_wireAnchor, wxPoint(m_wireAnchor.x, p.y));
        m_wireAnchor = wxPoint(m_wireAnchor.x, p.y);
    }

    m_wireCursor = m_wireAnchor;

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"导线延伸到 (%d, %d)", m_wireAnchor.x, m_wireAnchor.y));

    GetNetGraph();
    UpdateHoverNet(m_wireAnchor);

    if (OnSelectionChanged)
        OnSelectionChanged();

    Refresh();
}

void SchematicCanvas::FinishWire()
{
    if (!m_wireDrawing)
        return;

    m_wireDrawing = false;
    m_netsDirty   = true;

    GetNetGraph();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"导线绘制结束，当前共 %d 条线段，%d 个网络",
                                         static_cast<int>(m_wires.size()),
                                         static_cast<int>(m_graph.nets.size())));

    if (OnSelectionChanged)
        OnSelectionChanged();

    Refresh();
}

void SchematicCanvas::DeleteSelection()
{
    if (!HasSelection())
        return;

    const int componentCount = GetSelectedComponentCount();
    const int wireCount      = GetSelectedWireCount();

    PushUndo(wxString::Format(L"删除 %d 个元件 / %d 条线段", componentCount, wireCount));

    // 从后往前删，避免下标失效
    for (int k = static_cast<int>(m_selComponents.size()) - 1; k >= 0; --k)
        m_components.erase(m_components.begin() + m_selComponents[k]);

    for (int k = static_cast<int>(m_selWires.size()) - 1; k >= 0; --k)
        m_wires.erase(m_wires.begin() + m_selWires[k]);

    ClearSelectionInternal();
    m_netsDirty    = true;
    m_highlightNet = -1;
    Touch();

    GetNetGraph();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已删除 %d 个元件、%d 条线段", componentCount, wireCount));

    NotifySelectionChanged();

    Refresh();
}

void SchematicCanvas::DeselectAll()
{
    if (!HasSelection())
        return;

    ClearSelectionInternal();
    m_highlightNet = -1;

    NotifySelectionChanged();

    Refresh();
}

void SchematicCanvas::ClearAll(bool markModified)
{
    StopSimulation();
    // 清空也算一次可撤销的操作（新建文档时由 MainFrame 传 false 并另行清栈）
    if (markModified && (!m_components.empty() || !m_wires.empty()))
        PushUndo(L"清空画布");

    m_components.clear();
    m_wires.clear();
    m_refCount.clear();
    m_nextComponentId = 0;
    m_nextWireId      = 0;
    m_pending         = nullptr;
    m_wireDrawing     = false;
    m_dragging        = false;
    m_bandSelecting   = false;
    m_highlightNet    = -1;
    m_netsDirty       = true;
    m_zoom            = 1.0;
    m_originInit      = false;
    m_modified        = markModified;

    ClearSelectionInternal();

    EnsureOriginInitialised();
    GetNetGraph();

    NotifySelectionChanged();

    NotifyViewChanged();
}

// ------------------------------------------------------------
//  新建文档：清内容 + 清撤销栈
// ------------------------------------------------------------
void SchematicCanvas::ResetDocument()
{
    m_undoStack.clear();
    m_redoStack.clear();
    ClearAll(false);
}

// ============================================================
//  文档装载（任务 5）
// ============================================================

void SchematicCanvas::SetDocument(const std::vector<ComponentInstance>& components,
                                  const std::vector<Wire>&              wires,
                                  int    gridStep,
                                  bool   gridVisible,
                                  bool   snapEnabled,
                                  double zoom)
{
    StopSimulation();
    m_components  = components;
    m_wires       = wires;
    m_gridStep    = std::max(1, gridStep);
    m_gridVisible = gridVisible;
    m_snapEnabled = snapEnabled;
    m_zoom        = (zoom > 0.05) ? std::min(8.0, zoom) : 1.0;

    // ---- 重建编号计数器与位号计数 ----
    m_nextComponentId = 0;
    m_nextWireId      = 0;
    m_refCount.clear();

    for (const ComponentInstance& comp : m_components)
    {
        m_nextComponentId = std::max(m_nextComponentId, comp.id);

        // 从位号尾部数字恢复计数：U3 → 3
        int split = static_cast<int>(comp.refdes.length());

        while (split > 0 && comp.refdes[split - 1] >= L'0' && comp.refdes[split - 1] <= L'9')
            --split;

        long number = 0;

        if (split < static_cast<int>(comp.refdes.length()) &&
            comp.refdes.Mid(split).ToLong(&number))
        {
            int& counter = m_refCount[comp.name];

            if (static_cast<int>(number) > counter)
                counter = static_cast<int>(number);
        }
    }

    for (const Wire& wire : m_wires)
        m_nextWireId = std::max(m_nextWireId, wire.id);

    // ---- 重置视图与选中状态 ----
    ClearSelectionInternal();
    m_wireDrawing   = false;
    m_dragging      = false;
    m_bandSelecting = false;
    m_pending       = nullptr;
    m_highlightNet  = -1;
    m_netsDirty     = true;
    m_originInit    = false;

    // 换了一份文档，历史记录没有意义了
    m_undoStack.clear();
    m_redoStack.clear();

    EnsureOriginInitialised();
    GetNetGraph();
    CenterOnContent();

    m_modified = false;

    NotifySelectionChanged();

    NotifyViewChanged();
    Refresh();
}

// ============================================================
//  视图操作
// ============================================================

void SchematicCanvas::SetZoomAt(double newZoom, const wxPoint2DDouble& screenAnchor)
{
    newZoom = std::max(0.2, std::min(8.0, newZoom));

    if (std::fabs(newZoom - m_zoom) < 1e-9)
        return;

    const wxPoint2DDouble world = ToWorld(screenAnchor);

    m_zoom = newZoom;

    m_origin.m_x = screenAnchor.m_x - world.m_x * m_zoom;
    m_origin.m_y = screenAnchor.m_y - world.m_y * m_zoom;

    NotifyViewChanged();
    Refresh();
}

void SchematicCanvas::ZoomIn()
{
    const wxSize size = GetClientSize();
    SetZoomAt(m_zoom * 1.25, wxPoint2DDouble(size.x / 2.0, size.y / 2.0));
}

void SchematicCanvas::ZoomOut()
{
    const wxSize size = GetClientSize();
    SetZoomAt(m_zoom / 1.25, wxPoint2DDouble(size.x / 2.0, size.y / 2.0));
}

void SchematicCanvas::ZoomActual()
{
    const wxSize size = GetClientSize();
    SetZoomAt(1.0, wxPoint2DDouble(size.x / 2.0, size.y / 2.0));
}

bool SchematicCanvas::ComputeContentBounds(wxRect& bbox) const
{
    bool hasBounds = false;

    for (const ComponentInstance& comp : m_components)
    {
        const wxRect r = BoxOf(comp);

        if (!hasBounds) { bbox = r;      hasBounds = true; }
        else            { bbox.Union(r); }
    }

    for (const Wire& w : m_wires)
    {
        const wxRect r(std::min(w.a.x, w.b.x), std::min(w.a.y, w.b.y),
                       std::abs(w.b.x - w.a.x) + 1, std::abs(w.b.y - w.a.y) + 1);

        if (!hasBounds) { bbox = r;      hasBounds = true; }
        else            { bbox.Union(r); }
    }

    return hasBounds;
}

void SchematicCanvas::CenterOnContent()
{
    const wxSize size = GetClientSize();

    wxRect bbox;

    if (!ComputeContentBounds(bbox))
    {
        m_origin = wxPoint2DDouble(size.x / 2.0, size.y / 2.0);
        return;
    }

    m_origin.m_x = size.x / 2.0 - (bbox.x + bbox.width / 2.0) * m_zoom;
    m_origin.m_y = size.y / 2.0 - (bbox.y + bbox.height / 2.0) * m_zoom;
}

void SchematicCanvas::ZoomFit()
{
    EnsureOriginInitialised();

    const wxSize size = GetClientSize();

    wxRect bbox;

    if (!ComputeContentBounds(bbox))
    {
        m_zoom   = 1.0;
        m_origin = wxPoint2DDouble(size.x / 2.0, size.y / 2.0);

        NotifyViewChanged();
        Refresh();
        return;
    }

    bbox.Inflate(kPinLen * 3, kPinLen * 3);

    const double zx = static_cast<double>(size.x) / std::max(1, bbox.width);
    const double zy = static_cast<double>(size.y) / std::max(1, bbox.height);

    m_zoom = std::max(0.2, std::min(4.0, std::min(zx, zy)));

    m_origin.m_x = size.x / 2.0 - (bbox.x + bbox.width / 2.0) * m_zoom;
    m_origin.m_y = size.y / 2.0 - (bbox.y + bbox.height / 2.0) * m_zoom;

    NotifyViewChanged();
    Refresh();
}

void SchematicCanvas::SetPendingComponent(const ComponentDef* def)
{
    m_pending = def;

    if (def && m_tool != ToolSelect)
        m_tool = ToolSelect;

    Refresh();
}

void SchematicCanvas::SetTool(Tool tool)
{
    m_bundleWidth=1;
    if (m_tool == ToolWire && tool != ToolWire)
        FinishWire();

    m_tool = tool;

    if (tool != ToolSelect)
        m_pending = nullptr;

    Refresh();
}

void SchematicCanvas::SetGridVisible(bool on)
{
    if (m_gridVisible == on)
        return;

    m_gridVisible = on;
    Touch();
    NotifyViewChanged();
    Refresh();
}

void SchematicCanvas::SetSnapEnabled(bool on)
{
    if (m_snapEnabled == on)
        return;

    m_snapEnabled = on;
    Touch();
    NotifyViewChanged();
}

void SchematicCanvas::SetCrosshairVisible(bool on)
{
    if (m_crosshair == on)
        return;

    m_crosshair = on;
    NotifyViewChanged();
    Refresh();
}

void SchematicCanvas::NotifyViewChanged()
{
    if (OnViewChanged)
        OnViewChanged();
}

} // namespace se

namespace se {
void SchematicCanvas::CopySelection() {
    m_copiedComponents.clear(); m_copiedWires.clear(); m_pasteCount=0;
    for(int i:m_selComponents) m_copiedComponents.push_back(m_components[i]);
    for(int i:m_selWires) m_copiedWires.push_back(m_wires[i]);
    if(OnStatusMessage) OnStatusMessage(wxString::Format(L"已复制 %d 个元件、%d 条线段（应用内剪贴板）",(int)m_copiedComponents.size(),(int)m_copiedWires.size()));
}
void SchematicCanvas::CutSelection() { if(HasSelection()) { CopySelection(); DeleteSelection(); } }
void SchematicCanvas::PasteSelection() {
    if(m_copiedComponents.empty() && m_copiedWires.empty()) return;
    FinishWire(); PushUndo(L"粘贴");
    const int nc=(int)m_components.size(), nw=(int)m_wires.size();
    const int delta=2*m_gridStep*(++m_pasteCount);
    AppendCopies(m_components,m_wires,m_copiedComponents,m_copiedWires,wxPoint(delta,delta));
    ClearSelectionInternal();
    for(int i=nc;i<(int)m_components.size();++i) m_selComponents.push_back(i);
    for(int i=nw;i<(int)m_wires.size();++i) m_selWires.push_back(i);
    for(const auto& c:m_components) m_nextComponentId=std::max(m_nextComponentId,c.id);
    for(const auto& w:m_wires) m_nextWireId=std::max(m_nextWireId,w.id);
    m_pending=nullptr; m_tool=ToolSelect; m_netsDirty=true; Touch(); NotifySelectionChanged(); Refresh();
}
void SchematicCanvas::Annotate() {
    PushUndo(L"自动编号"); AnnotateComponents(m_components); m_netsDirty=true; Touch(); NotifySelectionChanged(); Refresh();
}
bool SchematicCanvas::SetComponentProperty(const wxString& key,const wxString& value,wxString& error) {
    int index=GetSelectedComponentIndex();
    if(index<0) { error=L"请只选中一个元件";return false; }
    auto candidate=m_components[index];
    if(key=="refdes") {
        if(value.empty()){error=L"位号不能为空";return false;}
        for(size_t i=0;i<m_components.size();++i)if((int)i!=index && m_components[i].refdes==value){error=L"位号已存在";return false;}
        candidate.refdes=value;
    }else if(key=="value") {
        if(DigitalModel(candidate)=="input"&&value!="0"&&value!="1"&&value!="X"){error=L"输入初值只能为0、1或X";return false;}
        candidate.value=value;
    }else if(key=="footprint")candidate.footprint=value;
    else if(key=="delay"||key=="clockPeriod") {
        long n=0;if(!value.ToLong(&n)||n<1||n>(key=="delay"?1000:10000)||(key=="clockPeriod"&&(n<2||n%2))){error=L"延迟为1..1000；时钟周期为2..10000的偶数";return false;}
        if(key=="delay")candidate.delayTicks=(int)n;else candidate.clockPeriodTicks=(int)n;
    }else if(key=="truth"){
        candidate.truthTable=value;DigitalSimulator probe;
        if(!probe.Reset({candidate},NetBuilder::Build({candidate},{}),error))return false;
    }else if(key.StartsWith("pin.")){
        long k=0;if(!key.Mid(4).ToLong(&k)||k<0||k>=(long)candidate.pins.size()||value.empty()){error=L"非法引脚编号";return false;}
        for(size_t i=0;i<candidate.pins.size();++i)if((long)i!=k&&PcbPinNumber(candidate,i)==value){error=L"引脚编号重复";return false;}
        candidate.pins[k].number=value;
    }else {error=L"不支持的属性";return false;}
    PushUndo(L"修改元件属性");m_components[index]=candidate;m_netsDirty=true;Touch();Refresh();
    return true;
}
void SchematicCanvas::SetGridStep(int value) {
    m_gridStep=std::max(1,std::min(100,value));Touch();NotifyViewChanged();Refresh();
}
bool SchematicCanvas::ExportPng(const wxString& path) {
    const auto size=GetClientSize(); if(size.x<=0 || size.y<=0) return false;
    wxBitmap bitmap(size.x,size.y); wxMemoryDC dc(bitmap);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if(!gc) return false;
    GetNetGraph(); DrawBackground(gc.get());DrawGrid(gc.get());DrawWires(gc.get());DrawComponents(gc.get());
    gc.reset(); dc.SelectObject(wxNullBitmap);
    return bitmap.ConvertToImage().SaveFile(path,wxBITMAP_TYPE_PNG);
}
}
namespace se {
void SchematicCanvas::SetWireBundle(int width) {SetTool(ToolWire);m_bundleWidth=std::max(1,std::min(32,width));}
}

namespace se {
bool SchematicCanvas::StartSimulation(wxString& error){
    if(m_components.empty()){error=L"请先绘制电路";return false;}
    if(!m_simActive){
        FinishWire();m_pending=nullptr;m_tool=ToolSelect;
        if(!m_simulator.Reset(m_components,GetNetGraph(),error))return false;
        m_simActive=true;
    }
    m_simTimer.Start(100);SimulationChanged();return true;
}
bool SchematicCanvas::StepSimulation(wxString& error){
    if(!m_simActive && !StartSimulation(error))return false;
    m_simTimer.Stop();
    if(!m_simulator.Advance()){error=m_simulator.Error();StopSimulation();return false;}
    SimulationChanged();return true;
}
void SchematicCanvas::StopSimulation(){m_simTimer.Stop();if(m_simActive){m_simActive=false;Refresh();}}
void SchematicCanvas::OnSimTimer(wxTimerEvent&){
    if(!m_simActive)return;
    if(!m_simulator.Advance()){const auto error=m_simulator.Error();StopSimulation();if(OnStatusMessage)OnStatusMessage(error);return;}
    SimulationChanged();
}
void SchematicCanvas::SimulationChanged(){
    Refresh();if(OnStatusMessage)OnStatusMessage(wxString::Format(L"仿真 t=%llu步；转换=%llu；空格/双击输入切换，F6单步，Shift+F5停止。编辑电路会停止并清除运行状态。",(unsigned long long)m_simulator.Time(),(unsigned long long)m_simulator.TransitionCount()));
}
bool SchematicCanvas::ToggleSimulationInput(){
    if(!m_simActive)return false;auto* c=GetSelectedComponent();
    if(!c||DigitalModel(*c)!="input")return false;
    if(!m_simulator.SetInput(c->id,m_simulator.InputValue(c->id)==1?0:1))return false;
    SimulationChanged();return true;
}
}
