# 21 - PSP 后端（第一版骨架）

> 状态：**已在 PPSSPP 1.20.4 实跑**（2026-11-07）。EBOOT 能启动、PARAM.SFO 正确、进入
> `user_main`，但画面全黑。根因已定位：`eng::DrawList` 实测 **360.9KB**，`psp_main` 原来把它
> 放在 `main()` 的栈上，而主线程栈只申请了 256KB —— 进循环前就栈溢出。
> 已改为静态区，**待 CI 重编后复验**。
> 本机没有 pspdev（也没有 WSL），改 PSP 必须走 GitHub Actions 重编。
> 目标：先把 PSP 相关源码和构建入口补齐，方便对照 3DS/PC 后端，也方便以后定位问题。

## 1. 文件

| 文件 | 作用 |
|---|---|
| `engine/platform/_psp/platform_psp.cpp` | 输入、计时、显示模式、日志 |
| `engine/render/backend_psp.cpp` | PSP GU 后端：clear / rect / image |
| `runtime/psp_main.cpp` | PSP 播放器入口，读 p3d，跑 Game |
| `runtime/audio_psp.cpp` | 音频占位（静音） |
| `cmake/toolchain-psp.cmake` | pspdev 工具链 |
| `cmake/psp-package.cmake` | ELF → EBOOT.PBP |
| `tools/build-psp.ps1` | 一键构建 |

## 2. 和 PC 的对应关系

| 职责 | PC | PSP |
|---|---|---|
| 平台 | `platform/windows/platform_windows.cpp` | `platform/_psp/platform_psp.cpp` |
| 渲染 | `render/backend_soft.cpp` | `render/backend_psp.cpp` |
| 入口 | `runtime/pc_main.cpp` | `runtime/psp_main.cpp` |
| 音频 | `audio_miniaudio.cpp` | `audio_psp.cpp` |
| 输出 | 窗口 | GU → EBOOT.PBP |

## 3. 设计约定

- PSP 只运行 runtime，不运行编辑器。
- `runtime/game` / `p3d` 不 include PSP 头。
- PSP 差异只在：
  - `engine/platform/_psp`
  - `engine/render/backend_psp.cpp`
  - `runtime/audio_psp.cpp`
  - `runtime/psp_main.cpp`
- 同一份 p3d 跑 PC / 3DS / PSP。
- GUI 后端只消费 `DrawList`，不直接读 Game 状态。

## 4. 路径

默认：

```text
ms0:/PSP/GAME/PocketLoom/EBOOT.PBP
ms0:/PSP/GAME/PocketLoom/game.p3d
ms0:/PSP/SAVEDATA/PocketLoom/save0.dat
ms0:/PSP/SAVEDATA/PocketLoom/report.txt
```

也支持直接读当前目录的 `game.p3d`，方便 PPSSPP 运行。

## 5. 构建

**Windows 原生没有官方 pspdev。** 官方 Windows 方案是 WSL2 Ubuntu。

### 5.1 WSL2 / Ubuntu

管理员 PowerShell：

```powershell
wsl --install -d Ubuntu
```

重启后进 Ubuntu，在仓库目录执行：

```bash
bash tools/setup-psp-wsl.sh
bash tools/build-psp-wsl.sh
```

### 5.2 Linux / CI

```bash
export PSPDEV="$HOME/pspdev"
export PATH="$PATH:$PSPDEV/bin"
cmake -S . -B build/psp-wsl -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-psp.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/psp-wsl
```

### 5.3 产物

```text
build/psp-wsl/bin/game_psp.elf
build/psp-wsl/EBOOT.PBP
```

> 2026-11-06：GitHub Actions 已编过，生成 `game_psp.elf` 和 `EBOOT.PBP`。  
> 2026-11-07：在 PPSSPP 1.20.4 实跑 —— 启动正常、黑屏，根因是 DrawList 占栈超 256KB（见 §6.9）。

### 5.4 不用 WSL 的方案

1. **GitHub Actions（推荐）**
   - 仓库已有 `.github/workflows/psp.yml`；
   - 在 GitHub Actions 手动运行 **PSP build**；
   - 下载 artifact `pocketloom-psp`，里面有 `game_psp.elf` / `EBOOT.PBP`；
   - 本机不需要装 WSL 或 pspdev。

2. **Linux 虚拟机**
   - VirtualBox / Hyper-V 装 Ubuntu；
   - 在 Ubuntu 里执行 `tools/setup-psp-wsl.sh`；
   - 仓库通过共享目录挂进去即可。

3. **Docker（Hyper-V 后端）**
   - Docker Desktop 选 Hyper-V 而不是 WSL2；
   - 用 Ubuntu + pspdev 容器构建；
   - 本质上仍然是 Linux 环境。

4. **Windows 原生 MinPSP**
   - 有旧的 `Minimalist-PSPSDK`；
   - 太老，不支持当前 C++17/CMake 工程；
   - 不推荐。

**没有官方 Windows 原生 pspdev。** 不想用 WSL，就走 GitHub Actions 或 Linux VM。

### 5.5 本地 PPSSPP 复跑（Windows，已实测）

`ppsspp_win/PPSSPPWindows64.exe` 是**便携模式**：exe 目录可写时，memstick 根目录是
`<repo>\ppsspp_win\memstick\PSP\`，**不是** `文档\PPSSPP`。放错地方的表现是 EBOOT
读完 p3d 失败、直接退回 PPSSPP 菜单（窗口标题没有 `- PocketLoom`）。

```powershell
# 1) 部署：注意 memstick 下面还要有 PSP 这一层
$ms = "D:\GitHud\GameStudio\ppsspp_win\memstick\PSP"
New-Item -ItemType Directory -Force -Path "$ms\GAME\PocketLoom" | Out-Null
Copy-Item "dist\psp\EBOOT.PBP" "$ms\GAME\PocketLoom\" -Force
Copy-Item "dist\psp\game.p3d"  "$ms\GAME\PocketLoom\" -Force

# 2) 打开日志（第一次跑完 PPSSPP 才会生成 ini；改完重启 PPSSPP）
#    $ms\SYSTEM\ppsspp.ini  =>  [Log] FileLogging = True
#    日志落在 $ms\SYSTEM\DUMP\log.txt

# 3) 跑
& "D:\GitHud\GameStudio\ppsspp_win\PPSSPPWindows64.exe" "$ms\GAME\PocketLoom\EBOOT.PBP"
```

判据：

- **跑通**：窗口标题带 `- PocketLoom`，画面有内容，退出后
  `$ms\SAVEDATA\PocketLoom\report.txt` 里 `ok=1`。
- **黑屏**：`SYSTEM\DUMP\log.txt` 里出现成片的
  `E[MEMMAP] Bad memory access ... 00000048 (00000204A8E30048)` —— 这类
  `0x00~0xA0` 的低地址就是「空/坏指针 + 字段偏移」，优先查栈和控制块。
- 注意 EBOOT 里没有 `--frames` 的传参途径（PPSSPP 不透传 argv），主循环只能靠关窗口结束，
  所以 `report.txt` 在正常退出前不会落盘 —— 判断是否活跑要看画面和标题。

## 6. 已知限制

1. **纹理必须补成 2 的幂**
   - PSP GU 对纹理尺寸有要求；
   - `backend_psp.cpp` 会把纹理补边到 POT；
   - 超过 1024 会被拒绝。

2. **颜色顺序需要真机验证**
   - 当前按 `0xAABBGGRR` 传给 GU；
   - 如果红蓝反了，只改 `ToGuColor()`。

3. **p3d 已移除 `<filesystem>`，但文件接口仍用 iostream**
   - `runtime/p3d.h`、`runtime/game.cpp` 已不再 include `<filesystem>`；
   - `P3dLoadFile` / `P3dSave` 改用 `std::string` 路径；
   - PSP 端只用 `P3dLoad(bytes)`，不依赖路径接口；
   - 如果 psp-g++ 的 iostream 不可用，再把文件接口移到 `sceIo`。

4. **存档路径还没抽象**
   - 现在 `psp_main` 直接写 `ms0:`；
   - 以后应统一为 `PlatSavePath()`。

5. **音频是静音占位**
   - `audio_psp.cpp` 只保证链接；
   - 后续接 `sceAudio`。

6. **没有 HOME/退出键**
   - PSP 没有可直接读的 HOME 键；
   - 目前靠 PPSSPP 停止或 `--frames`。

7. **EBOOT 打包已验证**
   - GitHub Actions 打出的 `EBOOT.PBP` 能被 PPSSPP 1.20.4 正常识别并启动（标题显示 PocketLoom）。

8. **32MB 内存**
   - 当前 p3d/纹理还没做预算；
   - 大图集要先估内存。

9. **栈预算：大对象不能放栈上（2026-11-07 已修）**
   - `sizeof(DrawList)=369512`（`kMaxCmds=6144` × `sizeof(DrawCmd)=60` ≈ 360.9KB）；
   - 主线程栈 `PSP_MAIN_THREAD_STACK_SIZE_KB(256)` 只有 256KB，放栈上必溢出；
   - 现在改成 `static eng::DrawList dl;`（BSS）；
   - 以后往 `psp_main` / `Game` 里加大缓冲，先按 256KB 算一遍栈账。

## 7. 下一步

1. ~~安装 pspdev，跑通 `game_psp.elf`~~ → 走 CI 编出 EBOOT（已完成）。
2. ~~跑通 EBOOT.PBP~~ → PPSSPP 能启动，**黑屏待修**（§6.9 已改，等重编复验）。
3. 重编后复验：清屏、图块、对象三条依次确认。
4. 再接输入、音频、存档、UI。
5. 把 `P3dLoadFile` / `P3dSave` 的文件访问移到平台层（`sceIo`）。

## 8. 和 3DS 的关系

3DS 后端只作为参考：

- `engine/platform/_3ds`
- `engine/render/backend_citro.cpp`
- `runtime/3ds_main.cpp`

PSP 不复用旧 3DS runtime/editor/demo；只对照平台层和提交顺序。
