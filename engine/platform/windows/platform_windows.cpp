// engine/platform/windows/platform_windows.cpp
//
// PC 平台：单窗口 240x160，GDI 呈现 + 键盘 + 计时。
#include "platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "base/color.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace eng {
namespace {

struct WinState {
  HWND hwnd = nullptr;
  HDC hdc = nullptr;
  bool quit = false;
  bool running = false;

  int w = 240;
  int h = 160;
  int scale = 3;

  u32* frame = nullptr;
  u32* present = nullptr;

  InputState input;
  LARGE_INTEGER qpc_freq;
  LARGE_INTEGER qpc_start;
  BITMAPINFO bmi;
};

WinState g;

bool KeyFromVk(WPARAM vk, Key* out) {
  switch (vk) {
    case VK_UP:    *out = Key::kUp;     return true;
    case VK_DOWN:  *out = Key::kDown;   return true;
    case VK_LEFT:  *out = Key::kLeft;   return true;
    case VK_RIGHT: *out = Key::kRight;  return true;
    case 'X':      *out = Key::kA;      return true;
    case 'Z':      *out = Key::kB;      return true;
    case 'S':      *out = Key::kX;      return true;
    case 'A':      *out = Key::kY;      return true;
    case 'Q':      *out = Key::kL;      return true;
    case 'E':      *out = Key::kR;      return true;
    case VK_RETURN:    *out = Key::kStart;  return true;
    case VK_BACK:      *out = Key::kSelect; return true;
    default:       return false;
  }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
      g.quit = true;
      return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      const bool was_down = (lp & (1 << 30)) != 0;
      Key k;
      if (KeyFromVk(wp, &k)) {
        const int i = static_cast<int>(k);
        if (!was_down && !g.input.down[i]) g.input.pressed[i] = true;
        g.input.down[i] = true;
        return 0;
      }
      break;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
      Key k;
      if (KeyFromVk(wp, &k)) {
        g.input.down[static_cast<int>(k)] = false;
        return 0;
      }
      break;
    }

    case WM_ERASEBKGND:
      return 1;

    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void Present() {
  if (g.hdc == nullptr || g.frame == nullptr || g.present == nullptr) return;
  for (int i = 0; i < g.w * g.h; ++i) g.present[i] = SwapRB(g.frame[i]);

  RECT rc;
  GetClientRect(g.hwnd, &rc);
  const int cw = rc.right - rc.left;
  const int ch = rc.bottom - rc.top;
  if (cw <= 0 || ch <= 0) return;

  g.bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  g.bmi.bmiHeader.biWidth = g.w;
  g.bmi.bmiHeader.biHeight = -g.h;
  g.bmi.bmiHeader.biPlanes = 1;
  g.bmi.bmiHeader.biBitCount = 32;
  g.bmi.bmiHeader.biCompression = BI_RGB;

  SetStretchBltMode(g.hdc, COLORONCOLOR);
  StretchDIBits(g.hdc, 0, 0, cw, ch, 0, 0, g.w, g.h, g.present, &g.bmi,
                DIB_RGB_COLORS, SRCCOPY);
}

static void PlatLogSink(void* user, LogLevel level, const char* text) {
  (void)user;
  const char* tag = "INF";
  switch (level) {
    case LogLevel::kDebug: tag = "DBG"; break;
    case LogLevel::kInfo:  tag = "INF"; break;
    case LogLevel::kWarn:  tag = "WRN"; break;
    case LogLevel::kError: tag = "ERR"; break;
  }
  std::fprintf(stderr, "[%s] %s\n", tag, (text != nullptr) ? text : "");
  std::fflush(stderr);
}

bool PlatInit(const PlatformConfig& cfg) {
  SetProcessDPIAware();
  log_set_sink(&PlatLogSink, nullptr);
  g.w = cfg.width > 0 ? cfg.width : 240;
  g.h = cfg.height > 0 ? cfg.height : 160;
  g.scale = cfg.window_scale > 0 ? cfg.window_scale : 1;
  g.frame = static_cast<u32*>(std::malloc(sizeof(u32) * static_cast<usize>(g.w) * g.h));
  g.present = static_cast<u32*>(std::malloc(sizeof(u32) * static_cast<usize>(g.w) * g.h));
  if (g.frame == nullptr || g.present == nullptr) {
    std::fprintf(stderr, "[platform] 帧缓冲分配失败\n");
    return false;
  }
  std::memset(g.frame, 0, sizeof(u32) * static_cast<usize>(g.w) * g.h);

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"PocketLoomPlatform";
  if (RegisterClassExW(&wc) == 0) {
    std::fprintf(stderr, "[platform] RegisterClassExW 失败\n");
    return false;
  }

  RECT want = {0, 0, g.w * g.scale, g.h * g.scale};
  AdjustWindowRect(&want, WS_OVERLAPPEDWINDOW, FALSE);
  wchar_t title[128];
  int ti = 0;
  for (; cfg.title[ti] != '\0' && ti < 127; ++ti) {
    title[ti] = static_cast<wchar_t>(static_cast<unsigned char>(cfg.title[ti]));
  }
  title[ti] = L'\0';

  g.hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           want.right - want.left, want.bottom - want.top,
                           nullptr, nullptr, wc.hInstance, nullptr);
  if (g.hwnd == nullptr) {
    std::fprintf(stderr, "[platform] CreateWindowExW 失败\n");
    return false;
  }
  g.hdc = GetDC(g.hwnd);
  ShowWindow(g.hwnd, SW_SHOW);
  UpdateWindow(g.hwnd);

  QueryPerformanceFrequency(&g.qpc_freq);
  QueryPerformanceCounter(&g.qpc_start);
  g.running = true;
  g.quit = false;
  return true;
}

void PlatShutdown() {
  if (g.hdc != nullptr && g.hwnd != nullptr) {
    ReleaseDC(g.hwnd, g.hdc);
    g.hdc = nullptr;
  }
  if (g.hwnd != nullptr) {
    DestroyWindow(g.hwnd);
    g.hwnd = nullptr;
  }
  std::free(g.frame);
  std::free(g.present);
  g.frame = nullptr;
  g.present = nullptr;
  g.running = false;
}

void PlatBeginFrame() {
  std::memset(g.input.pressed, 0, sizeof(g.input.pressed));
}
void PlatEndFrame() { Present(); }

void PlatPumpEvents() {
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      g.quit = true;
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

bool PlatShouldQuit() { return g.quit; }
const InputState& PlatInput() { return g.input; }

u64 PlatTickUs() {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (g.qpc_freq.QuadPart == 0) return 0;
  const i64 d = now.QuadPart - g.qpc_start.QuadPart;
  return static_cast<u64>((d * 1000000) / g.qpc_freq.QuadPart);
}

void PlatLogWrite(LogLevel lv, const char* text) {
  const char* tag = "INF";
  switch (lv) {
    case LogLevel::kDebug: tag = "DBG"; break;
    case LogLevel::kInfo:  tag = "INF"; break;
    case LogLevel::kWarn:  tag = "WRN"; break;
    case LogLevel::kError: tag = "ERR"; break;
  }
  std::fprintf(stdout, "[%s] %s\n", tag, text);
  std::fflush(stdout);
}

SoftFrameBuffer PlatSoftFramebuffer() {
  SoftFrameBuffer fb;
  fb.pixels = g.frame;
  fb.width = g.w;
  fb.height = g.h;
  fb.pitch = g.w;
  return fb;
}

DisplayImage PlatReadDisplay() {
  DisplayImage di;
  di.pixels = g.frame;
  di.width = g.w;
  di.height = g.h;
  di.pitch = g.w;
  return di;
}

}  // namespace

const IPlatform kPlatform = {
    PlatInit,
    PlatShutdown,
    PlatBeginFrame,
    PlatEndFrame,
    PlatPumpEvents,
    PlatShouldQuit,
    PlatInput,
    PlatTickUs,
    PlatLogWrite,
    PlatSoftFramebuffer,
    PlatReadDisplay,
};

}  // namespace eng
