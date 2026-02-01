// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.
//
// Streaming Control Demo - 流式控制演示
// 模拟云游戏/远程桌面场景：持续截图 + 键盘鼠标输入
//
// 功能演示：
//   1. 帧捕获循环（模拟视频流传输）
//   2. 键盘鼠标事件模拟
//   3. 简单的交互式控制
//
// 编译 (Linux):
//   g++ -std=c++17 streaming_control_demo.cpp -o streaming_demo -lX11 -lXtst -lXrandr -lXfixes -lpthread
//
// 使用:
//   ./streaming_demo [目标FPS] [运行时长秒]
//   例如: ./streaming_demo 30 10   # 30fps运行10秒

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "system_input.hpp"
#include "system_output.hpp"

using namespace autoalg;
using namespace std::chrono;
using Clock = steady_clock;

// ============================================================================
// 帧数据结构
// ============================================================================
struct Frame {
  uint64_t frame_id{0};
  int64_t timestamp_ms{0};  // 捕获时间戳
  int width{0};
  int height{0};
  std::vector<uint8_t> rgba_data;  // RGBA像素数据

  size_t data_size() const { return rgba_data.size(); }
};

// ============================================================================
// 输入事件结构（模拟网络传输的输入指令）
// ============================================================================
// 注意：避免使用 KeyPress/KeyRelease，它们是 X11 的宏定义
enum class InputEventType { 
  MouseMove, 
  MouseClick, 
  MouseDrag, 
  KeyDown,      // 键按下
  KeyUp,        // 键抬起
  MouseScroll,  // 滚轮
  TextInput     // 文本输入
};

struct InputEvent {
  InputEventType type;
  int x{0}, y{0};              // 鼠标坐标
  int button{0};               // 鼠标按钮 0=左 1=右 2=中
  int key_code{0};             // 键盘码
  int scroll_dx{0}, scroll_dy{0};  // 滚动量
  std::string text;            // 输入文本
  uint64_t mods{0};            // 修饰键
};

// ============================================================================
// 性能统计
// ============================================================================
struct StreamStats {
  std::atomic<uint64_t> frames_captured{0};
  std::atomic<uint64_t> total_bytes{0};
  std::atomic<uint64_t> input_events_processed{0};
  std::atomic<double> avg_capture_time_ms{0};
  std::atomic<double> actual_fps{0};

  Clock::time_point start_time;

  void reset() {
    frames_captured = 0;
    total_bytes = 0;
    input_events_processed = 0;
    avg_capture_time_ms = 0;
    actual_fps = 0;
    start_time = Clock::now();
  }

  void print() const {
    auto elapsed = duration_cast<milliseconds>(Clock::now() - start_time).count();
    double elapsed_sec = elapsed / 1000.0;

    std::cout << "\n========== 流式控制统计 ==========\n";
    std::cout << "运行时间: " << std::fixed << std::setprecision(2) << elapsed_sec << " 秒\n";
    std::cout << "捕获帧数: " << frames_captured.load() << "\n";
    std::cout << "实际FPS: " << std::fixed << std::setprecision(1) << actual_fps.load() << "\n";
    std::cout << "平均捕获耗时: " << std::fixed << std::setprecision(2) << avg_capture_time_ms.load() << " ms\n";
    std::cout << "传输数据量: " << std::fixed << std::setprecision(2) << (total_bytes.load() / 1024.0 / 1024.0) << " MB\n";
    std::cout << "输入事件处理: " << input_events_processed.load() << " 次\n";
    std::cout << "==================================\n";
  }
};

// ============================================================================
// 帧缓冲区（生产者-消费者模式）
// ============================================================================
class FrameBuffer {
 public:
  explicit FrameBuffer(size_t max_size = 3) : max_size_(max_size) {}

  bool push(Frame&& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frames_.size() >= max_size_) {
      frames_.pop();  // 丢弃旧帧，保持实时性
    }
    frames_.push(std::move(frame));
    return true;
  }

  bool pop(Frame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frames_.empty()) return false;
    frame = std::move(frames_.front());
    frames_.pop();
    return true;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frames_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::queue<Frame> frames_;
  size_t max_size_;
};

// ============================================================================
// 输入事件队列
// ============================================================================
class InputQueue {
 public:
  void push(const InputEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push(event);
  }

  bool pop(InputEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return false;
    event = events_.front();
    events_.pop();
    return true;
  }

 private:
  std::mutex mutex_;
  std::queue<InputEvent> events_;
};

// ============================================================================
// 流式控制器
// ============================================================================
class StreamingController {
 public:
  StreamingController(int target_fps = 30, int display_index = 0)
      : target_fps_(target_fps), display_index_(display_index), running_(false) {
    frame_interval_us_ = 1000000 / target_fps_;
  }

  ~StreamingController() { stop(); }

  // 启动流式控制
  void start() {
    if (running_) return;
    running_ = true;
    stats_.reset();

    // 启动捕获线程
    capture_thread_ = std::thread(&StreamingController::captureLoop, this);

    // 启动输入处理线程
    input_thread_ = std::thread(&StreamingController::inputLoop, this);

    // 启动模拟"网络传输"线程（消费帧）
    consumer_thread_ = std::thread(&StreamingController::consumerLoop, this);

    std::cout << "[StreamingController] 已启动，目标FPS: " << target_fps_ << "\n";
  }

  void stop() {
    if (!running_) return;
    running_ = false;

    if (capture_thread_.joinable()) capture_thread_.join();
    if (input_thread_.joinable()) input_thread_.join();
    if (consumer_thread_.joinable()) consumer_thread_.join();

    std::cout << "[StreamingController] 已停止\n";
  }

  bool isRunning() const { return running_; }

  // 提交输入事件（模拟从网络接收）
  void submitInput(const InputEvent& event) { input_queue_.push(event); }

  // 获取统计信息
  const StreamStats& getStats() const { return stats_; }

  // 获取当前帧信息（用于显示）
  bool getCurrentFrame(Frame& frame) { return frame_buffer_.pop(frame); }

 private:
  // 帧捕获循环
  void captureLoop() {
    uint64_t frame_id = 0;
    double total_capture_time = 0;

    while (running_) {
      auto loop_start = Clock::now();

      // 捕获屏幕
      auto capture_start = Clock::now();
      ImageRGBA image;
      bool success = SystemOutput::CaptureScreenWithCursor(display_index_, image);
      auto capture_end = Clock::now();

      if (success) {
        double capture_ms = duration_cast<microseconds>(capture_end - capture_start).count() / 1000.0;
        total_capture_time += capture_ms;

        // 构建帧
        Frame frame;
        frame.frame_id = ++frame_id;
        frame.timestamp_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        frame.width = image.width;
        frame.height = image.height;
        frame.rgba_data = std::move(image.pixels);

        // 更新统计
        stats_.frames_captured++;
        stats_.total_bytes += frame.data_size();
        stats_.avg_capture_time_ms = total_capture_time / frame_id;

        // 放入缓冲区
        frame_buffer_.push(std::move(frame));
      }

      // 计算实际FPS
      auto elapsed = duration_cast<milliseconds>(Clock::now() - stats_.start_time).count();
      if (elapsed > 0) {
        stats_.actual_fps = (stats_.frames_captured.load() * 1000.0) / elapsed;
      }

      // 帧率控制
      auto loop_end = Clock::now();
      auto loop_duration = duration_cast<microseconds>(loop_end - loop_start).count();
      if (loop_duration < frame_interval_us_) {
        std::this_thread::sleep_for(microseconds(frame_interval_us_ - loop_duration));
      }
    }
  }

  // 输入事件处理循环
  void inputLoop() {
    while (running_) {
      InputEvent event;
      if (input_queue_.pop(event)) {
        processInput(event);
        stats_.input_events_processed++;
      } else {
        std::this_thread::sleep_for(milliseconds(1));
      }
    }
  }

  // 处理单个输入事件
  void processInput(const InputEvent& event) {
    switch (event.type) {
      case InputEventType::MouseMove:
        input_.MouseMoveTo(event.x, event.y);
        break;

      case InputEventType::MouseClick:
        input_.MouseClickAt(event.x, event.y, event.button);
        break;

      case InputEventType::MouseDrag:
        input_.MouseDragTo(event.x, event.y, event.button);
        break;

      case InputEventType::KeyDown:
        if (event.mods != 0) {
          input_.KeyboardDownWithMods(event.key_code, event.mods);
        } else {
          input_.KeyboardDown(event.key_code);
        }
        break;

      case InputEventType::KeyUp:
        if (event.mods != 0) {
          input_.KeyboardUpWithMods(event.key_code, event.mods);
        } else {
          input_.KeyboardUp(event.key_code);
        }
        break;

      case InputEventType::MouseScroll:
        input_.ScrollLines(event.scroll_dx, event.scroll_dy);
        break;

      case InputEventType::TextInput:
        input_.TypeUTF8(event.text);
        break;
    }
  }

  // 模拟消费者（网络传输/编码）
  void consumerLoop() {
    while (running_) {
      Frame frame;
      if (frame_buffer_.pop(frame)) {
        // 这里可以进行：
        // 1. 视频编码（H.264/H.265）
        // 2. 网络传输
        // 3. 写入文件
        // 目前只是模拟消费

        // 每100帧打印一次进度
        if (frame.frame_id % 100 == 0) {
          std::cout << "[Frame " << frame.frame_id << "] " << frame.width << "x" << frame.height << " @ "
                    << std::fixed << std::setprecision(1) << stats_.actual_fps.load() << " fps\n";
        }
      } else {
        std::this_thread::sleep_for(milliseconds(1));
      }
    }
  }

  int target_fps_;
  int display_index_;
  int64_t frame_interval_us_;
  std::atomic<bool> running_;

  SystemInput input_;
  FrameBuffer frame_buffer_;
  InputQueue input_queue_;
  StreamStats stats_;

  std::thread capture_thread_;
  std::thread input_thread_;
  std::thread consumer_thread_;
};

// ============================================================================
// 演示：模拟游戏操作序列
// ============================================================================
void runGameSimulation(StreamingController& controller, int duration_sec) {
  std::cout << "\n>>> 开始模拟游戏操作序列...\n";

  auto start = Clock::now();
  int action_count = 0;

  // 获取屏幕尺寸
  int screen_w = 1920, screen_h = 1080;  // 默认值

  while (duration_cast<seconds>(Clock::now() - start).count() < duration_sec) {
    // 模拟各种游戏操作
    int action = action_count % 8;

    switch (action) {
      case 0: {
        // 鼠标移动到屏幕中心
        InputEvent e;
        e.type = InputEventType::MouseMove;
        e.x = screen_w / 2;
        e.y = screen_h / 2;
        controller.submitInput(e);
        std::cout << "  [动作] 鼠标移动到中心 (" << e.x << ", " << e.y << ")\n";
        break;
      }

      case 1: {
        // 鼠标左键点击
        InputEvent e;
        e.type = InputEventType::MouseClick;
        e.x = screen_w / 2 + 100;
        e.y = screen_h / 2;
        e.button = 0;  // 左键
        controller.submitInput(e);
        std::cout << "  [动作] 左键点击 (" << e.x << ", " << e.y << ")\n";
        break;
      }

      case 2: {
        // 鼠标右键点击
        InputEvent e;
        e.type = InputEventType::MouseClick;
        e.x = screen_w / 2 - 100;
        e.y = screen_h / 2;
        e.button = 1;  // 右键
        controller.submitInput(e);
        std::cout << "  [动作] 右键点击 (" << e.x << ", " << e.y << ")\n";
        break;
      }

      case 3: {
        // 滚轮滚动
        InputEvent e;
        e.type = InputEventType::MouseScroll;
        e.scroll_dx = 0;
        e.scroll_dy = 3;
        controller.submitInput(e);
        std::cout << "  [动作] 滚轮向上滚动\n";
        break;
      }

      case 4: {
        // 模拟 WASD 移动 - W键
        InputEvent down, up;
        down.type = InputEventType::KeyDown;
        up.type = InputEventType::KeyUp;
        // 注意：这里的 key_code 是平台相关的
        // Linux X11: 使用 XKeysymToKeycode 转换
        // 这里使用通用的 ASCII 方式演示
        down.key_code = 25;  // Linux X11 'W' 的 keycode (可能因系统不同而异)
        up.key_code = 25;
        controller.submitInput(down);
        std::this_thread::sleep_for(milliseconds(100));
        controller.submitInput(up);
        std::cout << "  [动作] 按键 W (前进)\n";
        break;
      }

      case 5: {
        // 模拟空格跳跃
        InputEvent down, up;
        down.type = InputEventType::KeyDown;
        up.type = InputEventType::KeyUp;
        down.key_code = 65;  // Linux X11 Space keycode
        up.key_code = 65;
        controller.submitInput(down);
        std::this_thread::sleep_for(milliseconds(50));
        controller.submitInput(up);
        std::cout << "  [动作] 按键 Space (跳跃)\n";
        break;
      }

      case 6: {
        // 鼠标拖拽（模拟视角转动）
        InputEvent e;
        e.type = InputEventType::MouseMove;
        e.x = screen_w / 2 + (action_count % 200) - 100;
        e.y = screen_h / 2;
        controller.submitInput(e);
        std::cout << "  [动作] 鼠标视角移动\n";
        break;
      }

      case 7: {
        // 组合键 Ctrl+S (保存)
        InputEvent e;
        e.type = InputEventType::KeyDown;
        e.key_code = 39;  // 's' on Linux X11
        e.mods = SystemInput::kControl;
        controller.submitInput(e);

        InputEvent up;
        up.type = InputEventType::KeyUp;
        up.key_code = 39;
        up.mods = SystemInput::kControl;
        controller.submitInput(up);
        std::cout << "  [动作] 组合键 Ctrl+S\n";
        break;
      }
    }

    action_count++;
    std::this_thread::sleep_for(milliseconds(500));  // 每500ms一个动作
  }

  std::cout << ">>> 游戏模拟结束，共执行 " << action_count << " 个动作\n";
}

// ============================================================================
// 演示：保存快照
// ============================================================================
bool saveSnapshot(const Frame& frame, const std::string& filename) {
  if (frame.rgba_data.empty()) return false;

  // 简单的 BMP 保存
#pragma pack(push, 1)
  struct BMPFileHeader {
    uint16_t bfType{0x4D42};
    uint32_t bfSize{};
    uint16_t bfReserved1{};
    uint16_t bfReserved2{};
    uint32_t bfOffBits{54};
  };
  struct BMPInfoHeader {
    uint32_t biSize{40};
    int32_t biWidth{};
    int32_t biHeight{};
    uint16_t biPlanes{1};
    uint16_t biBitCount{32};
    uint32_t biCompression{0};
    uint32_t biSizeImage{};
    int32_t biXPelsPerMeter{2835};
    int32_t biYPelsPerMeter{2835};
    uint32_t biClrUsed{};
    uint32_t biClrImportant{};
  };
#pragma pack(pop)

  std::vector<uint8_t> bgra(frame.rgba_data);
  for (size_t i = 0; i < bgra.size(); i += 4) {
    std::swap(bgra[i], bgra[i + 2]);  // RGBA -> BGRA
  }

  BMPFileHeader fh;
  BMPInfoHeader ih;
  ih.biWidth = frame.width;
  ih.biHeight = -frame.height;  // top-down
  ih.biSizeImage = static_cast<uint32_t>(bgra.size());
  fh.bfSize = fh.bfOffBits + ih.biSizeImage;

  std::ofstream f(filename, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
  f.write(reinterpret_cast<const char*>(&ih), sizeof(ih));
  f.write(reinterpret_cast<const char*>(bgra.data()), static_cast<std::streamsize>(bgra.size()));
  return f.good();
}

// ============================================================================
// 主程序
// ============================================================================
void printUsage(const char* prog) {
  std::cout << "用法: " << prog << " [目标FPS] [运行时长秒] [显示器索引]\n\n";
  std::cout << "参数:\n";
  std::cout << "  目标FPS      : 目标帧率，默认 30\n";
  std::cout << "  运行时长秒   : 运行多少秒，默认 10\n";
  std::cout << "  显示器索引   : 捕获哪个显示器，默认 0\n\n";
  std::cout << "示例:\n";
  std::cout << "  " << prog << " 60 30 0   # 60fps运行30秒，捕获主显示器\n";
  std::cout << "  " << prog << " 30 10     # 30fps运行10秒\n";
}

int main(int argc, char* argv[]) {
  std::cout << "==============================================\n";
  std::cout << "   easy_control 流式控制演示 (云游戏模拟)\n";
  std::cout << "==============================================\n\n";

  // 解析参数
  int target_fps = 30;
  int duration_sec = 10;
  int display_index = 0;

  if (argc > 1) {
    if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
      printUsage(argv[0]);
      return 0;
    }
    target_fps = std::atoi(argv[1]);
    if (target_fps <= 0 || target_fps > 120) {
      std::cerr << "错误: FPS 应在 1-120 之间\n";
      return 1;
    }
  }
  if (argc > 2) {
    duration_sec = std::atoi(argv[2]);
    if (duration_sec <= 0) {
      std::cerr << "错误: 运行时长应大于0\n";
      return 1;
    }
  }
  if (argc > 3) {
    display_index = std::atoi(argv[3]);
  }

  // 打印系统信息
  int display_count = SystemOutput::GetDisplayCount();
  std::cout << "系统信息:\n";
  std::cout << "  显示器数量: " << display_count << "\n";
  std::cout << "  捕获显示器: " << display_index << "\n";
  std::cout << "  目标帧率: " << target_fps << " fps\n";
  std::cout << "  运行时长: " << duration_sec << " 秒\n\n";

  if (display_index >= display_count) {
    std::cerr << "错误: 显示器索引超出范围 (0-" << (display_count - 1) << ")\n";
    return 1;
  }

  // 创建控制器
  StreamingController controller(target_fps, display_index);

  // 启动流式控制
  controller.start();

  // 等待一秒让系统稳定
  std::this_thread::sleep_for(seconds(1));

  // 运行游戏模拟（注意：会实际移动鼠标和按键！）
  std::cout << "\n警告: 接下来将模拟鼠标和键盘操作！\n";
  std::cout << "按 Ctrl+C 可随时中断...\n\n";

  // 简单模式：只运行截图，不模拟输入（更安全）
  bool simulate_input = false;  // 设为 true 启用输入模拟

  if (simulate_input) {
    runGameSimulation(controller, duration_sec);
  } else {
    // 仅截图模式
    std::cout << ">>> 纯截图模式运行中...\n";
    for (int i = 0; i < duration_sec; ++i) {
      std::this_thread::sleep_for(seconds(1));
      std::cout << "  运行中... " << (i + 1) << "/" << duration_sec << " 秒\n";
    }
  }

  // 停止
  controller.stop();

  // 打印统计
  controller.getStats().print();

  // 可选：保存最后一帧作为快照
  // Frame last_frame;
  // if (controller.getCurrentFrame(last_frame)) {
  //   saveSnapshot(last_frame, "last_frame.bmp");
  //   std::cout << "已保存最后一帧: last_frame.bmp\n";
  // }

  std::cout << "\n演示结束!\n";
  return 0;
}
