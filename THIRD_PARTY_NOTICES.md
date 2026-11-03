# 来源与第三方说明

1. **用户提供的 SchematicEditor 源码**：最初的导入提交用于记录收到的代码，不代表追溯恢复原作者Git历史。提供的原项目没有发现许可证声明，没有替原作者声明新的开源许可证。公开仓库准备工作由项目提供者明确授权；公开可见不等同于授予未声明的再许可权利。
2. **wxWidgets 3.2**：程序动态链接本机 wxGTK / wxBase / propgrid / XML 库，使用wxWidgets提供的图标和控件。许可证为wxWindows Library Licence（带例外的LGPL体系）；保留本机发行版版权/许可证文本于 `licenses/wxWidgets-copyright.txt`。官方：https://www.wxwidgets.org/about/licence/ 。本轮未修改或打包wxWidgets库源码。
3. **KiCad 网表格式参考与外部测试工具**：独立实现S表达式导出，参考KiCad原生读取器的字段行为；没有复制或编入KiCad源代码。官方读取器：https://github.com/KiCad/kicad-source-mirror/blob/9.0/pcbnew/netlist_reader/kicad_netlist_reader.cpp 。GUI导入测试的界面依据：https://github.com/KiCad/kicad-source-mirror/blob/8.0/pcbnew/dialogs/dialog_import_netlist.cpp 。CI配置安装发行版提供的KiCad与封装库，通过其真实GUI导入并检查保存的PCB。是否实际运行、是否通过必须以具体Actions结果为准。KiCad本身的许可证不应误写成本项目已采用的源码许可证。
4. 构建使用系统 GCC、CMake，测试使用Python标准库；没有安装或采用从GitHub下载的第三方编辑器。方案沿用现有C++/wxWidgets工程，依据是实际编译和模型测试可复现，且变动可以限定在新仿真模型、网表后端和编辑修复。没有用其他项目的README或截图作为本项目成熟度证据。
5. CI 的外部GUI测试另外使用发行版提供的Xvfb、Openbox、xdotool、scrot和Python AT-SPI绑定；这些工具没有复制或编入应用程序。工作流使用GitHub官方的checkout与upload-artifact Actions，固定到官方v7提交；其源码在各官方仓库按各自许可证提供。
