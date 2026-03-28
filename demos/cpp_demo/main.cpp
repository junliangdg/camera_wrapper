/**
 * camera_wrapper C++ Console Demo
 *
 * Demonstrates the complete usage of the camera_wrapper library:
 *   1. Device enumeration
 *   2. Open + transport configuration
 *   3. Mode 1 – SnapSync: synchronous single-frame grab
 *   4. Mode 2 – TriggerCallback: callback → queue → independent consumer thread
 *   5. Mode 3 – StreamCallback: continuous stream → queue → consumer thread
 *   6. Live mode switching (Stream → Trigger → Stream)
 *   7. Queue statistics
 *   8. Graceful shutdown
 *
 * The recommended consumption pattern (callback → queue → consumer thread)
 * is demonstrated in demoTriggerCallback() and demoStreamCallback().
 */

#include "camera_wrapper/camera.h"
#include "camera_wrapper/camera_enumerator.h"
#include "camera_wrapper/enumerator_factory.h"
#include "camera_wrapper/frame_queue.h"
#include "camera_wrapper/image_frame.h"
#include "camera_wrapper/status_callback_manager.h"
#include "camera_wrapper/transport_config.h"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <thread>

using namespace camera_wrapper;
using namespace std::chrono_literals;

// ======================================================================== //
//  Helpers                                                                   //
// ======================================================================== //

static void printDeviceList(const std::vector<DeviceInfo>& devices) {
    if (devices.empty()) {
        std::cout << "  (no devices found)\n";
        return;
    }
    for (std::size_t i = 0; i < devices.size(); ++i)
        std::cout << "  [" << i << "] " << devices[i].displayName() << '\n';
}

static void printFrameInfo(const ImageFrame& f) {
    std::cout << "  Frame #" << f.frameId << "  size=" << f.width << 'x' << f.height
              << "  ts=" << f.timestampNs << "  lostPkts=" << f.lostPackets << '\n';
}

static void printQueueStats(const FrameQueueStats& s, const std::string& label) {
    std::cout << "[Queue:" << label << "]"
              << "  pushed=" << s.totalPushed << "  popped=" << s.totalPopped
              << "  dropped=" << s.dropCount << '\n';
}

// ======================================================================== //
//  Demo 1 – SnapSync                                                         //
// ======================================================================== //

static void demoSnapSync(Camera& cam) {
    std::cout << "\n=== Demo 1: SnapSync ===\n";

    GrabConfig cfg;
    cfg.mode = GrabMode::SnapSync;
    cfg.triggerSource = TriggerSource::Software;
    cfg.snapTimeoutMs = 3000;
    cfg.snapRetryCount = 3;

    if (! cam.switchMode(cfg)) {
        std::cerr << "switchMode(SnapSync) failed\n";
        return;
    }

    std::cout << "Grabbing one frame synchronously...\n";
    auto frame = cam.snapSync();
    if (! frame) {
        std::cerr << "snapSync() returned no frame\n";
        return;
    }

    printFrameInfo(*frame);

    // Save to disk.
    if (! frame->image.empty()) {
        cv::imwrite("snap_sync_frame.png", frame->image);
        std::cout << "  Saved to snap_sync_frame.png\n";
    }
}

// ======================================================================== //
//  Demo 2 – TriggerCallback                                                  //
// ======================================================================== //

static void demoTriggerCallback(Camera& cam) {
    std::cout << "\n=== Demo 2: TriggerCallback (callback -> queue -> consumer thread) ===\n";

    // Create a frame queue with DropNewest policy (every triggered frame must
    // be processed in order – don't discard any).
    FrameQueue queue(16, OverflowPolicy::DropNewest);

    // Register a frame callback that simply pushes frames into the queue.
    // This lambda runs on the SDK callback thread – it must be fast.
    auto cbId = cam.registerFrameCallback([&queue](const ImageFrame& frame) {
        queue.push(frame); // shallow copy – O(1)
    });

    GrabConfig cfg;
    cfg.mode = GrabMode::TriggerCallback;
    cfg.triggerSource = TriggerSource::Software;

    if (! cam.switchMode(cfg)) {
        std::cerr << "switchMode(TriggerCallback) failed\n";
        cam.unregisterFrameCallback(cbId);
        return;
    }

    // Consumer thread: pops frames from the queue and processes them.
    constexpr int kFramesToProcess = 5;
    std::atomic<int> processed{0};

    std::thread consumer([&] {
        while (processed.load() < kFramesToProcess) {
            auto frame = queue.pop(-1);
            if (! frame) {
                std::cerr << "  Consumer: pop() timed out\n";
                break;
            }
            ++processed;
            std::cout << "  Consumer processed ";
            printFrameInfo(*frame);
        }
    });

    // Producer: send software triggers from the main thread.
    for (int i = 0; i < kFramesToProcess; ++i) {
        std::cout << "  Sending soft trigger " << (i + 1) << "/" << kFramesToProcess << '\n';
        cam.sendSoftTrigger();
        std::this_thread::sleep_for(200ms);
    }

    queue.stop(); // unblock consumer
    consumer.join();

    printQueueStats(queue.stats(), "trigger");

    cam.unregisterFrameCallback(cbId);
}

// ======================================================================== //
//  Demo 3 – StreamCallback                                                   //
// ======================================================================== //

static void demoStreamCallback(Camera& cam) {
    std::cout << "\n=== Demo 3: StreamCallback (continuous stream for 3 seconds) ===\n";

    // DropOldest: always keep the most recent frames; discard stale ones.
    FrameQueue queue(8, OverflowPolicy::DropOldest);

    auto cbId = cam.registerFrameCallback([&queue](const ImageFrame& frame) { queue.push(frame); });

    GrabConfig cfg;
    cfg.mode = GrabMode::StreamCallback;

    if (! cam.switchMode(cfg)) {
        std::cerr << "switchMode(StreamCallback) failed\n";
        cam.unregisterFrameCallback(cbId);
        return;
    }

    // Consumer thread: process frames for 3 seconds.
    std::atomic<bool> stopConsumer{false};
    std::atomic<uint64_t> frameCount{0};
    uint64_t firstFrameId = 0;
    uint64_t lastFrameId = 0;

    std::thread consumer([&] {
        while (! stopConsumer.load()) {
            auto frame = queue.pop(0);
            continue;

            if (frameCount.load() == 0)
                firstFrameId = frame->frameId;
            lastFrameId = frame->frameId;
            ++frameCount;
        }
        // Drain remaining frames.
        while (auto frame = queue.pop(0)) {
            lastFrameId = frame->frameId;
            ++frameCount;
        }
    });

    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(3s);

    stopConsumer.store(true);
    consumer.join();

    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    uint64_t fc = frameCount.load();
    std::cout << "  Elapsed: " << std::fixed << std::setprecision(2) << elapsed << " s\n"
              << "  Frames consumed: " << fc << '\n'
              << "  Effective FPS: " << (fc / elapsed) << '\n'
              << "  Frame IDs: " << firstFrameId << " -> " << lastFrameId << '\n';

    printQueueStats(queue.stats(), "stream");

    cam.unregisterFrameCallback(cbId);
}

// ======================================================================== //
//  Demo 4 – Live mode switching                                              //
// ======================================================================== //

static void demoModeSwitching(Camera& cam) {
    std::cout << "\n=== Demo 4: Live mode switching (Stream -> Trigger -> Stream) ===\n";

    // Shared queue used across mode switches.
    FrameQueue queue(8, OverflowPolicy::DropOldest);

    auto cbId = cam.registerFrameCallback([&queue](const ImageFrame& frame) { queue.push(frame); });

    // --- Phase 1: StreamCallback for 1 second ---
    std::cout << "  Phase 1: StreamCallback (1 s)\n";
    cam.switchMode({GrabMode::StreamCallback});
    std::this_thread::sleep_for(1s);
    std::cout << "  Queue size after stream phase: " << queue.size() << '\n';

    // --- Phase 2: Switch to TriggerCallback ---
    std::cout << "  Phase 2: TriggerCallback (5 soft triggers)\n";
    queue.clear();
    GrabConfig trigCfg;
    trigCfg.mode = GrabMode::TriggerCallback;
    trigCfg.triggerSource = TriggerSource::Software;
    cam.switchMode(trigCfg);

    for (int i = 0; i < 5; ++i) {
        cam.sendSoftTrigger();
        std::this_thread::sleep_for(150ms);
    }
    std::this_thread::sleep_for(500ms); // let callbacks arrive
    std::cout << "  Frames in queue after trigger phase: " << queue.size() << '\n';

    // --- Phase 3: Switch back to StreamCallback ---
    std::cout << "  Phase 3: Back to StreamCallback (1 s)\n";
    queue.clear();
    cam.switchMode({GrabMode::StreamCallback});
    std::this_thread::sleep_for(1s);
    std::cout << "  Queue size after second stream phase: " << queue.size() << '\n';

    cam.unregisterFrameCallback(cbId);

    std::cout << "  Mode switching demo complete.\n";
}

// ======================================================================== //
//  Main                                                                      //
// ======================================================================== //

int main() {
    std::cout << "CameraWrapper C++ Console Demo\n"
              << "================================\n\n";

    // ---------------------------------------------------------------- //
    //  1. Enumerate devices                                              //
    // ---------------------------------------------------------------- //
    auto enumerator = createEnumerator();
    if (! enumerator) {
        std::cerr << "Failed to create enumerator\n";
        return 1;
    }
    auto devices = enumerator->enumerate();

    std::cout << "Found " << devices.size() << " device(s):\n";
    printDeviceList(devices);

    if (devices.empty()) {
        std::cerr << "\nNo cameras found. Exiting.\n";
        return 1;
    }

    // Use the first device for the demo.
    const DeviceInfo& selected = devices[0];
    std::cout << "\nUsing: " << selected.displayName() << '\n';

    // ---------------------------------------------------------------- //
    //  2. Create and open camera                                         //
    // ---------------------------------------------------------------- //
    auto cam = enumerator->createCamera(selected);
    if (! cam) {
        std::cerr << "Failed to create camera object\n";
        return 1;
    }

    // Register status callback to print connection events.
    cam->registerStatusCallback([](const StatusEvent& ev) {
        const char* names[] = {"Connected", "Disconnected", "Reconnecting", "Reconnected", "Error"};
        int idx = static_cast<int>(ev.status);
        std::cout << "[Status] " << names[idx] << (ev.message.empty() ? "" : ": " + ev.message)
                  << (ev.retryCount > 0 ? " (retry " + std::to_string(ev.retryCount) + ")" : "")
                  << '\n';
    });

    if (! cam->open()) {
        std::cerr << "Failed to open camera\n";
        return 1;
    }

    // ---------------------------------------------------------------- //
    //  3. Apply transport configuration                                  //
    // ---------------------------------------------------------------- //
    TransportConfig transport;
    transport.gige.packetSize = 0; // auto-detect optimal MTU
    transport.gige.heartbeatTimeoutMs = 3000;
    transport.gige.interPacketDelayNs = 0;
    cam->applyTransportConfig(transport);

    // ---------------------------------------------------------------- //
    //  4. Set camera parameters                                          //
    // ---------------------------------------------------------------- //
    double us = 0, minUs = 0, maxUs = 0;
    if (cam->getExposureTime(us, minUs, maxUs))
        std::cout << "Exposure: " << us << " us  [" << minUs << ", " << maxUs << "]\n";

    cam->setExposureTime(10000.0); // 10 ms

    double dB = 0, minDb = 0, maxDb = 0;
    if (cam->getGain(dB, minDb, maxDb))
        std::cout << "Gain: " << dB << " dB  [" << minDb << ", " << maxDb << "]\n";

    cam->setGain(0.0); // minimum gain

    // ---------------------------------------------------------------- //
    //  5. Run demos                                                      //
    // ---------------------------------------------------------------- //
    demoSnapSync(*cam);
    demoTriggerCallback(*cam);
    demoStreamCallback(*cam);
    demoModeSwitching(*cam);

    // ---------------------------------------------------------------- //
    //  6. Shutdown                                                       //
    // ---------------------------------------------------------------- //
    std::cout << "\nShutting down...\n";
    cam->close();
    std::cout << "Done.\n";

    return 0;
}
