# Camera Wrapper

一个现代化的 C++ 包装库，提供清晰、线程安全且与供应商无关的接口，用于相机设备枚举和帧采集。目前实现了对海康机器人 MVS 相机 SDK 的支持。

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![CMake](https://img.shields.io/badge/CMake-3.17+-blue.svg)](https://cmake.org/)

[English](README.md) | [中文](README_CN.md)

## 功能特性

- **设备枚举**: 发现并枚举所有连接的相机，支持多种接口类型（GigE、USB 等）
- **多种采集模式**:
  - **SnapSync**: 同步单帧采集，支持软件或硬件触发
  - **TriggerCallback**: 事件驱动的帧采集，支持软件/硬件触发
  - **StreamCallback**: 连续流采集，基于回调的帧传递
- **线程安全 API**: 所有公共方法都是线程安全的（除非另有说明）
- **实时模式切换**: 在运行时原子性地切换采集模式
- **帧队列**: 内置帧队列，支持可配置的溢出策略（DropOldest、DropNewest）
- **传输配置**: 控制数据包大小、心跳超时和包间延迟
- **相机参数控制**: 设置和查询曝光时间、增益等相机参数
- **状态回调**: 监控连接状态变化（已连接、已断开、重新连接中）
- **跨平台支持**: 支持 Windows 和 Linux（具有特定于架构的 SDK 路径）
- **OpenCV 集成**: 帧以 OpenCV Mat 对象形式传递，便于图像处理

## 项目结构

```
camera_wrapper/
├── wrapper/                          # 核心库
│   ├── include/camera_wrapper/       # 公共 API 头文件
│   │   ├── camera.h                  # 抽象相机接口
│   │   ├── camera_enumerator.h       # 设备枚举接口
│   │   ├── device_info.h             # 设备信息结构
│   │   ├── grab_config.h             # 采集模式配置
│   │   ├── image_frame.h             # 帧数据结构
│   │   ├── frame_queue.h             # 线程安全帧队列
│   │   ├── frame_callback_manager.h  # 帧回调管理
│   │   ├── status_callback_manager.h # 状态事件回调
│   │   ├── transport_config.h        # 传输层配置
│   │   ├── enumerator_factory.h      # 枚举器工厂
│   │   └── export.h                  # 符号导出宏
│   └── src/                          # 实现
│       ├── hik_camera.h/cpp          # 海康相机实现
│       ├── hik_enumerator.h/cpp      # 海康设备枚举器
│       ├── device_info.cpp           # 设备信息实现
│       ├── frame_queue.cpp           # 帧队列实现
│       ├── frame_callback_manager.cpp
│       ├── status_callback_manager.cpp
│       └── enumerator_factory.cpp
├── demos/
│   ├── cpp_demo/                     # C++ 控制台演示
│   │   ├── main.cpp                  # 演示所有采集模式
│   │   └── CMakeLists.txt
│   └── qt_demo/                      # Qt GUI 演示
│       ├── main.cpp
│       ├── main_window.h/cpp
│       ├── camera_bridge.h/cpp       # Qt/C++ 桥接
│       ├── main_window.ui
│       └── CMakeLists.txt
├── CMakeLists.txt                    # 根 CMake 配置
└── wrapper/CMakeLists.txt            # 库 CMake 配置
```

## 系统要求

### 依赖项

- **C++17** 或更高版本
- **CMake 3.17** 或更高版本
- **OpenCV** （用于图像处理）
- **海康机器人 MVS SDK** （用于相机硬件支持）

### 系统要求

- **Windows**: Visual Studio 2017+ 或 MinGW，支持 C++17
- **Linux**: GCC 7+ 或 Clang 5+，支持 C++17

## 安装

### 1. 安装海康机器人 MVS SDK

从[官方网站](https://www.hikrobotics.com/en)下载并安装海康机器人 MVS SDK。

安装程序将设置 `MVCAM_COMMON_RUNENV` 环境变量，指向 SDK 根目录。

### 2. 安装 OpenCV

**Windows (vcpkg)**:
```bash
vcpkg install opencv:x64-windows
```

**Ubuntu/Debian**:
```bash
sudo apt-get install libopencv-dev
```

**macOS (Homebrew)**:
```bash
brew install opencv
```

### 3. 构建库

```bash
# 创建构建目录
mkdir build && cd build

# 使用 CMake 配置
cmake .. -DCMAKE_BUILD_TYPE=Release

# 构建
cmake --build . --config Release

# （可选）安装
cmake --install .
```

### 自定义 SDK 路径

如果海康 SDK 不在默认位置，请显式指定：

```bash
cmake .. -DHIKCAM_SDK_DIR=/path/to/hikrobot/sdk
```

## 作为第三方库集成

### 使用 CMake FetchContent

在你的 `CMakeLists.txt` 中添加：

```cmake
include(FetchContent)

FetchContent_Declare(
    camera_wrapper
    GIT_REPOSITORY https://github.com/yourusername/camera_wrapper.git
    GIT_TAG v1.0.0
)

FetchContent_MakeAvailable(camera_wrapper)

# 链接到你的目标
target_link_libraries(your_target PRIVATE CameraWrapper)
```

### 使用 CMake add_subdirectory

如果你的项目中有 camera_wrapper 源代码：

```cmake
add_subdirectory(path/to/camera_wrapper)
target_link_libraries(your_target PRIVATE CameraWrapper)
```

### 使用已安装的包

安装库后：

```cmake
find_package(CameraWrapper REQUIRED)
target_link_libraries(your_target PRIVATE CameraWrapper::CameraWrapper)
```

### 包含头文件

```cpp
#include "camera_wrapper/camera.h"
#include "camera_wrapper/camera_enumerator.h"
#include "camera_wrapper/enumerator_factory.h"
```

## 使用示例

### 基础示例：设备枚举和帧采集

```cpp
#include "camera_wrapper/camera.h"
#include "camera_wrapper/camera_enumerator.h"
#include "camera_wrapper/enumerator_factory.h"
#include "camera_wrapper/grab_config.h"

using namespace camera_wrapper;

int main() {
    // 创建枚举器
    auto enumerator = createEnumerator();
    
    // 枚举所有设备
    auto devices = enumerator->enumerate();
    if (devices.empty()) {
        std::cerr << "未找到相机\n";
        return 1;
    }
    
    // 创建并打开相机
    auto cam = enumerator->createCamera(devices[0]);
    if (!cam->open()) {
        std::cerr << "打开相机失败\n";
        return 1;
    }
    
    // 配置同步单帧采集
    GrabConfig cfg;
    cfg.mode = GrabMode::SnapSync;
    cfg.triggerSource = TriggerSource::Software;
    cfg.snapTimeoutMs = 3000;
    
    if (!cam->switchMode(cfg)) {
        std::cerr << "配置相机失败\n";
        return 1;
    }
    
    // 采集一帧
    auto frame = cam->snapSync();
    if (frame) {
        std::cout << "采集帧: " << frame->width << "x" << frame->height << "\n";
        cv::imwrite("frame.png", frame->image);
    }
    
    cam->close();
    return 0;
}
```

### 连续帧流采集与回调

```cpp
#include "camera_wrapper/frame_queue.h"
#include <thread>

// 创建帧队列
FrameQueue queue(16, OverflowPolicy::DropOldest);

// 注册帧回调
auto cbId = cam->registerFrameCallback([&queue](const ImageFrame& frame) {
    queue.push(frame);  // 浅拷贝 - 非常快
});

// 配置连续流采集
GrabConfig cfg;
cfg.mode = GrabMode::StreamCallback;
cam->switchMode(cfg);

// 消费者线程
std::thread consumer([&queue]() {
    while (auto frame = queue.pop(1000)) {  // 1 秒超时
        std::cout << "处理帧 " << frame->frameId << "\n";
        // 处理帧...
    }
});

// 运行一段时间
std::this_thread::sleep_for(std::chrono::seconds(10));

queue.stop();
consumer.join();

cam->unregisterFrameCallback(cbId);
```

### 触发帧采集

```cpp
// 配置触发采集
GrabConfig cfg;
cfg.mode = GrabMode::TriggerCallback;
cfg.triggerSource = TriggerSource::Software;
cam->switchMode(cfg);

// 注册回调
auto cbId = cam->registerFrameCallback([](const ImageFrame& frame) {
    std::cout << "收到触发帧: " << frame->frameId << "\n";
});

// 发送软件触发
for (int i = 0; i < 10; ++i) {
    cam->sendSoftTrigger();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

cam->unregisterFrameCallback(cbId);
```

### 监控连接状态

```cpp
auto statusId = cam->registerStatusCallback([](const StatusEvent& ev) {
    switch (ev.status) {
        case StatusCode::Connected:
            std::cout << "相机已连接\n";
            break;
        case StatusCode::Disconnected:
            std::cout << "相机已断开\n";
            break;
        case StatusCode::Reconnecting:
            std::cout << "正在重新连接（重试 " << ev.retryCount << "）\n";
            break;
        case StatusCode::Reconnected:
            std::cout << "相机已重新连接\n";
            break;
        case StatusCode::Error:
            std::cout << "错误: " << ev.message << "\n";
            break;
    }
});

// ... 相机操作 ...

cam->unregisterStatusCallback(statusId);
```

## API 概览

### 核心类

#### `CameraEnumerator`
发现并创建相机对象。

```cpp
std::vector<DeviceInfo> enumerate();
std::vector<DeviceInfo> enumerateByType(InterfaceType type);
std::optional<DeviceInfo> findBySerial(const std::string& serial);
std::unique_ptr<Camera> createCamera(const DeviceInfo& info);
```

#### `Camera`
相机控制和帧采集的主接口。

**生命周期**:
- `bool open()` - 打开相机设备
- `bool close()` - 关闭相机设备
- `bool isOpened() const` - 检查相机是否打开

**配置**:
- `bool applyTransportConfig(const TransportConfig& cfg)` - 配置传输层
- `bool setExposureTime(double us)` - 设置曝光时间（微秒）
- `bool setGain(double dB)` - 设置增益（dB）
- `bool configure(const GrabConfig& cfg)` - 配置采集模式

**采集**:
- `bool startGrabbing()` - 开始帧采集
- `bool stopGrabbing()` - 停止帧采集
- `bool isGrabbing() const` - 检查是否正在采集
- `bool switchMode(const GrabConfig& cfg)` - 原子性切换采集模式
- `std::optional<ImageFrame> snapSync()` - 同步单帧采集

**回调**:
- `FrameCallbackManager::CallbackId registerFrameCallback(Callback cb)` - 注册帧回调
- `void unregisterFrameCallback(CallbackId id)` - 注销帧回调
- `StatusCallbackManager::CallbackId registerStatusCallback(Callback cb)` - 注册状态回调
- `void unregisterStatusCallback(CallbackId id)` - 注销状态回调

#### `FrameQueue`
用于缓冲帧的线程安全队列。

```cpp
FrameQueue(size_t capacity, OverflowPolicy policy);
void push(const ImageFrame& frame);
std::optional<ImageFrame> pop(int timeoutMs);
void clear();
void stop();
size_t size() const;
FrameQueueStats stats() const;
```

### 数据结构

#### `DeviceInfo`
包含设备标识和元数据。

```cpp
std::string serialNumber;
std::string modelName;
std::string displayName();
InterfaceType interfaceType;
```

#### `ImageFrame`
表示采集的帧。

```cpp
cv::Mat image;              // OpenCV Mat (BGR 或 Mono8)
uint64_t frameId;           // 帧序列号
uint64_t timestampNs;       // 时间戳（纳秒）
uint32_t width, height;     // 图像尺寸
uint32_t lostPackets;       // 丢失的数据包数（GigE）
```

#### `GrabConfig`
采集模式配置。

```cpp
GrabMode mode;              // SnapSync、TriggerCallback、StreamCallback
TriggerSource triggerSource; // 软件或硬件
int snapTimeoutMs;          // SnapSync 模式超时
int snapRetryCount;         // SnapSync 重试次数
```

#### `TransportConfig`
传输层参数。

```cpp
struct GigEConfig {
    uint32_t packetSize;           // 0 = 自动检测
    uint32_t heartbeatTimeoutMs;
    uint32_t interPacketDelayNs;
};
GigEConfig gige;
```

## 示例

### C++ 控制台演示

`demos/cpp_demo/` 目录包含一个全面的示例，演示：
- 设备枚举
- SnapSync 模式（同步单帧采集）
- TriggerCallback 模式（事件驱动采集）
- StreamCallback 模式（连续流采集）
- 实时模式切换
- 帧队列统计
- 优雅关闭

构建和运行：
```bash
cd build
cmake --build . --target cpp_demo
./demos/cpp_demo/cpp_demo  # 或 Windows 上的 cpp_demo.exe
```

### Qt GUI 演示

`demos/qt_demo/` 目录包含一个基于 Qt 的 GUI 应用程序，用于交互式相机控制和实时帧显示。*注意：Qt 演示采用 LGPL v3 许可证。*

**功能特性**:
- 设备枚举和选择
- 实时相机参数调整（曝光、增益）
- 使用 OpenCV 渲染的实时帧预览
- 多种采集模式支持（SnapSync、TriggerCallback、StreamCallback）
- 帧统计和性能监控
- 连接状态监控
- 优雅的错误处理和用户反馈

**系统要求**:
- Qt 5.15+ 或 Qt 6.0+
- 支持 Qt 的 OpenCV

构建和运行：
```bash
cd build
cmake --build . --target qt_demo
./demos/qt_demo/qt_demo  # 或 Windows 上的 qt_demo.exe
```

GUI 提供直观的界面用于：
- 从枚举的设备列表中选择相机
- 配置传输参数
- 实时切换采集模式
- 查看实时帧流和帧率信息
- 监控连接状态和错误消息

## 自定义构建配置

### 仅构建库（不构建演示）

```bash
cmake .. -DBUILD_DEMOS=OFF
cmake --build .
```

### 构建特定演示

```bash
cmake .. -DBUILD_CPP_DEMO=ON -DBUILD_QT_DEMO=OFF
cmake --build .
```

### 调试构建

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
```

## 线程安全

`Camera` 类的所有公共方法都是线程安全的（除非另有说明）。库使用内部同步确保：

- 来自 SDK 线程的并发帧回调
- 从任何线程安全地切换模式
- 线程安全的帧队列操作
- 原子连接状态更新

**注意**: 帧回调从 SDK 的内部回调线程调用，必须快速返回（微秒级）。推荐的模式是将帧浅拷贝到 `FrameQueue` 中，然后在单独的消费者线程中处理。

## 性能考虑

1. **帧回调**: 保持回调快速；使用 `FrameQueue` 进行缓冲
2. **队列溢出**: 选择适当的 `OverflowPolicy`:
   - `DropOldest`: 保留最新帧（流采集）
   - `DropNewest`: 按顺序保留所有帧（触发采集）
3. **传输配置**: 根据网络调整数据包大小和心跳
4. **曝光和增益**: 根据光照条件调整以最小化处理开销

## 故障排除

### "未找到海康机器人 MVS SDK"

确保 SDK 已安装且 `MVCAM_COMMON_RUNENV` 已设置：

```bash
# Windows
echo %MVCAM_COMMON_RUNENV%

# Linux
echo $MVCAM_COMMON_RUNENV
```

如果未设置，请显式指定 SDK 路径：
```bash
cmake .. -DHIKCAM_SDK_DIR=/path/to/sdk
```

### "未找到相机"

1. 检查物理连接
2. 验证相机电源和网络连接
3. 使用海康官方 MVS 软件测试
4. 检查防火墙设置（GigE 相机）

### 帧丢失或超时

1. 增加 `FrameQueue` 容量
2. 优化帧回调处理时间
3. 调整传输配置（数据包大小、心跳）
4. 检查网络带宽和延迟

## 贡献

欢迎贡献！请：

1. Fork 本仓库
2. 创建功能分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m 'Add amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 开启 Pull Request

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件。

## 致谢

- 海康机器人提供的 MVS SDK
- OpenCV 图像处理库
- Qt GUI 框架

## 更新日志

### 版本 1.0.0 (初始发布)

- 支持多种接口类型的设备枚举
- 三种采集模式：SnapSync、TriggerCallback、StreamCallback
- 具有可配置溢出策略的线程安全帧队列
- 实时模式切换
- 传输层配置
- 相机参数控制（曝光、增益）
- 状态事件回调
- 跨平台支持（Windows、Linux）
- 完整的 C++ 和 Qt 演示
