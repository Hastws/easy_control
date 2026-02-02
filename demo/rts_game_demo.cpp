// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.
//
// RTS游戏控制演示 - 星际争霸/即时战略游戏风格
// 
// 演示如何使用 easy_control 实现云游戏中的RTS操作：
//   - 框选单位
//   - 右键移动/攻击
//   - 快捷键操作
//   - 小地图点击
//   - 编队控制
//
// 编译:
//   cmake --build build -j
//
// 使用:
//   ./rts_demo [模式]
//   模式: 
//     0 = 仅截图 (安全模式)
//     1 = 模拟操作 (会实际控制鼠标键盘!)

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <fstream>

#include "system_input.hpp"
#include "system_output.hpp"

using namespace autoalg;
using namespace std::chrono;

// ============================================================================
// RTS 游戏操作封装
// ============================================================================
class RTSController {
 public:
  RTSController() {
    // 获取屏幕尺寸
    screen_w_ = static_cast<int>(input_.GetDisplayWidth());
    screen_h_ = static_cast<int>(input_.GetDisplayHeight());
    
    // 假设的游戏UI布局（可根据实际游戏调整）
    // 小地图区域（左下角）
    minimap_x_ = 0;
    minimap_y_ = screen_h_ - 200;
    minimap_w_ = 200;
    minimap_h_ = 200;
    
    // 主游戏区域
    game_area_x_ = 0;
    game_area_y_ = 0;
    game_area_w_ = screen_w_;
    game_area_h_ = screen_h_ - 150;  // 底部UI
  }

  // ========== 单位选择 ==========
  
  // 点击选择单位
  void selectUnit(int x, int y) {
    std::cout << "  [选择] 点击选择单位 @ (" << x << ", " << y << ")\n";
    input_.MouseClickAt(x, y, SystemInput::kLeft);
    sleepMs(50);
  }

  // 框选单位（从起点拖拽到终点）
  void boxSelect(int x1, int y1, int x2, int y2) {
    std::cout << "  [框选] 从 (" << x1 << "," << y1 << ") 到 (" << x2 << "," << y2 << ")\n";
    input_.MouseMoveTo(x1, y1);
    sleepMs(30);
    input_.MouseDown(SystemInput::kLeft);
    sleepMs(30);
    
    // 平滑拖拽
    int steps = 10;
    for (int i = 1; i <= steps; ++i) {
      int cx = x1 + (x2 - x1) * i / steps;
      int cy = y1 + (y2 - y1) * i / steps;
      input_.MouseMoveTo(cx, cy);
      sleepMs(15);
    }
    
    input_.MouseUp(SystemInput::kLeft);
    sleepMs(50);
  }

  // Ctrl+点击 添加到选择
  void addToSelection(int x, int y) {
    std::cout << "  [添加选择] Ctrl+点击 @ (" << x << ", " << y << ")\n";
    input_.MouseMoveTo(x, y);
    input_.KeyboardDown(input_.CharToKeyCode('c') - 2);  // Ctrl key
    sleepMs(20);
    input_.MouseClick(SystemInput::kLeft);
    sleepMs(20);
    input_.KeyboardUp(input_.CharToKeyCode('c') - 2);
    sleepMs(30);
  }

  // ========== 命令操作 ==========

  // 右键移动/攻击目标
  void rightClickCommand(int x, int y) {
    std::cout << "  [命令] 右键命令 @ (" << x << ", " << y << ")\n";
    input_.MouseClickAt(x, y, SystemInput::kRight);
    sleepMs(50);
  }

  // 攻击移动 (A+左键)
  void attackMove(int x, int y) {
    std::cout << "  [A移动] 攻击移动到 (" << x << ", " << y << ")\n";
    int key_a = input_.CharToKeyCode('a');
    if (key_a >= 0) {
      input_.KeyboardClick(key_a);
      sleepMs(30);
      input_.MouseClickAt(x, y, SystemInput::kLeft);
      sleepMs(50);
    }
  }

  // 停止 (S键)
  void stop() {
    std::cout << "  [停止] 按下S键\n";
    int key_s = input_.CharToKeyCode('s');
    if (key_s >= 0) {
      input_.KeyboardClick(key_s);
      sleepMs(50);
    }
  }

  // 保持位置 (H键)
  void holdPosition() {
    std::cout << "  [保持] 按下H键\n";
    int key_h = input_.CharToKeyCode('h');
    if (key_h >= 0) {
      input_.KeyboardClick(key_h);
      sleepMs(50);
    }
  }

  // 巡逻 (P键)
  void patrol(int x, int y) {
    std::cout << "  [巡逻] 巡逻到 (" << x << ", " << y << ")\n";
    int key_p = input_.CharToKeyCode('p');
    if (key_p >= 0) {
      input_.KeyboardClick(key_p);
      sleepMs(30);
      input_.MouseClickAt(x, y, SystemInput::kLeft);
      sleepMs(50);
    }
  }

  // ========== 编队操作 ==========

  // 创建编队 (Ctrl + 数字)
  void createGroup(int group_num) {
    std::cout << "  [编队] 创建编队 " << group_num << " (Ctrl+" << group_num << ")\n";
    int key = input_.CharToKeyCode('0' + group_num);
    if (key >= 0) {
      input_.KeyboardClickWithMods(key, SystemInput::kControl);
      sleepMs(50);
    }
  }

  // 选择编队 (数字键)
  void selectGroup(int group_num) {
    std::cout << "  [编队] 选择编队 " << group_num << "\n";
    int key = input_.CharToKeyCode('0' + group_num);
    if (key >= 0) {
      input_.KeyboardClick(key);
      sleepMs(50);
    }
  }

  // 双击聚焦编队
  void focusGroup(int group_num) {
    std::cout << "  [编队] 聚焦编队 " << group_num << " (双击)\n";
    int key = input_.CharToKeyCode('0' + group_num);
    if (key >= 0) {
      input_.KeyboardClick(key);
      sleepMs(30);
      input_.KeyboardClick(key);
      sleepMs(50);
    }
  }

  // ========== 视角控制 ==========

  // 小地图点击
  void clickMinimap(float rel_x, float rel_y) {
    int x = minimap_x_ + static_cast<int>(minimap_w_ * rel_x);
    int y = minimap_y_ + static_cast<int>(minimap_h_ * rel_y);
    std::cout << "  [小地图] 点击 (" << x << ", " << y << ")\n";
    input_.MouseClickAt(x, y, SystemInput::kLeft);
    sleepMs(50);
  }

  // 键盘移动视角
  void panCamera(int dx, int dy) {
    // 移动鼠标到屏幕边缘
    int edge_x = (dx > 0) ? screen_w_ - 5 : (dx < 0 ? 5 : screen_w_ / 2);
    int edge_y = (dy > 0) ? screen_h_ - 5 : (dy < 0 ? 5 : screen_h_ / 2);
    
    std::cout << "  [视角] 平移到边缘 (" << edge_x << ", " << edge_y << ")\n";
    input_.MouseMoveTo(edge_x, edge_y);
    sleepMs(200);  // 保持在边缘一段时间
    input_.MouseMoveTo(screen_w_ / 2, screen_h_ / 2);  // 回到中心
    sleepMs(50);
  }

  // ========== 建造/技能 ==========

  // 按技能快捷键
  void pressHotkey(char key) {
    std::cout << "  [快捷键] 按下 " << key << "\n";
    int kc = input_.CharToKeyCode(key);
    if (kc >= 0) {
      input_.KeyboardClick(kc);
      sleepMs(50);
    }
  }

  // Shift队列命令
  void shiftCommand(int x, int y) {
    std::cout << "  [队列] Shift+右键 @ (" << x << ", " << y << ")\n";
    input_.KeyboardDown(42);  // Shift (Linux keycode, 可能需要调整)
    sleepMs(20);
    input_.MouseClickAt(x, y, SystemInput::kRight);
    sleepMs(20);
    input_.KeyboardUp(42);
    sleepMs(30);
  }

  // ========== 屏幕捕获 ==========
  
  bool captureScreen(ImageRGBA& out) {
    return SystemOutput::CaptureScreenWithCursor(0, out);
  }

  // 保存截图
  bool saveScreenshot(const std::string& filename) {
    ImageRGBA img;
    if (!captureScreen(img)) return false;
    
    // 转为BMP
    std::vector<uint8_t> bgra(img.pixels);
    for (size_t i = 0; i < bgra.size(); i += 4) {
      std::swap(bgra[i], bgra[i + 2]);
    }

#pragma pack(push, 1)
    struct BMPFileHeader { uint16_t t{0x4D42}; uint32_t s{}; uint16_t r1{}, r2{}; uint32_t o{54}; };
    struct BMPInfoHeader { uint32_t s{40}; int32_t w{}, h{}; uint16_t p{1}, b{32}; uint32_t c{}, i{}; int32_t x{2835}, y{2835}; uint32_t u{}, m{}; };
#pragma pack(pop)

    BMPFileHeader fh; BMPInfoHeader ih;
    ih.w = img.width; ih.h = -img.height; ih.i = static_cast<uint32_t>(bgra.size());
    fh.s = fh.o + ih.i;

    std::ofstream f(filename, std::ios::binary);
    if (!f) return false;
    f.write((const char*)&fh, sizeof(fh));
    f.write((const char*)&ih, sizeof(ih));
    f.write((const char*)bgra.data(), bgra.size());
    return f.good();
  }

  int screenWidth() const { return screen_w_; }
  int screenHeight() const { return screen_h_; }

 private:
  SystemInput input_;
  int screen_w_, screen_h_;
  int minimap_x_, minimap_y_, minimap_w_, minimap_h_;
  int game_area_x_, game_area_y_, game_area_w_, game_area_h_;

  void sleepMs(int ms) {
    std::this_thread::sleep_for(milliseconds(ms));
  }
};

// ============================================================================
// 演示场景
// ============================================================================

void demoBasicOperations(RTSController& rts) {
  std::cout << "\n=== 演示1: 基础单位操作 ===\n";
  
  int cx = rts.screenWidth() / 2;
  int cy = rts.screenHeight() / 2;

  // 1. 点击选择单位
  rts.selectUnit(cx, cy);
  std::this_thread::sleep_for(milliseconds(300));

  // 2. 框选单位
  rts.boxSelect(cx - 200, cy - 150, cx + 200, cy + 150);
  std::this_thread::sleep_for(milliseconds(300));

  // 3. 右键移动
  rts.rightClickCommand(cx + 300, cy);
  std::this_thread::sleep_for(milliseconds(300));

  // 4. 攻击移动
  rts.attackMove(cx - 300, cy);
  std::this_thread::sleep_for(milliseconds(300));
}

void demoGroupControl(RTSController& rts) {
  std::cout << "\n=== 演示2: 编队控制 ===\n";

  int cx = rts.screenWidth() / 2;
  int cy = rts.screenHeight() / 2;

  // 框选一组单位
  rts.boxSelect(cx - 100, cy - 100, cx + 100, cy + 100);
  std::this_thread::sleep_for(milliseconds(200));

  // 创建编队1
  rts.createGroup(1);
  std::this_thread::sleep_for(milliseconds(300));

  // 点击其他地方取消选择
  rts.selectUnit(cx + 400, cy);
  std::this_thread::sleep_for(milliseconds(200));

  // 按1选择编队
  rts.selectGroup(1);
  std::this_thread::sleep_for(milliseconds(200));

  // 双击1聚焦
  rts.focusGroup(1);
  std::this_thread::sleep_for(milliseconds(300));
}

void demoMinimapAndCamera(RTSController& rts) {
  std::cout << "\n=== 演示3: 视角与小地图 ===\n";

  // 小地图点击
  rts.clickMinimap(0.2f, 0.3f);
  std::this_thread::sleep_for(milliseconds(500));

  rts.clickMinimap(0.8f, 0.7f);
  std::this_thread::sleep_for(milliseconds(500));

  // 屏幕边缘平移
  rts.panCamera(1, 0);  // 向右
  std::this_thread::sleep_for(milliseconds(300));

  rts.panCamera(-1, 0); // 向左
  std::this_thread::sleep_for(milliseconds(300));
}

void demoBuildAndAbility(RTSController& rts) {
  std::cout << "\n=== 演示4: 建造与技能 ===\n";

  int cx = rts.screenWidth() / 2;
  int cy = rts.screenHeight() / 2;

  // 模拟选择农民并建造
  rts.selectUnit(cx - 200, cy);
  std::this_thread::sleep_for(milliseconds(200));

  // 按B进入建造菜单
  rts.pressHotkey('b');
  std::this_thread::sleep_for(milliseconds(200));

  // 按B建造兵营
  rts.pressHotkey('b');
  std::this_thread::sleep_for(milliseconds(200));

  // 点击放置位置
  rts.selectUnit(cx + 100, cy + 100);
  std::this_thread::sleep_for(milliseconds(300));

  // Shift队列多个建造
  rts.shiftCommand(cx + 200, cy + 100);
  rts.shiftCommand(cx + 300, cy + 100);
  std::this_thread::sleep_for(milliseconds(300));
}

void demoScreenCapture(RTSController& rts) {
  std::cout << "\n=== 演示5: 屏幕捕获 ===\n";

  // 捕获并保存截图
  for (int i = 0; i < 3; ++i) {
    std::string filename = "rts_screenshot_" + std::to_string(i) + ".bmp";
    auto start = steady_clock::now();
    if (rts.saveScreenshot(filename)) {
      auto end = steady_clock::now();
      auto ms = duration_cast<milliseconds>(end - start).count();
      std::cout << "  [截图] 保存 " << filename << " (" << ms << "ms)\n";
    }
    std::this_thread::sleep_for(milliseconds(200));
  }
}

// ============================================================================
// 主程序
// ============================================================================
int main(int argc, char* argv[]) {
  std::cout << "==============================================\n";
  std::cout << "   easy_control RTS游戏控制演示\n";
  std::cout << "   (星际争霸/即时战略风格)\n";
  std::cout << "==============================================\n\n";

  int mode = 0;  // 默认安全模式
  if (argc > 1) {
    mode = std::atoi(argv[1]);
  }

  std::cout << "显示器数量: " << SystemOutput::GetDisplayCount() << "\n";

  RTSController rts;
  std::cout << "屏幕分辨率: " << rts.screenWidth() << "x" << rts.screenHeight() << "\n\n";

  if (mode == 0) {
    std::cout << ">>> 安全模式: 仅演示屏幕捕获\n";
    std::cout << "    使用 './rts_demo 1' 启用完整操作模拟\n\n";
    
    demoScreenCapture(rts);
    
  } else {
    std::cout << ">>> 完整模式: 将模拟鼠标键盘操作!\n";
    std::cout << "    警告: 这会实际控制你的电脑!\n";
    std::cout << "    按 Ctrl+C 可随时中断\n\n";
    
    std::cout << "3秒后开始...\n";
    std::this_thread::sleep_for(seconds(3));

    // 运行所有演示
    demoBasicOperations(rts);
    demoGroupControl(rts);
    demoMinimapAndCamera(rts);
    demoBuildAndAbility(rts);
    demoScreenCapture(rts);
  }

  std::cout << "\n演示完成!\n";
  return 0;
}
