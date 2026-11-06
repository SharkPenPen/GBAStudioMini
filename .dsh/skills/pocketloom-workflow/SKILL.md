---
name: pocketloom-workflow
description: PocketLoom 开发工作流。修改 engine/runtime/pgmmv/editor、改 p3d 或存档格式、做 demo、接入 3DS/PSP 后端时使用；先定变更轨道，再按构建、能力测试、demo 导出、打包的顺序验证，避免旧玩法回潮和格式不同步。
whenToUse: 任务涉及 D:\GitHud\GameStudio\GBAStudioMini，或提到 PocketLoom、p3d、3DSStudioMini、PSP 移植、编辑器、demo、打包时。
---

# PocketLoom 工作流

## 0. 项目事实

- 主工程：`D:\GitHud\GameStudio\GBAStudioMini`
- 3DS 参考工程：`D:\GitHud\GameStudio\3DSStudioMini-PGMMV`
- 编辑器、运行时、导出、demo、打包都在主工程。
- 3DS 参考工程是旧版本，只能参考平台层，不要整包合并。
- 当前数据版本：
  - p3d **v91**
  - 存档 **PLSV v2**
- 当前设计基线：
  - 运行时没有固定玩法：没有玩家属性、HP、分数、伤害公式、Shooter、FlowLink。
  - 所有状态只走变量表。
  - 对象没有属性表。
  - 8×8 只是编辑器最小绘制单位，不是地图底层。
  - 图块尺寸读 tileset 的 `tile_w/tile_h`。
  - 碰撞层是独立的 8px 网格。
  - 前景层会盖住对象，地砖默认放背景层。
  - UI 字体 12/14/16；12px 不能删。
- 编辑器只跑 PC，不把编辑器带进 3DS/PSP。

## 1. 开工前

1. 确认当前路径和 git 根。
2. 读最近提交和相关 docs，不要凭记忆改。
3. 给任务标轨道：

```text
[core]     runtime / p3d / game / engine base
[editor]   PC 编辑器
[data]     改 p3d、save、JSON 格式
[3ds]      只改 3DS 平台层/后端
[psp]      只改 PSP 平台层/后端
```

一次只做一条轨道。跨轨道时拆成多个可验证的小步。

## 2. 任务合同

改代码前先写清楚：

```text
目标：
涉及文件：
是否改数据格式：
p3d/save 是否升版本：
验证方式：
```

没有验证方式的任务不开始。

## 3. 实现规则

- 先 grep 引用，再删/改。
- 大删除前先提交或备份；删完立刻构建。
- 不把表现层概念写回基础层。
- 不在 runtime 里加平台头（`<3ds.h>`、PSP SDK、`windows.h`）。
- 平台差异只放：
  - `engine/platform/<platform>`
  - `engine/render/backend_<platform>`
- 旧工程不兼容是默认策略，不做无谓迁移。

## 4. 验证顺序

在主工程执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-editor.ps1
build\host\bin\capabilities.exe
```

导出三个 demo：

```powershell
build\host\bin\ploom.exe --open samples\bee\project.json --export-p3d samples\bee\bee.p3d
build\host\bin\ploom.exe --open samples\platformer\project.json --export-p3d samples\platformer\game.p3d
build\host\bin\ploom.exe --open samples\topdown\project.json --export-p3d samples\topdown\game.p3d
```

启动 smoke：

```powershell
build\host\bin\PocketLoom-Play.exe --p3d samples\bee\bee.p3d --frames 2
build\host\bin\PocketLoom-Play.exe --p3d samples\platformer\game.p3d --frames 2
build\host\bin\PocketLoom-Play.exe --p3d samples\topdown\game.p3d --frames 2
```

需要发布包时：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package.ps1
```

## 5. 平台闸门

| 改动 | PC | 3DS | PSP |
|---|---|---|---|
| [core] | build + capabilities + 3 demo | 编译；运行路径变了就实跑 3dsx | 编译；运行路径变了就实跑 EBOOT |
| [editor] | build + editor smoke | 不影响 | 不影响 |
| [data] | 重导 3 demo | 读同一 p3d | 读同一 p3d |
| [3ds] | 不回归即可 | 3dsx 实跑 | 只要求编译不坏 |
| [psp] | 不回归即可 | 只要求编译不坏 | EBOOT 实跑 |

没有工具链就明确写“未验证”，不要写“应该可以”。

## 6. 数据格式规则

改以下内容必须升 p3d 或 save 版本：

- p3d 二进制字段、枚举顺序、对象/场景结构；
- save 字段或编码；
- 变量作用域/持久化语义。

改完必须：

1. 升常量；
2. 重新导出三个 demo；
3. 用播放器实际读；
4. 更新 docs；
5. 在提交信息里写版本变化。

旧 p3d/旧存档不兼容是允许的，不做兼容层。

## 7. 3DS / PSP 接入顺序

不要直接把旧 Game 搬过去。

### 阶段 0：平台契约

先统一：

- 文件读取；
- 存档路径；
- 纹理上传；
- 输入映射；
- 音频；
- 时间/日志。

runtime 不直接依赖 `std::filesystem` 和平台 API。

### 阶段 1：平台 hello

- 平台层 + 渲染后端 + 工具链；
- 清屏/输入/退出；
- 不接 Game。

### 阶段 2：当前 runtime

- 读当前 p3d；
- 调 `Game::Init/Update/Render`；
- 只画背景。

### 阶段 3：demo

- 当前 demo 重新导出；
- 放进 romfs / ISO；
- 实跑 3DSX / EBOOT。

### 阶段 4：输入/音频/存档/UI

每个阶段都要有实跑证据。

## 8. 3DS 复用清单

可复用：

- `engine/platform/_3ds/`
- `engine/render/backend_citro.cpp`
- `runtime/audio_3ds.cpp`
- `cmake/toolchain-3ds.cmake`
- `cmake/3dsx.cmake`
- `cmake/cia/`
- 本机 devkitPro

不可直接复用：

- 旧 `runtime/game.cpp`（p3d v67 + 固定玩法）
- 旧 `runtime/p3d.*`
- 旧 `pgmmv/`、`editor/`、`demo/`
- 旧字体 `font*`

3DS 阶段目标：

- 3DS 显示 400×240 安全区；
- 当前 p3d v91；
- 同一份 demo 数据。

## 9. PSP 预留

- PSP 逻辑分辨率 480×272，和 PC 视口一致。
- PSP 内存只有 32MB，资产/图集要有预算。
- PSP 工具链可能不支持 `std::filesystem`，runtime 必须走平台读文件 + 内存 p3d。
- 纹理 swizzle 只放 PSP 后端。
- 不复制 runtime，不写平台分支进 Game。
- 同一份 p3d 跑 PC/3DS/PSP。
- 当前已补 PSP 骨架：
  - `engine/platform/_psp/`
  - `engine/render/backend_psp.cpp`
  - `runtime/psp_main.cpp`
  - `runtime/audio_psp.cpp`
  - `cmake/toolchain-psp.cmake`
  - `tools/build-psp.ps1`
  - 状态与限制见 `docs/21-PSP后端.md`。
- PSP 骨架尚未在真实 pspdev 上编译验证；不能把“语法检查通过”当成“能跑”。

## 10. 收尾定义

完成必须同时满足：

- 构建通过；
- capabilities 通过；
- 三个 demo 能导出、能启动；
- 目标平台有实跑证据；
- 数据版本已同步；
- `tools/package.ps1` 通过（需要发布时）；
- `git status` 干净；
- 提交信息说明改了什么、版本是否变化。

## 11. 大改保护

- 删除超过 100 行：先列引用，先提交，分两次删。
- 改数据格式：先改运行时读取，再改导出，再改 demo。
- 改图层/相机：用 16px 和 8px 各验一次。
- 改 UI 字体：生成 480×272、400×240 预览，检查底部裁切。
- 改规则：同时测 var_add/var_set 和一次区域触发。
- 改平台层：先 hello，再接 Game。

## 12. 常用路径

```text
构建：tools/build-editor.ps1
测试：build/host/bin/capabilities.exe
播放器：build/host/bin/PocketLoom-Play.exe
编辑器：build/host/bin/PocketLoom-Editor.exe
打包：tools/package.ps1
demo：samples/bee、samples/platformer、samples/topdown
3DS 参考：D:\GitHud\GameStudio\3DSStudioMini-PGMMV
```
