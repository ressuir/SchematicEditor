# SchematicEditor 2.0

C++17 / wxWidgets 3.2 电路原理图编辑器。本目录只包含软件、示例、构建说明与测试；不包含课程报告或演示文稿。

**当前验收状态：未完成全部验收。** 动态仿真、持久化、编辑模型和 CLI 集成已有真实自动化结果。当前执行沙箱禁止 socket，本机也没有可用桌面连接，因此新版 GUI 启动、鼠标键盘端到端操作、实际截图以及目标 PCB 软件导入尚未验证。不能把本目录称为已通过全部任务书验收。随包 `verification/STATUS.json` 和原始日志记录最终版本的实际结果。

## 构建与运行

Linux 需要 C++17 编译器、CMake 3.16 以上，以及 wxWidgets 3.2 的 core/base/propgrid/xml 开发库。Ubuntu 通常由 `build-essential cmake libwxgtk3.2-dev` 提供。Python3 用于 CLI 集成检查，不参与程序运行。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/SchematicEditor examples/dynamic-and.ses
```

交付包中 `bin/linux/` 是本机 Linux x86_64 构建。通过 `./run-editor.sh` 启动，或直接传入设计文件：

```bash
./run-editor.sh examples/clock-and.ses
./bin/linux/schematic-cli --version
```

这些程序动态链接本机系统共享库，不是跨发行版静态包。Windows 的 VS 工程已列入新增源文件，但没有 Windows 构建或运行结论；包中没有冒充新版的旧 exe。

## 编辑和文件

从元件库选择元件，在绘图区单击放置。Esc 结束放置；选择工具下拖动元件。W 进入正交导线工具，单击起点和折点，Enter/Esc/右键结束。滚轮缩放，中键平移，Home 适合窗口。Ctrl/Shift 单击加选，空白处拖动框选。Delete 删除；Ctrl+C/X/V 为本应用内复制/剪切/粘贴；Ctrl+Z/Y 撤销/重做。

右侧属性可修改位号、参数、封装、PCB 引脚编号、传播延迟，以及时钟周期或自定义真值表。F5 仿真运行期间双击输入或按空格改变的是运行值；在属性面板编辑的是工程初值。编辑几何、初值、模型或撤销重做会停止仿真并清除运行状态，防止图纸和引擎使用不同版本的数据。

Ctrl+S 保存、Ctrl+O 打开；导入菜单与打开使用同一种 `.ses` 设计格式。设计文件采用 UTF-8 XML，应用文件格式版本为1.1（XML声明仍为1.0），包含元件完整引脚、数字模型、真值表、延迟、时钟周期、PCB 脚号、封装及导线；仍可读取原 1.0 文件。格式错误和非法字段会在替换当前文档前拒绝。先写同目录临时文件，再替换保存目标。

## 动态数字仿真

F5 运行/继续，F6 暂停并前进一个仿真步，Shift+F5 停止。每步100ms；每个门默认传播延迟1步，可设1—1000步。时钟周期为2—10000的偶数步。画布显示当前仿真时间、引脚值和导线颜色：0蓝、1绿、X灰、驱动冲突ERR红。

输入和时钟改变驱动后，引擎在后续时间点执行门输出事件；不是每次按钮点击后立即计算最终真值。引擎使用离散时间、可取消事件和惯性延迟；短于门延迟的脉冲会被取消。多级门会逐级传播。无驱动/未确定为X，相反驱动为ERR。反馈保持X不代表电路正确。

支持与、或、非、与非、或非、异或、同或、半加器、全加器、输入、常量、时钟、VCC/GND，以及下述自定义数字模型。旧库中的电阻、晶体管、运放只是符号；启动数字仿真时会明确拒绝没有数字模型的器件。没有模拟电路求解，也没有新增工业级布线功能。

无需 GUI 的真实运行入口：

```bash
./bin/linux/schematic-cli simulate examples/dynamic-and.ses 10 2:1:1 4:2:1 7:1:0
./bin/linux/schematic-cli simulate examples/clock-and.ses 10
```

事件参数为 `时间步:元件ID:0或1或X`。前一个示例在第2步改变ID1、第4步改变ID2、第7步复位ID1；输出在第5和8步变化。标准输出是实际引擎逐步执行的CSV轨迹。

## 自定义元件

首选项中导入 `examples/sample-library.xml`。导入后进入“自定义”分类。元件支持逻辑引脚、矩形大小、说明、数字模型和延迟；不包含复杂符号绘制器。

```xml
<library>
  <component name="CUSTOM_XOR" prefix="U" inputs="2" outputs="1"
             model="truth" truth="0110" delay="1" w="80" h="50"/>
</library>
```

`model` 可为 and/nand/or/nor/not/buffer/xor/xnor/half-adder/full-adder/truth。truth 的输入最多8个、输出最多16个；只含0/1，长度为 `2^输入数 × 输出数`。按输入A为最低位、B为次低位的行号排列，每行依次列所有输出。两输入异或为0110，三输入与为00000001。not/buffer必须一入一出；半加器二入二出，全加器三入二出。非法库整批拒绝，不会导入前半部分。

库条目只在当前应用会话注册；已放入图纸的实例会保存模型和引脚，因此重新打开图纸后无需原库也可仿真。新会话若要继续放置该类型，再导入库文件。无model的旧式结构库仍可导入，但不能把没有数字模型的自定义符号当作可仿真器件。

## KiCad 网表导出

文件→导出→KiCad标准网表写出 KiCad 原生 S 表达式 `.net`，含组件、封装标识、稳定UUID、网络和物理引脚号。内部“查看网络表”仍是诊断文本，不是PCB导入文件。`examples/legacy-*-diagnostic.txt` 仅保留历史诊断样例。

```bash
./bin/linux/schematic-cli export-kicad examples/dynamic-and.ses output.net
```

所有PCB元件须有 `Library:Footprint` 封装。新放置元件默认按1—40个逻辑端子分配标准2.54mm单排连接器封装，脚号默认按引脚顺序1、2、3……；可以在属性面板修改。该默认映射代表外接逻辑模块的端子，不是假称为7400等真实多门IC的电源/封装引脚。若使用实际IC，必须按实物修改封装和PCB脚号。旧设计没有封装时，导出会指出缺失元件；不会静默导出不完整网表。

GND/VCC、标签、连接点和文本不作为物理元件，电源/标签的连接语义仍进入网络。重复位号、重复脚号或缺失封装会使导出失败。独立解析测试已核对示例的全部元件、脚号和连接关系；**尚未使用真实 KiCad PCB Editor 导入，不能宣称PCB兼容验收通过。** 目标软件为KiCad 9.x原生网表读取器，格式依据见第三方说明。实际导入后还必须检查封装库解析和逐引脚连通性。

## 验证与版本

- `model_tests`：原有模型回归，输出217次断言；包含夹具查找，不是217个独立功能。
- `dynamic_tests`：真实时间推进、时钟、延迟、输入变化、冲突、自定义模型和持久化。
- `editing_tests`：移动T节点、无点交叉、正交线及ID一致性；不是GUI操作测试。
- `cli_integration`：执行交付CLI并独立检查动态CSV、文件往返和网表语法/映射；不是PCB软件导入测试。
- `gui_widget_tests`：在可用显示服务下创建真实wxWidgets窗口，验证库回调、画布事件、属性、撤销/重做、保存重开和仿真接入；显示服务不可用时以77返回并标为Skipped。该测试即使通过也不能替代原生文件对话框、真实键鼠和目标PCB软件的端到端验收。

原始日志、JUnit、运行版本与文件哈希位于交付包 `verification/`。没有运行截图文件时表示未取得新版真实截图，不会生成示意图冒充截图。所有本轮Git提交使用Codex Development署名，不冒充团队成员或补造过去的协作历史。源代码基于用户提供的项目，没有引入第三方仿真器/编辑器源码；依赖和格式参考见 `THIRD_PARTY_NOTICES.md`。
