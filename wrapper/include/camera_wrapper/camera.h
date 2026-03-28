#pragma once

#include "device_info.h"
#include "export.h"
#include "frame_callback_manager.h"
#include "grab_config.h"
#include "image_frame.h"
#include "status_callback_manager.h"
#include "transport_config.h"

#include <optional>

namespace camera_wrapper {

/// Connection state of the camera.
enum class ConnectionState {
    Disconnected,
    Connected,
    Reconnecting,
};

/// Abstract camera interface.
///
/// All public methods are thread-safe unless explicitly noted otherwise.
/// Methods return bool to indicate success/failure; exceptions are NOT used
/// as flow-control mechanisms.
///
/// Typical usage
/// -------------
///   1. Obtain a DeviceInfo from CameraEnumerator.
///   2. Construct the concrete camera (e.g. HikCamera).
///   3. Call open().
///   4. Optionally call applyTransportConfig() and setExposureTime() / setGain().
///   5. Register frame callbacks with registerFrameCallback().
///   6. Call configure() / switchMode() to select the grab mode.
///   7. Call startGrabbing().
///   8. Consume frames via snapSync() or the registered callbacks.
///   9. Call stopGrabbing() then close() when done.
class CAMERA_WRAPPER_API Camera {
  public:
    virtual ~Camera() = default;

    // ------------------------------------------------------------------ //
    //  Lifecycle                                                           //
    // ------------------------------------------------------------------ //

    /// Open the camera device and establish the SDK handle.
    /// @return true on success.
    virtual bool open() = 0;

    /// Stop grabbing (if active), release the SDK handle, and free resources.
    virtual bool close() = 0;

    /// @return true if the camera handle is open and the device is reachable.
    virtual bool isOpened() const = 0;

    // ------------------------------------------------------------------ //
    //  Transport configuration                                             //
    // ------------------------------------------------------------------ //

    /// Apply transport-layer parameters (packet size, heartbeat, etc.).
    /// The implementation selects the sub-struct matching the actual interface
    /// type and ignores the rest.
    /// Should be called after open() and before startGrabbing().
    virtual bool applyTransportConfig(const TransportConfig& cfg) = 0;

    // ------------------------------------------------------------------ //
    //  Camera parameter control                                            //
    // ------------------------------------------------------------------ //

    /// @param us  Exposure time in microseconds.
    virtual bool setExposureTime(double us) = 0;
    virtual bool getExposureTime(double& us, double& minUs, double& maxUs) = 0;

    /// @param dB  Analogue gain in dB.
    virtual bool setGain(double dB) = 0;
    virtual bool getGain(double& dB, double& minDb, double& maxDb) = 0;

    // ------------------------------------------------------------------ //
    //  Acquisition control                                                 //
    // ------------------------------------------------------------------ //

    /// Configure the grab mode without starting acquisition.
    /// Stores the configuration as the "desired state" for auto-reconnect.
    virtual bool configure(const GrabConfig& cfg) = 0;

    /// Start frame acquisition using the last configure()d settings.
    virtual bool startGrabbing() = 0;

    /// Stop frame acquisition.  Safe to call even if not grabbing.
    virtual bool stopGrabbing() = 0;

    /// @return true if the camera is currently grabbing.
    virtual bool isGrabbing() const = 0;

    /// @return The currently active grab mode.
    virtual GrabMode currentMode() const = 0;

    /// Atomically stop → reconfigure → start in one call.
    ///
    /// This is the preferred way to switch between modes at runtime.
    /// The call is idempotent: switching to the same mode resets the state.
    /// Registered frame callbacks are NOT cleared by this call.
    ///
    /// Thread-safety: safe to call from any thread.  Concurrent switchMode()
    /// calls are serialised internally.
    virtual bool switchMode(const GrabConfig& cfg) = 0;

    // ------------------------------------------------------------------ //
    //  Synchronous grab (SnapSync mode)                                    //
    // ------------------------------------------------------------------ //

    /// Block until one frame is captured and return it.
    ///
    /// In SnapSync mode with a software trigger source, the trigger is fired
    /// automatically.  With a hardware trigger source, the call blocks until
    /// the external signal arrives.
    ///
    /// @return The captured frame, or std::nullopt on timeout / error.
    virtual std::optional<ImageFrame> snapSync() = 0;

    // ------------------------------------------------------------------ //
    //  Software trigger                                                    //
    // ------------------------------------------------------------------ //

    /// Send a software trigger command to the camera.
    /// Only meaningful in TriggerCallback or SnapSync mode with
    /// TriggerSource::Software.
    virtual bool sendSoftTrigger() = 0;

    // ------------------------------------------------------------------ //
    //  Frame callback subscription                                         //
    // ------------------------------------------------------------------ //

    /// Register a frame-arrival callback.
    ///
    /// The callback is invoked from the SDK callback thread.  It MUST return
    /// quickly (microseconds).  The recommended pattern is to shallow-copy the
    /// frame into a FrameQueue and return immediately.
    ///
    /// @return An opaque ID for use with unregisterFrameCallback().
    virtual FrameCallbackManager::CallbackId
    registerFrameCallback(FrameCallbackManager::Callback cb) = 0;

    /// Unregister a previously registered frame callback.
    virtual void unregisterFrameCallback(FrameCallbackManager::CallbackId id) = 0;

    // ------------------------------------------------------------------ //
    //  Status / event callback subscription                                //
    // ------------------------------------------------------------------ //

    /// Register a status-event callback (connect / disconnect / reconnect).
    /// @return An opaque ID for use with unregisterStatusCallback().
    virtual StatusCallbackManager::CallbackId
    registerStatusCallback(StatusCallbackManager::Callback cb) = 0;

    /// Unregister a previously registered status callback.
    virtual void unregisterStatusCallback(StatusCallbackManager::CallbackId id) = 0;

    // ------------------------------------------------------------------ //
    //  Connection state                                                    //
    // ------------------------------------------------------------------ //

    virtual ConnectionState connectionState() const = 0;

    // ------------------------------------------------------------------ //
    //  Device information                                                  //
    // ------------------------------------------------------------------ //

    virtual DeviceInfo deviceInfo() const = 0;
};

} // namespace camera_wrapper
