#pragma once

/**
 * CameraBridge – Qt adapter layer between camera_wrapper and the Qt UI.
 *
 * This class lives in the demo project, NOT in the wrapper library.
 * It demonstrates the recommended multi-camera bridging pattern:
 *
 *   SDK callback thread (cam N)
 *       │  (frame arrives)
 *       ▼
 *   registerFrameCallback lambda  [captures cameraIndex]
 *       │  QMetaObject::invokeMethod(QueuedConnection)
 *       ▼
 *   Qt main-thread event loop          ← single shared event loop for ALL cameras
 *       │  emit frameReady(cameraIndex, ...)
 *       ▼
 *   MainWindow slot → display panel N
 *
 * All SDK callback threads from every camera funnel into the same Qt main-thread
 * event loop via QueuedConnection.  Qt's event queue is thread-safe, so no
 * additional locking is required.  Frames are serialised automatically.
 *
 * Each camera slot owns an independent FrameQueue (capacity=1, DropAll) so
 * that a slow UI render on one camera never blocks or starves the others.
 */

#include "camera_wrapper/camera.h"
#include "camera_wrapper/camera_enumerator.h"
#include "camera_wrapper/frame_callback_manager.h"
#include "camera_wrapper/frame_queue.h"
#include "camera_wrapper/grab_config.h"
#include "camera_wrapper/image_frame.h"
#include "camera_wrapper/status_callback_manager.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>
#include <mutex>
#include <opencv2/core.hpp>
#include <vector>

namespace camera_wrapper {
class CameraEnumerator;
} // namespace camera_wrapper

/// Qt-side wrapper that manages **multiple** Camera instances.
///
/// All public methods are safe to call from the Qt main thread.
/// Signals are always emitted on the Qt main thread (via QueuedConnection).
///
/// Index semantics
/// ---------------
/// Every open camera is identified by a *camera index* – the position in the
/// internal slots_ vector.  The index is stable for the lifetime of the slot
/// (i.e. until closeCamera(index) is called).  Indices are NOT recycled after
/// close; call enumerateDevices() + openCamera() to obtain a fresh index.
class CameraBridge : public QObject {
    Q_OBJECT

  public:
    explicit CameraBridge(QObject* parent = nullptr);
    ~CameraBridge() override;

    // ---------------------------------------------------------------- //
    //  Device enumeration                                                //
    // ---------------------------------------------------------------- //

    /// Enumerate all reachable cameras and return their display names.
    /// The returned list is also cached internally; pass indices from this
    /// list to openCamera().
    QVector<QString> enumerateDevices();

    // ---------------------------------------------------------------- //
    //  Per-camera lifecycle                                              //
    // ---------------------------------------------------------------- //

    /// Open the device at position @p deviceIndex in the last
    /// enumerateDevices() result and assign it a camera slot.
    ///
    /// @return The camera index (≥ 0) on success, or -1 on failure.
    ///         The index is used in all subsequent per-camera calls and
    ///         is carried by every signal to identify the source camera.
    int openCamera(int deviceIndex);

    /// Close and release the camera identified by @p cameraIndex.
    void closeCamera(int cameraIndex);

    /// Close ALL open cameras.
    void closeAllCameras();

    /// @return true if the camera slot @p cameraIndex exists and is open.
    bool isCameraOpen(int cameraIndex) const;

    /// @return Number of currently open camera slots.
    int openCameraCount() const;

    // ---------------------------------------------------------------- //
    //  Per-camera acquisition control                                    //
    // ---------------------------------------------------------------- //

    /// Switch the grab mode of camera @p cameraIndex.
    bool switchMode(
        int cameraIndex, camera_wrapper::GrabMode mode,
        camera_wrapper::TriggerSource triggerSource = camera_wrapper::TriggerSource::Software);

    /// Send a software trigger to camera @p cameraIndex.
    bool sendSoftTrigger(int cameraIndex);

    /// Synchronously grab a single frame from camera @p cameraIndex,
    /// regardless of the current grab mode.  The call is non-blocking from
    /// the caller's perspective: the actual grabOne() is dispatched to a
    /// worker thread and the result is delivered via the frameReady() signal.
    ///
    /// @return true  if the async grab was successfully queued.
    /// @return false if no camera exists at @p cameraIndex, or a grab is
    ///               already in flight for that slot.
    bool grabOne(int cameraIndex);

    // ---------------------------------------------------------------- //
    //  Per-camera parameter control                                      //
    // ---------------------------------------------------------------- //

    bool setExposureTime(int cameraIndex, double us);
    bool getExposureTime(int cameraIndex, double& us, double& minUs, double& maxUs);

    bool setGain(int cameraIndex, double dB);
    bool getGain(int cameraIndex, double& dB, double& minDb, double& maxDb);

    // ---------------------------------------------------------------- //
    //  Per-camera statistics / state query                               //
    // ---------------------------------------------------------------- //

    camera_wrapper::FrameQueueStats queueStats(int cameraIndex) const;
    camera_wrapper::GrabMode currentMode(int cameraIndex) const;

  signals:
    /// Emitted on the Qt main thread when a new frame is ready.
    ///
    /// @param cameraIndex  Identifies which camera produced the frame.
    /// @param image        Shallow copy of the frame's cv::Mat.
    ///                     Do NOT modify without calling image.clone() first.
    /// @param frameId      Hardware frame counter.
    /// @param timestampNs  Hardware timestamp in nanoseconds.
    void frameReady(int cameraIndex, cv::Mat image, quint64 frameId, quint64 timestampNs);

    /// Emitted when the connection state of a camera changes.
    ///
    /// @param cameraIndex  Identifies which camera changed state.
    /// @param statusText   Human-readable description of the new state.
    void connectionStateChanged(int cameraIndex, QString statusText);

    /// Emitted (on the Qt main thread) after each frame delivery so the UI
    /// can refresh queue statistics in the status bar.
    ///
    /// @param cameraIndex  Identifies which camera's queue was updated.
    void queueStatsUpdated(int cameraIndex, quint64 pushed, quint64 popped, quint64 dropped);

  private:
    // ---------------------------------------------------------------- //
    //  Internal per-camera state                                         //
    // ---------------------------------------------------------------- //

    struct CameraSlot {
        std::unique_ptr<camera_wrapper::Camera> camera;

        // Preview queue: capacity 1, DropAll – always holds the latest frame.
        // Each slot has its own queue so a slow consumer on one camera cannot
        // starve or block the others.
        std::unique_ptr<camera_wrapper::FrameQueue> previewQueue;

        camera_wrapper::FrameCallbackManager::CallbackId frameCbId{-1};
        camera_wrapper::StatusCallbackManager::CallbackId statusCbId{-1};

        // SnapSync: prevents overlapping snap calls on this slot.
        std::atomic<bool> snapRunning{false};

        CameraSlot();
        ~CameraSlot() = default;

        // Non-copyable (owns atomic + unique_ptr).
        CameraSlot(const CameraSlot&) = delete;
        CameraSlot& operator=(const CameraSlot&) = delete;

        // Movable (needed for vector growth before slots are populated).
        CameraSlot(CameraSlot&&) = default;
        CameraSlot& operator=(CameraSlot&&) = default;
    };

    // ---------------------------------------------------------------- //
    //  Private helpers                                                   //
    // ---------------------------------------------------------------- //

    /// Returns a raw pointer to the slot if @p cameraIndex is valid and the
    /// camera is open, otherwise nullptr.
    CameraSlot* slotAt(int cameraIndex);
    const CameraSlot* slotAt(int cameraIndex) const;

    /// Frame-arrival handler – called from the SDK callback thread.
    /// @p cameraIndex is captured by the lambda registered in openCamera().
    void onFrameArrived(int cameraIndex, const camera_wrapper::ImageFrame& frame);

    /// Status-event handler – called from the reconnect / SDK thread.
    void onStatusEvent(int cameraIndex, const camera_wrapper::StatusEvent& event);

    // ---------------------------------------------------------------- //
    //  Member data                                                       //
    // ---------------------------------------------------------------- //

    std::unique_ptr<camera_wrapper::CameraEnumerator> enumerator_;
    std::vector<camera_wrapper::DeviceInfo> deviceList_;

    // slots_ is indexed by camera index.  A slot whose camera == nullptr is
    // considered closed / unused.  Slots are never removed from the vector
    // so that existing indices remain stable.
    mutable std::mutex slotsMutex_; ///< protects slots_ structural changes
    std::vector<std::unique_ptr<CameraSlot>> slots_;
};
