# Camera Wrapper

A modern C++ wrapper library providing a clean, thread-safe, and vendor-agnostic interface for camera device enumeration and frame acquisition. Currently implements support for Hikrobot MVS camera SDK.

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![CMake](https://img.shields.io/badge/CMake-3.17+-blue.svg)](https://cmake.org/)

[English](README.md) | [中文](README_CN.md)

## Features

- **Device Enumeration**: Discover and enumerate all connected cameras across multiple interface types (GigE, USB, etc.)
- **Multiple Acquisition Modes**:
  - **SnapSync**: Synchronous single-frame capture with software or hardware triggers
  - **TriggerCallback**: Event-driven frame acquisition with software/hardware triggers
  - **StreamCallback**: Continuous frame streaming with callback-based delivery
- **Thread-Safe API**: All public methods are thread-safe unless explicitly noted
- **Live Mode Switching**: Atomically switch between acquisition modes at runtime
- **Frame Queuing**: Built-in frame queue with configurable overflow policies (DropOldest, DropNewest)
- **Transport Configuration**: Control packet size, heartbeat timeout, and inter-packet delays
- **Camera Parameter Control**: Set and query exposure time, gain, and other camera parameters
- **Status Callbacks**: Monitor connection state changes (connected, disconnected, reconnecting)
- **Cross-Platform**: Supports Windows and Linux (with architecture-specific SDK paths)
- **OpenCV Integration**: Frames are delivered as OpenCV Mat objects for easy image processing

## Project Structure

```
camera_wrapper/
├── wrapper/                          # Core library
│   ├── include/camera_wrapper/       # Public API headers
│   │   ├── camera.h                  # Abstract camera interface
│   │   ├── camera_enumerator.h       # Device enumeration interface
│   │   ├── device_info.h             # Device information structures
│   │   ├── grab_config.h             # Acquisition mode configuration
│   │   ├── image_frame.h             # Frame data structure
│   │   ├── frame_queue.h             # Thread-safe frame queue
│   │   ├── frame_callback_manager.h  # Frame callback management
│   │   ├── status_callback_manager.h # Status event callbacks
│   │   ├── transport_config.h        # Transport layer configuration
│   │   ├── enumerator_factory.h      # Factory for creating enumerators
│   │   └── export.h                  # Symbol export macros
│   └── src/                          # Implementation
│       ├── hik_camera.h/cpp          # Hikrobot camera implementation
│       ├── hik_enumerator.h/cpp      # Hikrobot device enumerator
│       ├── device_info.cpp           # Device info implementation
│       ├── frame_queue.cpp           # Frame queue implementation
│       ├── frame_callback_manager.cpp
│       ├── status_callback_manager.cpp
│       └── enumerator_factory.cpp
├── demos/
│   ├── cpp_demo/                     # C++ console demo
│   │   ├── main.cpp                  # Demonstrates all acquisition modes
│   │   └── CMakeLists.txt
│   └── qt_demo/                      # Qt GUI demo
│       ├── main.cpp
│       ├── main_window.h/cpp
│       ├── camera_bridge.h/cpp       # Qt/C++ bridge
│       ├── main_window.ui
│       └── CMakeLists.txt
├── CMakeLists.txt                    # Root CMake configuration
└── wrapper/CMakeLists.txt            # Library CMake configuration
```

## Requirements

### Dependencies

- **C++17** or later
- **CMake 3.17** or later
- **OpenCV** (for image processing)
- **Hikrobot MVS SDK** (for camera hardware support)

### System Requirements

- **Windows**: Visual Studio 2017+ or MinGW with C++17 support
- **Linux**: GCC 7+ or Clang 5+ with C++17 support

## Installation

### 1. Install Hikrobot MVS SDK

Download and install the Hikrobot MVS SDK from the [official website](https://www.hikrobotics.com/en).

The installer will set the `MVCAM_COMMON_RUNENV` environment variable pointing to the SDK root directory.

### 2. Install OpenCV

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

### 3. Build the Library

```bash
# Create build directory
mkdir build && cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --config Release

# (Optional) Install
cmake --install .
```

### Custom SDK Path

If the Hikrobot SDK is not in the default location, specify it explicitly:

```bash
cmake .. -DHIKCAM_SDK_DIR=/path/to/hikrobot/sdk
```

## Integration as a Third-Party Library

### Using CMake FetchContent

Add to your `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
    camera_wrapper
    GIT_REPOSITORY https://github.com/yourusername/camera_wrapper.git
    GIT_TAG v1.0.0
)

FetchContent_MakeAvailable(camera_wrapper)

# Link to your target
target_link_libraries(your_target PRIVATE CameraWrapper)
```

### Using CMake add_subdirectory

If you have the camera_wrapper source in your project:

```cmake
add_subdirectory(path/to/camera_wrapper)
target_link_libraries(your_target PRIVATE CameraWrapper)
```

### Using Installed Package

After installing the library:

```cmake
find_package(CameraWrapper REQUIRED)
target_link_libraries(your_target PRIVATE CameraWrapper::CameraWrapper)
```

### Include Headers

```cpp
#include "camera_wrapper/camera.h"
#include "camera_wrapper/camera_enumerator.h"
#include "camera_wrapper/enumerator_factory.h"
```

## Usage

### Basic Example: Device Enumeration and Frame Capture

```cpp
#include "camera_wrapper/camera.h"
#include "camera_wrapper/camera_enumerator.h"
#include "camera_wrapper/enumerator_factory.h"
#include "camera_wrapper/grab_config.h"

using namespace camera_wrapper;

int main() {
    // Create enumerator
    auto enumerator = createEnumerator();
    
    // Enumerate all devices
    auto devices = enumerator->enumerate();
    if (devices.empty()) {
        std::cerr << "No cameras found\n";
        return 1;
    }
    
    // Create and open camera
    auto cam = enumerator->createCamera(devices[0]);
    if (!cam->open()) {
        std::cerr << "Failed to open camera\n";
        return 1;
    }
    
    // Configure for synchronous single-frame capture
    GrabConfig cfg;
    cfg.mode = GrabMode::SnapSync;
    cfg.triggerSource = TriggerSource::Software;
    cfg.snapTimeoutMs = 3000;
    
    if (!cam->switchMode(cfg)) {
        std::cerr << "Failed to configure camera\n";
        return 1;
    }
    
    // Capture a frame
    auto frame = cam->snapSync();
    if (frame) {
        std::cout << "Captured frame: " << frame->width << "x" << frame->height << "\n";
        cv::imwrite("frame.png", frame->image);
    }
    
    cam->close();
    return 0;
}
```

### Continuous Frame Streaming with Callbacks

```cpp
#include "camera_wrapper/frame_queue.h"
#include <thread>

// Create a frame queue
FrameQueue queue(16, OverflowPolicy::DropOldest);

// Register frame callback
auto cbId = cam->registerFrameCallback([&queue](const ImageFrame& frame) {
    queue.push(frame);  // Shallow copy - very fast
});

// Configure for continuous streaming
GrabConfig cfg;
cfg.mode = GrabMode::StreamCallback;
cam->switchMode(cfg);

// Consumer thread
std::thread consumer([&queue]() {
    while (auto frame = queue.pop(1000)) {  // 1 second timeout
        std::cout << "Processing frame " << frame->frameId << "\n";
        // Process frame...
    }
});

// Let it run for a while
std::this_thread::sleep_for(std::chrono::seconds(10));

queue.stop();
consumer.join();

cam->unregisterFrameCallback(cbId);
```

### Triggered Frame Acquisition

```cpp
// Configure for triggered acquisition
GrabConfig cfg;
cfg.mode = GrabMode::TriggerCallback;
cfg.triggerSource = TriggerSource::Software;
cam->switchMode(cfg);

// Register callback
auto cbId = cam->registerFrameCallback([](const ImageFrame& frame) {
    std::cout << "Triggered frame received: " << frame->frameId << "\n";
});

// Send software triggers
for (int i = 0; i < 10; ++i) {
    cam->sendSoftTrigger();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

cam->unregisterFrameCallback(cbId);
```

### Monitor Connection Status

```cpp
auto statusId = cam->registerStatusCallback([](const StatusEvent& ev) {
    switch (ev.status) {
        case StatusCode::Connected:
            std::cout << "Camera connected\n";
            break;
        case StatusCode::Disconnected:
            std::cout << "Camera disconnected\n";
            break;
        case StatusCode::Reconnecting:
            std::cout << "Attempting to reconnect (retry " << ev.retryCount << ")\n";
            break;
        case StatusCode::Reconnected:
            std::cout << "Camera reconnected\n";
            break;
        case StatusCode::Error:
            std::cout << "Error: " << ev.message << "\n";
            break;
    }
});

// ... camera operations ...

cam->unregisterStatusCallback(statusId);
```

## API Overview

### Core Classes

#### `CameraEnumerator`
Discovers and creates camera objects.

```cpp
std::vector<DeviceInfo> enumerate();
std::vector<DeviceInfo> enumerateByType(InterfaceType type);
std::optional<DeviceInfo> findBySerial(const std::string& serial);
std::unique_ptr<Camera> createCamera(const DeviceInfo& info);
```

#### `Camera`
Main interface for camera control and frame acquisition.

**Lifecycle**:
- `bool open()` - Open camera device
- `bool close()` - Close camera device
- `bool isOpened() const` - Check if camera is open

**Configuration**:
- `bool applyTransportConfig(const TransportConfig& cfg)` - Configure transport layer
- `bool setExposureTime(double us)` - Set exposure time in microseconds
- `bool setGain(double dB)` - Set gain in dB
- `bool configure(const GrabConfig& cfg)` - Configure grab mode

**Acquisition**:
- `bool startGrabbing()` - Start frame acquisition
- `bool stopGrabbing()` - Stop frame acquisition
- `bool isGrabbing() const` - Check if currently grabbing
- `bool switchMode(const GrabConfig& cfg)` - Atomically switch acquisition mode
- `std::optional<ImageFrame> snapSync()` - Synchronous single-frame capture

**Callbacks**:
- `FrameCallbackManager::CallbackId registerFrameCallback(Callback cb)` - Register frame callback
- `void unregisterFrameCallback(CallbackId id)` - Unregister frame callback
- `StatusCallbackManager::CallbackId registerStatusCallback(Callback cb)` - Register status callback
- `void unregisterStatusCallback(CallbackId id)` - Unregister status callback

#### `FrameQueue`
Thread-safe queue for buffering frames.

```cpp
FrameQueue(size_t capacity, OverflowPolicy policy);
void push(const ImageFrame& frame);
std::optional<ImageFrame> pop(int timeoutMs);
void clear();
void stop();
size_t size() const;
FrameQueueStats stats() const;
```

### Data Structures

#### `DeviceInfo`
Contains device identification and metadata.

```cpp
std::string serialNumber;
std::string modelName;
std::string displayName();
InterfaceType interfaceType;
```

#### `ImageFrame`
Represents a captured frame.

```cpp
cv::Mat image;              // OpenCV Mat (BGR or Mono8)
uint64_t frameId;           // Frame sequence number
uint64_t timestampNs;       // Timestamp in nanoseconds
uint32_t width, height;     // Image dimensions
uint32_t lostPackets;       // Number of lost packets (GigE)
```

#### `GrabConfig`
Acquisition mode configuration.

```cpp
GrabMode mode;              // SnapSync, TriggerCallback, StreamCallback
TriggerSource triggerSource; // Software or Hardware
int snapTimeoutMs;          // Timeout for SnapSync mode
int snapRetryCount;         // Retry count for SnapSync
```

#### `TransportConfig`
Transport layer parameters.

```cpp
struct GigEConfig {
    uint32_t packetSize;           // 0 = auto-detect
    uint32_t heartbeatTimeoutMs;
    uint32_t interPacketDelayNs;
};
GigEConfig gige;
```

## Examples

### C++ Console Demo

The `demos/cpp_demo/` directory contains a comprehensive example demonstrating:
- Device enumeration
- SnapSync mode (synchronous single-frame capture)
- TriggerCallback mode (event-driven acquisition)
- StreamCallback mode (continuous streaming)
- Live mode switching
- Frame queue statistics
- Graceful shutdown

Build and run:
```bash
cd build
cmake --build . --target cpp_demo
./demos/cpp_demo/cpp_demo  # or cpp_demo.exe on Windows
```

### Qt GUI Demo

The `demos/qt_demo/` directory contains a Qt-based GUI application for interactive camera control and real-time frame display. *Note: The Qt demo is licensed under the LGPL v3 License.*

**Features:**
- Device enumeration and selection
- Real-time camera parameter adjustment (exposure, gain)
- Live frame preview with OpenCV rendering
- Multiple acquisition mode support (SnapSync, TriggerCallback, StreamCallback)
- Frame statistics and performance monitoring
- Connection status monitoring
- Graceful error handling and user feedback

**Requirements:**
- Qt 5.15+ or Qt 6.0+
- OpenCV with Qt support

Build and run:
```bash
cd build
cmake --build . --target qt_demo
./demos/qt_demo/qt_demo  # or qt_demo.exe on Windows
```

The GUI provides an intuitive interface for:
- Selecting cameras from the enumerated device list
- Configuring transport parameters
- Switching between acquisition modes in real-time
- Viewing live frame stream with frame rate information
- Monitoring connection state and error messages

## Building with Custom Configurations

### Build Only the Library (No Demos)

```bash
cmake .. -DBUILD_DEMOS=OFF
cmake --build .
```

### Build Specific Demos

```bash
cmake .. -DBUILD_CPP_DEMO=ON -DBUILD_QT_DEMO=OFF
cmake --build .
```

### Debug Build

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
```

## Thread Safety

All public methods of the `Camera` class are thread-safe unless explicitly noted otherwise. The library uses internal synchronization to ensure:

- Concurrent frame callbacks from the SDK thread
- Safe mode switching from any thread
- Thread-safe frame queue operations
- Atomic connection state updates

**Note**: Frame callbacks are invoked from the SDK's internal callback thread and must return quickly (microseconds). The recommended pattern is to shallow-copy frames into a `FrameQueue` and process them in a separate consumer thread.

## Performance Considerations

1. **Frame Callbacks**: Keep callbacks fast; use `FrameQueue` for buffering
2. **Queue Overflow**: Choose appropriate `OverflowPolicy`:
   - `DropOldest`: Keep most recent frames (streaming)
   - `DropNewest`: Keep all frames in order (triggered acquisition)
3. **Transport Configuration**: Tune packet size and heartbeat for your network
4. **Exposure & Gain**: Adjust based on lighting conditions to minimize processing overhead

## Troubleshooting

### "Hikrobot MVS SDK not found"

Ensure the SDK is installed and `MVCAM_COMMON_RUNENV` is set:

```bash
# Windows
echo %MVCAM_COMMON_RUNENV%

# Linux
echo $MVCAM_COMMON_RUNENV
```

If not set, specify the SDK path explicitly:
```bash
cmake .. -DHIKCAM_SDK_DIR=/path/to/sdk
```

### "No cameras found"

1. Check physical connections
2. Verify camera power and network connectivity
3. Test with Hikrobot's official MVS software
4. Check firewall settings (for GigE cameras)

### Frame Drops or Timeouts

1. Increase `FrameQueue` capacity
2. Optimize frame callback processing time
3. Adjust transport configuration (packet size, heartbeat)
4. Check network bandwidth and latency

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Hikrobot for the MVS SDK
- OpenCV for image processing capabilities
- Qt for the GUI framework

## Changelog

### Version 1.0.0 (Initial Release)

- Device enumeration across multiple interface types
- Three acquisition modes: SnapSync, TriggerCallback, StreamCallback
- Thread-safe frame queue with configurable overflow policies
- Live mode switching
- Transport layer configuration
- Camera parameter control (exposure, gain)
- Status event callbacks
- Cross-platform support (Windows, Linux)
- Comprehensive C++ and Qt demos
