// ============================================================
//  minimal.cpp  —— 程序入口
//
//  由原来的“单文件骨架”拆分而来：
//    菜单 / 工具栏 / 三分区布局  → MainFrame.h / MainFrame.cpp
//    元件库数据模型              → ComponentCatalog.h / .cpp
//    元件库面板                  → LibraryPanel.h / .cpp
//    绘图区                      → SchematicCanvas.h / .cpp
//    属性表面板                  → PropertyPanel.h / .cpp
//    命令 ID                     → Ids.h
//
//  本文件只保留 wxApp 与主窗口的创建。
//  原文件（含全部菜单定义）已完整迁移到 MainFrame.cpp 的 CreateMenus()。
// ============================================================

#include <wx/wx.h>
#include <wx/image.h>
#include <wx/cmdline.h>

#include "MainFrame.h"

// ------------------------------------------------------------
// 应用程序
// ------------------------------------------------------------
class SchematicApp : public wxApp
{
public:
    wxString initialPath;
    void OnInitCmdLine(wxCmdLineParser& parser) override {
        wxApp::OnInitCmdLine(parser);
        parser.AddParam("design.ses",wxCMD_LINE_VAL_STRING,wxCMD_LINE_PARAM_OPTIONAL);
    }
    bool OnCmdLineParsed(wxCmdLineParser& parser) override {
        if(!wxApp::OnCmdLineParsed(parser))return false;
        if(parser.GetParamCount())initialPath=parser.GetParam(0);
        return true;
    }
    bool OnInit() override
    {
        if (!wxApp::OnInit())
            return false;

        // 位图 / PNG 图像处理器（wxArtProvider 的图标需要）
        wxInitAllImageHandlers();

        se::MainFrame* frame = new se::MainFrame();

        frame->Show(true);
        if(!initialPath.empty())frame->OpenDocument(initialPath);

        return true;
    }
};

wxIMPLEMENT_APP(SchematicApp);
