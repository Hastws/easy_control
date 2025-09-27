// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#include <chrono>
#include <iostream>
#include <thread>

#include "system_input.hpp"

using namespace autoalg;

int main() {
  std::cout << "pid=" << ProcessId() << ", hw=" << NumHWThreads() << "\n";
  std::cout << "exe=" << ExecutablePath().string() << "\n";
  std::cout << "home=" << HomeDir().string() << "\n";
  std::cout << "tmp=" << TempDir().string() << "\n";

  SleepSeconds(2);
  SystemInput in;
  // 同步系统光标（mac/Win/X11 有效）
  in.SyncCursorFromSystem();

  // // 相对移动 & 单击
  // in.MouseMoveRelative(200, 200);
  // Sleep(2);
  // in.MouseClick(SystemInput::kLeft);

  // 滚动
  // in.ScrollLines(3000, -3);

  // 组合键（举例：mac Command+C / Win Ctrl+C / Linux Super+C 请自行传入对应key）
  // 这里演示用 'C' 的虚拟键/KeyCode 需按平台传入（示例用 ASCII 映射可能不适配所有布局）
  // int key_c = in.CharToKeyCode('c');
  // if (key_c >= 0) {
  //     // mac: Command，Win: Windows，Linux: Super —— 如要 Ctrl 请替换成 kControl
  //     in.KeyboardClickWithMods(key_c, SystemInput::kCommand);
  // }
  SleepSeconds(2);
  // 拖拽示例：按住左键拖 200 像素再抬起
  in.MouseDragBy(50, 50, SystemInput::kLeft);
  SleepSeconds(2);

  // 输出当前屏幕尺寸和坐标（内部状态）
  printf("Display: %zu x %zu, Cursor: (%d, %d)\n", in.GetDisplayWidth(), in.GetDisplayHeight(), in.CursorX(), in.CursorY());

  int x;
  int y;
  in.GetCursorPixel(x,y);
  std::cout << x << ", " << y << "\n";
  in.GetPrimaryDisplayPixelSize(x,y);

  std::cout << x << ", " << y << "\n";

  return 0;
}
