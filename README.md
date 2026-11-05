# PocketLoom

轻量 2D 掌机游戏引擎：Windows 桌面编辑器 + PC 试玩；PSP / 3DS 目标在规划中。
“Pocket” = 掌上，“Loom” = 把图块和规则织成游戏。
GBA 目标已移除（见 docs/ 的历史文档）。

## 功能

- 场景 / 图块 / 动画 / 对象 / 资源 / 流程编辑器
- 对象规则：条件 + 动作（移动、动画、翻转、重力、音效、生成、销毁、属性等）
- 区域触发器：进入 / 离开 / 停留；区域 + 规则 + 动作
- 界面：常驻层 / 关卡层 / 弹窗层；平铺九宫格；数值显示；菜单
- 场景：4 个通用图块层 + 对象层；视差；相机跟随 / 固定
- 运行时无固定玩法模式：没有射击/胜负/分数/Game Over；平台物理只是通用模拟
- PC 试玩：直接读取 p3d，不需要模拟器
- 存档：全局变量 + 当前场景 + 玩家位置

## 目录

~~~text
editor/        Win32 + D3D11 + ImGui 编辑器
engine/        base / platform(windows) / render
pgmmv/         工程、对象、场景、图块、动画数据层 + p3d 导出
runtime/       p3d 数据格式、规则运行时、PC 试玩
tools/         构建与字体图集工具
third_party/   ImGui、miniaudio、内置字体源（带许可证）
docs/          设计与使用文档
~~~

## 构建

需要：CMake 3.20+、Ninja、MinGW-w64 g++（Windows）。

编辑器与 PC 试玩：

~~~powershell
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --parallel
~~~

或：

~~~powershell
.\tools\build-editor.ps1
~~~

产物：

~~~text
build/host/bin/PocketLoom-Editor.exe
build/host/bin/PocketLoom-Play.exe
build/host/bin/ploom.exe
~~~

## 字体

third_party/font 中的字体只用于生成游戏内位图字形，不用于编辑器 GUI。
编辑器 GUI 仍使用系统字体。详见 third_party/font/README.md。

## 文档

- [场景画布方案](docs/08-场景画布方案.md)
- [触发器与规则](docs/12-触发器与规则.md)
- [界面分层](docs/13-界面分层.md)
- [场景图层](docs/14-场景图层.md)
- [键位映射](docs/15-键位映射.md)
- [界面元素](docs/16-界面元素.md)
- [存档](docs/17-存档.md)
- [PC 窗口与调试](docs/18-PC窗口与调试.md)

GBA 时期的历史文档（已不再维护）：docs/04、docs/06、docs/10。

## 许可证

项目代码使用 MIT License，见 LICENSE。
第三方代码和字体不适用 MIT，保留各自许可证：

- THIRD_PARTY_NOTICES.md
- third_party/NOTICE.md
- third_party/font/README.md
