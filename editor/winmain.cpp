// editor/winmain.cpp
//
// GUI 子系统的入口壳。
//
// 为什么需要：
//   editor 的 CUI 版在 Windows 上双击会先弹一个黑底控制台；
//   用户看到的是控制台（中文还是乱码），GUI 窗口反而在它后面。
//   => 手动测试包必须用 -mwindows 的 GUI 子系统。
//
// 这个文件只做一件事：把 WinMain 的命令行转给同一个 main()。
//   ★ 不复制任何编辑器逻辑 ★ —— CUI / GUI 两个目标共享 main.cpp。
#include <windows.h>

#include <cstdlib>

int main(int argc, char** argv);

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  return main(__argc, __argv);
}
