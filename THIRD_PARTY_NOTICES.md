# 来源与第三方说明

1. **用户提供的 SchematicEditor 源码**：来自本机下载目录及已有审计副本。最初的导入提交用于记录收到的代码，不代表追溯恢复原作者Git历史。提供的原项目没有发现许可证声明，本轮没有替原作者声明新的开源许可证，也没有将其发布到外部服务。
2. **wxWidgets 3.2**：程序动态链接本机 wxGTK / wxBase / propgrid / XML 库，使用wxWidgets提供的图标和控件。许可证为wxWindows Library Licence（带例外的LGPL体系）；保留本机发行版版权/许可证文本于 `licenses/wxWidgets-copyright.txt`。官方：https://www.wxwidgets.org/about/licence/ 。本轮未修改或打包wxWidgets库源码。
3. **KiCad 网表格式参考**：独立实现S表达式导出，参考KiCad 9.0原生读取器的字段行为；没有复制或编入KiCad源代码，也没有把KiCad当作已运行的依赖。官方源文件：https://github.com/KiCad/kicad-source-mirror/blob/9.0/pcbnew/netlist_reader/kicad_netlist_reader.cpp 。KiCad本身的许可证不应误写成本项目已采用的源码许可证。实际目标软件导入仍未验证。
4. 构建使用系统 GCC、CMake，测试使用Python标准库；没有安装或采用从GitHub下载的第三方编辑器。方案沿用现有C++/wxWidgets工程，依据是实际编译和模型测试可复现，且变动可以限定在新仿真模型、网表后端和编辑修复。没有用其他项目的README或截图作为本项目成熟度证据。
