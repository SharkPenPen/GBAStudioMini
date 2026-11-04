# GBAStudioMini

GBA 版 PGMMV Lite：Windows 桌面编辑器 + GBA 导出 + PC 试玩。

> 本项目只维护 GBA 方向。3DS 平台层、构建和运行入口已经移除，不再作为并行版本维护。

## 功能

- 场景 / 图块 / 动画 / 对象 / 资源 / 流程编辑器
- 对象规则：条件 + 动作（播动画、音效、移动、属性、开关等）
- 场景触发器：指定对象进入后触发动作，可触发胜利
- 实体碰撞：图块碰撞层、可推动对象、动画帧碰撞盒
- GBA 构建：4 场景表、Mode 0 背景、HUD、淡入淡出、Game Over
- PC 试玩：直接读取 p3d，不需要 GBA 模拟器

## 目录

~~~text
editor/        Win32 + D3D11 + ImGui 编辑器
engine/        base / platform(windows) / render
pgmmv/         工程、对象、场景、图块、动画数据层 + p3d / GBA 导出
runtime/       p3d 数据格式、规则运行时、PC 试玩
gba/           GBA 目标：crt0 / linker / main.cpp / 生成数据
tools/         构建与字体图集工具
third_party/   ImGui、miniaudio、内置字体源（带许可证）
docs/          设计与使用文档
~~~

`build/`、`toolchain/`、`projects/` 和日志不进入版本库。

## 构建

需要：

- CMake 3.20+
- Ninja
- MinGW-w64 g++（Windows）
- GBA 构建额外需要 devkitARM

编辑器与 PC 试玩：

~~~powershell
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --parallel
~~~

产物：

~~~text
build/host/bin/GBAStudioMini-Editor.exe
build/host/bin/GBAStudioMini-Play.exe
build/host/bin/gbaedit.exe
~~~

GBA：

~~~powershell
.\tools\build-gba.ps1
~~~

如果 devkitARM 不在 `toolchain/devkitARM`，先设置：

~~~powershell
$env:DEVKITARM = "C:\devkitPro\devkitARM"
~~~

## 字体

`third_party/font` 中的字体只用于生成**游戏内位图字形**，不用于编辑器 GUI。
编辑器 GUI 仍使用系统字体。字体许可证、来源和 SHA256 都保存在对应目录，授权声明见
[third_party/font/README.md](third_party/font/README.md)。

当前只内置原生 12 / 14 / 16px，不做字号缩放。18/20px 没有合适的低风险简体中文原生字体，暂不加入。

## 文档

- [GBA 图层设计](docs/10-GBA图层设计.md)
- [场景画布方案](docs/08-场景画布方案.md)
- [显示层与模式](docs/06-GBA显示层与模式.md)
- [简化版编辑器](docs/04-简化版编辑器.md)

## 许可证

项目代码使用 MIT License，见 [LICENSE](LICENSE)。

第三方代码和字体不适用 MIT，保留各自许可证：

- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
- [third_party/NOTICE.md](third_party/NOTICE.md)
- [third_party/font/README.md](third_party/font/README.md)
