// engine/render/backend_soft.h
//
// ★ 软件光栅后端的可指定目标版本 ★
//
// ★★ 为什么要这个，而不只是 SoftSubmit() ★★
//   `SoftSubmit(dl)` 会从 `Platform().soft_framebuffer()` 取目标 ——
//   那是★游戏窗口自己的缓冲★。
//   而编辑器视口要在★自己的★缓冲上渲染（然后传给 ImGui 当纹理）。
//   编辑器没有那个平台，也不该为了画一个面板去开第二个窗口。
//
//   ⇒ 把两件事分开：
//     · `SoftSubmitTo(dl, target)` —— ★ 画到给定的缓冲 ★（无平台依赖）
//     · `SoftSubmit(dl)`               —— 取平台缓冲再调上面那个
//   ★ 光栅化代码仍然只有一份 ★ —— 视口里的像素和游戏窗口里的像素
//   出自同一个函数，这正是 T4.8「与引擎直接输出逐像素一致」能成立的前提。
#pragma once

#include "platform/platform.h"
#include "render/draw_list.h"

namespace eng {

// ★ 把 DrawList 光栅化到指定帧缓冲 ★
//   top / bottom 各自独立；哪个无效就不画哪一屏。
void SoftSubmitTo(const DrawList& dl, SoftFrameBuffer target);

}  // namespace eng
