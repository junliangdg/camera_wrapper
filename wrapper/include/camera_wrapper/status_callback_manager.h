#pragma once

#include "export.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace camera_wrapper {

/// Camera connection / lifecycle events delivered through the status channel.
enum class CAMERA_WRAPPER_API CameraStatus {
    Connected, ///< Camera opened and ready
    Disconnected, ///< Camera link lost (hardware disconnect / timeout)
    Reconnecting, ///< Auto-reconnect attempt in progress
    Reconnected, ///< Auto-reconnect succeeded; camera is operational again
    Error, ///< Unrecoverable error (device removed, driver fault, …)
};

/// Optional human-readable detail attached to a status event.
struct CAMERA_WRAPPER_API StatusEvent {
    CameraStatus status;
    std::string message; ///< May be empty
    int retryCount{0}; ///< Meaningful for Reconnecting events
};

/// Manages a set of status-event subscribers.
///
/// Thread-safety model is identical to FrameCallbackManager:
///   - register/unregister are mutex-protected.
///   - notifyAll snapshots the list under the mutex, then releases it before
///     invoking callbacks.
///   - Each subscriber call is wrapped in try/catch.
///
/// This is a completely separate channel from FrameCallbackManager; the two
/// share no locks and cannot deadlock each other.
class CAMERA_WRAPPER_API StatusCallbackManager {
  public:
    using Callback = std::function<void(const StatusEvent&)>;
    using CallbackId = int;

    StatusCallbackManager() = default;
    ~StatusCallbackManager() = default;

    StatusCallbackManager(const StatusCallbackManager&) = delete;
    StatusCallbackManager& operator=(const StatusCallbackManager&) = delete;

    /// Register a new subscriber.
    /// @return An opaque ID that can be passed to unregisterCallback().
    CallbackId registerCallback(Callback cb);

    /// Unregister a previously registered subscriber.
    void unregisterCallback(CallbackId id);

    /// Notify all subscribers of a status event.
    void notifyAll(const StatusEvent& event);

    std::size_t subscriberCount() const;

    void clear();

  private:
    mutable std::mutex mutex_;
    std::map<CallbackId, Callback> callbacks_;
    CallbackId nextId_{0};
};

} // namespace camera_wrapper
