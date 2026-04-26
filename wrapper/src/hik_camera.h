#pragma once

// This header is INTERNAL to the wrapper library.
// It is NOT installed as a public header and must NOT be included by consumers.
// SDK vendor headers are only included here and in HikCamera.cpp.

#include "camera_wrapper/camera.h"
#include "camera_wrapper/frame_callback_manager.h"
#include "camera_wrapper/grab_config.h"
#include "camera_wrapper/image_frame.h"
#include "camera_wrapper/status_callback_manager.h"
#include "camera_wrapper/transport_config.h"

// Hikrobot MVS SDK headers – only visible inside the library.
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4828)
#endif
#include "CameraParams.h"
#include "MvCameraControl.h"
#ifdef _WIN32
#pragma warning(pop)
#endif

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace camera_wrapper {

/// Hikrobot MVS SDK camera implementation.
///
/// Supports GigE, USB3, and CameraLink devices enumerated by the MVS SDK.
///
/// Thread-safety
/// -------------
/// Locking hierarchy (never acquire in reverse order):
///   1. stateMutex_      – protects grabbing state and mode switching
///   2. desiredMutex_    – protects the desired-state snapshot
///   3. frameCbManager_  – internal lock inside FrameCallbackManager
///   4. statusCbManager_ – internal lock inside StatusCallbackManager
///
/// SDK functions are NEVER called while holding stateMutex_ or desiredMutex_.
/// The pattern is: lock → read/write state → unlock → call SDK → lock → update.
class HikCamera : public Camera {
  public:
    explicit HikCamera(const DeviceInfo& info);
    ~HikCamera() override;

    // Non-copyable, non-movable.
    HikCamera(const HikCamera&) = delete;
    HikCamera& operator=(const HikCamera&) = delete;
    HikCamera(HikCamera&&) = delete;
    HikCamera& operator=(HikCamera&&) = delete;

    // ------------------------------------------------------------------ //
    //  Camera interface                                                    //
    // ------------------------------------------------------------------ //

    bool open() override;
    bool close() override;
    bool isOpened() const override;

    bool applyTransportConfig(const TransportConfig& cfg) override;

    bool setExposureTime(double us) override;
    bool getExposureTime(double& us, double& minUs, double& maxUs) override;

    bool setGain(double dB) override;
    bool getGain(double& dB, double& minDb, double& maxDb) override;

    bool configure(const GrabConfig& cfg) override;
    bool startGrabbing() override;
    bool stopGrabbing() override;
    bool isGrabbing() const override;
    GrabMode currentMode() const override;
    bool switchMode(const GrabConfig& cfg) override;

    std::optional<ImageFrame> grabOne(unsigned int timeoutMs) override;

    bool sendSoftTrigger() override;

    FrameCallbackManager::CallbackId
    registerFrameCallback(FrameCallbackManager::Callback cb) override;
    void unregisterFrameCallback(FrameCallbackManager::CallbackId id) override;

    StatusCallbackManager::CallbackId
    registerStatusCallback(StatusCallbackManager::Callback cb) override;
    void unregisterStatusCallback(StatusCallbackManager::CallbackId id) override;

    ConnectionState connectionState() const override;

    DeviceInfo deviceInfo() const override;

  private:
    // ------------------------------------------------------------------ //
    //  Internal helpers                                                    //
    // ------------------------------------------------------------------ //

    /// Convert a raw SDK buffer + frame info into an ImageFrame (deep copy).
    static ImageFrame convertToFrame(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pInfo);

    /// Apply GigE-specific transport parameters.
    bool applyGigETransport(const GigETransportConfig& cfg);

    /// Apply USB3-specific transport parameters.
    bool applyUSB3Transport(const USB3TransportConfig& cfg);

    /// Set trigger mode on/off and configure trigger source.
    bool applyTriggerSettings(const GrabConfig& cfg);

    /// Synchronous single-frame grab via SDK polling (SnapSync mode only).
    /// Uses MV_CC_GetImageBuffer with lost-packet retry.
    std::optional<ImageFrame> snapSync();

    /// Convert wrapper TriggerSource enum to SDK MV_TRIGGER_SOURCE_xxx value.
    static unsigned int triggerSourceToSdk(TriggerSource src);

    /// Internal stop – does NOT lock stateMutex_.  Caller must ensure safety.
    void stopGrabbingInternal();

    /// Internal start – does NOT lock stateMutex_.
    bool startGrabbingInternal();

    // ------------------------------------------------------------------ //
    //  SDK static callbacks                                                //
    // ------------------------------------------------------------------ //

    /// Frame-arrival callback registered with MV_CC_RegisterImageCallBackEx.
    static void sdkFrameCallback(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo,
                                 void* pUser);

    /// Exception / disconnect callback registered with
    /// MV_CC_RegisterExceptionCallBack.
    static void sdkExceptionCallback(unsigned int nMsgType, void* pUser);

    // ------------------------------------------------------------------ //
    //  Auto-reconnect                                                      //
    // ------------------------------------------------------------------ //

    void onDisconnected();
    void reconnectLoop();

    // ------------------------------------------------------------------ //
    //  Desired-state snapshot (survives disconnect)                        //
    // ------------------------------------------------------------------ //

    struct DesiredState {
        bool grabbing{false};
        GrabConfig grabConfig{};
        TransportConfig transportConfig{};
        double exposureUs{-1.0}; ///< < 0 means "not set by user"
        double gainDb{-1.0}; ///< < 0 means "not set by user"
    };

    // ------------------------------------------------------------------ //
    //  Member data                                                         //
    // ------------------------------------------------------------------ //

    DeviceInfo devInfo_;
    MV_CC_DEVICE_INFO sdkDevInfo_; ///< Original SDK struct kept for re-open

    void* handle_{nullptr}; ///< SDK device handle

    // State
    mutable std::mutex stateMutex_;
    std::atomic<bool> grabbing_{false};
    GrabMode currentMode_{GrabMode::StreamCallback};
    std::atomic<bool> switching_{false}; ///< Prevents concurrent switchMode()

    std::atomic<ConnectionState> connState_{ConnectionState::Disconnected};

    // Desired state (for auto-reconnect)
    mutable std::mutex desiredMutex_;
    DesiredState desired_;

    // Callback managers (each has its own internal lock)
    FrameCallbackManager frameCbManager_;
    StatusCallbackManager statusCbManager_;

    // Auto-reconnect
    std::thread reconnectThread_;
    std::atomic<bool> reconnectStop_{false};
    std::mutex reconnectMutex_;
    std::condition_variable reconnectCv_;
    bool reconnectPending_{false};

    // Reconnect configuration
    int reconnectIntervalMs_{2000};
    int reconnectMaxRetries_{-1}; ///< -1 = unlimited
};

} // namespace camera_wrapper
