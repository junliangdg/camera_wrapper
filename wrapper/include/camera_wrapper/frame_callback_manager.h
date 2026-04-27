#pragma once

#include "export.h"
#include "image_frame.h"

#include <functional>
#include <map>
#include <mutex>
#include <shared_mutex>

namespace camera_wrapper {

/// Manages a set of frame-callback subscribers.
///
/// Thread-safety model
/// -------------------
/// - registerCallback / unregisterCallback / clear hold an exclusive write
///   lock (std::unique_lock<std::shared_mutex>).
/// - notifyAll / subscriberCount hold a shared read lock
///   (std::shared_lock<std::shared_mutex>), so concurrent notifyAll() calls
///   are allowed and the subscriber map is never copied.
/// - If a subscriber calls unregisterCallback() from within its own callback,
///   the write-lock acquisition will block until the current notifyAll() call
///   returns (no deadlock; the removal takes effect after that call).
///   Each subscriber invocation is wrapped in try/catch so a misbehaving
///   subscriber cannot affect others.
///
/// Ownership
/// ---------
/// FrameCallbackManager is an independent utility class.  It is NOT owned by
/// any camera class; business code (or the camera implementation) creates and
/// holds it.
class CAMERA_WRAPPER_API FrameCallbackManager {
  public:
    using Callback = std::function<void(const ImageFrame&)>;
    using CallbackId = int;

    FrameCallbackManager() = default;
    ~FrameCallbackManager() = default;

    // Non-copyable (owns a mutex).
    FrameCallbackManager(const FrameCallbackManager&) = delete;
    FrameCallbackManager& operator=(const FrameCallbackManager&) = delete;

    /// Register a new subscriber.
    /// @return An opaque ID that can be passed to unregisterCallback().
    CallbackId registerCallback(Callback cb);

    /// Unregister a previously registered subscriber.
    /// Safe to call from within a callback (will block until the current
    /// notifyAll() round completes, then take effect immediately).
    void unregisterCallback(CallbackId id);

    /// Invoke all registered subscribers with the given frame.
    /// Each subscriber is called in registration order.
    /// Exceptions thrown by individual subscribers are caught and ignored.
    void notifyAll(const ImageFrame& frame);

    /// Number of currently registered subscribers.
    std::size_t subscriberCount() const;

    /// Remove all subscribers.
    void clear();

  private:
    mutable std::shared_mutex mutex_;
    std::map<CallbackId, Callback> callbacks_;
    CallbackId nextId_{0};
};

} // namespace camera_wrapper
