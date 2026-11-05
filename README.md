# PocketLoom

轻量 2D 掌机游戏引擎：Windows 桌面编辑器 + PC 试玩；PSP / 3DS 目标在规划中。
“Pocket” = 掌上，“Loom” = 把图块和规则织成游戏。

## 状态

- PC 编辑器、PC 播放器可用。
- 逻辑分辨率固定 **480×272**（PSP 全幅），1:1 像素，整数倍窗口。
- PSP / 3DS 后端尚未接入；接口先保留。
- 运行时没有固定玩法模式：没有射击、自动卷轴、胜负、分数、Game Over。

## 功能

- 六个工具页：场景 / 图块 / 动画 / 对象 / 资源 / 界面。
- 场景：4 个通用图块层 + 对象层；视差；相机跟随 / 固定。
- 区域 + 规则：进入 / 离开 / 在里面；条件 + 动作；动作可等界面关闭。
- 对象规则：条件 + 动作（移动、动画、翻转、重力、音效、生成、属性等）。
- 界面：常驻 / 关卡 / 弹窗三层；平铺九宫格；12/14/16 原生中文点阵；按钮、列表、淡入淡出。
- 存档：全局变量 + 当前场景 + 玩家位置；标题界面可“继续 / 新游戏”。
- p3d 直接导出，PC 播放器不需要模拟器。

## 快速开始

构建：

~~~powershell
.\tools\build-editor.ps1
~~~

产物：

~~~text
build/host/bin/PocketLoom-Editor.exe
build/host/bin/PocketLoom-Play.exe
build/host/bin/ploom.exe
~~~

运行：

- 打开编辑器，新建/打开工程；
- F5 导出 p3d 并启动播放器；
- 或直接：

~~~powershell
.\build\host\bin\PocketLoom-Play.exe --p3d path\to\game.p3d
~~~

## 操作

编辑器：

- 鼠标 / 键盘；F5 试玩。
- 界面页：拖元素、右下角缩放、方向键调属性。

播放器（默认键位，可在工程设置里改）：

- 方向键：移动
- Z：A / 确认
- X：B / 取消
- A：X
- S：Y
- Q / W：L / R
- Space：Start
- V / Enter：Select

## 测试

~~~powershell
.\build\host\bin\capabilities.exe
~~~

覆盖移动、碰撞、区域规则、模态/列表、存档、标题、相机、p3d 往返、UI 布局/九宫格/字体/数值条。

## 打包

~~~powershell
.\tools\package.ps1
~~~

生成：

~~~text
dist/PocketLoom-0.1.0-win64/
dist/PocketLoom-0.1.0-win64.zip
dist/PocketLoom-0.1.0-win64.zip.sha256
~~~

包内含编辑器、播放器、文档、空白模板、许可证。

## 项目布局

~~~text
editor/        Win32 + D3D11 + ImGui 编辑器
engine/        base / platform(windows) / render
pgmmv/         工程、对象、场景、图块、动画数据层 + p3d 导出
runtime/       p3d 数据格式、规则运行时、PC 播放器
tests/         C++ 能力测试
tools/         构建、字体、打包脚本
templates/     空白工程模板
third_party/   ImGui、miniaudio、字体源与许可证
docs/          设计与使用文档
~~~

## 文档

- [场景画布方案](docs/08-场景画布方案.md)
- [触发器与规则](docs/12-触发器与规则.md)
- [界面分层](docs/13-界面分层.md)
- [场景图层](docs/14-场景图层.md)
- [键位映射](docs/15-键位映射.md)
- [界面元素](docs/16-界面元素.md)
- [存档](docs/17-存档.md)
- [PC 窗口与调试](docs/18-PC窗口与调试.md)
- [发布与打包](docs/19-发布与打包.md)

GBA 时期历史文档（不再维护）：docs/04、docs/06、docs/10。

## 许可证

项目代码 MIT；第三方代码和字体保留各自许可证：

- THIRD_PARTY_NOTICES.md
- third_party/NOTICE.md
- third_party/font/README.md
