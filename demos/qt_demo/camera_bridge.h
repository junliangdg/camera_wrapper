#pragma once

/**
 * CameraBridge – Qt adapter layer between camera_wrapper and the Qt UI.
 *
 * This class lives in the demo project, NOT in the wrapper library.
 * It demonstrates the recommended bridging pattern:
 *
 *   SDK callback thread
 *       │  (frame arrives)
 *       ▼
 *   registerFrameCallback lambda
 *       │  QMetaObject::invokeMethod(QueuedConnection)
 *       ▼
 *   Qt event loop (main thread)
 *       │  emit frameReady(...)
 *       ▼
 *   MainWindow slot → display
 *
 * The QueuedConnection ensures the cv::Mat is delivered to the Qt main
 * thread safely without any additional locking.
 */

#include "camera_wrapper/camera.h"
#include "camera_wrapper/camera_enumerator.h"
#include "camera_wrapper/frame_callback_manager.h"
#include "camera_wrapper/frame_queue.h"
#include "camera_wrapper/grab_config.h"
#include "camera_wrapper/image_frame.h"
#include "camera_wrapper/status_callback_manager.h"

#include <QFuture>
#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>
#include <opencv2/core.hpp>

namespace camera_wrapper {
class CameraEnumerator;
}

/// Qt-side wrapper around a single Camera instance.
///
/// All public slots are safe to call from the Qt main thread.
/// Signals are emitted on the Qt main thread (via QueuedConnection).
class CameraBridge : public QObject {
    Q_OBJECT

  public:
    explicit CameraBridge(QObject* parent = nullptr);
    ~CameraBridge() override;

    // ---------------------------------------------------------------- //
    //  Device management                                                 //
    // ---------------------------------------------------------------- //

    /// Enumerate all available cameras and return their display names.
    QVector<QString> enumerateDevices();

    /// Open the camera at the given index (from the last enumerateDevices()).
    bool openCamera(int index);

    /// Close the currently open camera.
    void closeCamera();

    bool isCameraOpen() const;

    // ---------------------------------------------------------------- //
    //  Acquisition control                                               //
    // ---------------------------------------------------------------- //

    /// Switch to the given grab mode.
    bool switchMode(camera_wrapper::GrabMode mode, camera_wrapper::TriggerSource triggerSource =
                                                       camera_wrapper::TriggerSource::Software);

    /// Send a software trigger (TriggerCallback / SnapSync mode only).
    bool sendSoftTrigger();

    // ---------------------------------------------------------------- //
    //  Camera parameters                                                 //
    // ---------------------------------------------------------------- //

    bool setExposureTime(double us);
    bool getExposureTime(double& us, double& minUs, double& maxUs);

    bool setGain(double dB);
    bool getGain(double& dB, double& minDb, double& maxDb);

    // ---------------------------------------------------------------- //
    //  Queue statistics (for status bar)                                 //
    // ---------------------------------------------------------------- //

    camera_wrapper::FrameQueueStats queueStats() const;

    camera_wrapper::GrabMode currentMode() const;

  signals:
    /// Emitted on the Qt main thread when a new frame is ready.
    /// The cv::Mat is a shallow copy – do NOT modify it without clone().
    void frameReady(cv::Mat image, quint64 frameId, quint64 timestampNs);

    /// Emitted when the camera connection state changes.
    void connectionStateChanged(QString statusText);

    /// Emitted when queue statistics change (for status bar updates).
    void queueStatsUpdated(quint64 pushed, quint64 popped, quint64 dropped);

  private:
    void onFrameArrived(const camera_wrapper::ImageFrame& frame);
    void onStatusEvent(const camera_wrapper::StatusEvent& event);

    std::unique_ptr<camera_wrapper::CameraEnumerator> enumerator_;
    std::unique_ptr<camera_wrapper::Camera> camera_;

    std::vector<camera_wrapper::DeviceInfo> deviceList_;

    camera_wrapper::FrameCallbackManager::CallbackId frameCbId_{-1};
    camera_wrapper::StatusCallbackManager::CallbackId statusCbId_{-1};

    // Preview queue: DropAll, capacity 1 – always shows the latest frame.
    // Producer: SDK callback thread (StreamCallback / TriggerCallback modes).
    // Consumer: onFrameArrived() pops immediately after push so that
    //           totalPopped tracks real consumption and the queue never
    //           silently accumulates dropped frames.
    std::unique_ptr<camera_wrapper::FrameQueue> previewQueue_;

    // SnapSync support ------------------------------------------------- //
    // In SnapSync mode the SDK fires no callback.  sendSoftTrigger() posts
    // a snapSync() call to a dedicated worker thread so the Qt main thread
    // is never blocked.
    std::atomic<bool> snapRunning_{false}; ///< prevents overlapping snaps
};
