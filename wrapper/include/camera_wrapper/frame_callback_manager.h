#pragma once

#include "export.h"
#include "image_frame.h"

#include <functional>
#include <map>
#include <mutex>

namespace camera_wrapper {

/// Manages a set of frame-callback subscribers.
///
/// Thread-safety model
/// -------------------
/// - registerCallback / unregisterCallback are protected by an internal mutex.
/// - notifyAll acquires the same mutex to snapshot the subscriber list, then
///   releases it before invoking callbacks.  This means:
///     * Callbacks are never called while the mutex is held.
///     * A subscriber can safely call unregisterCallback from within its own
///       callback without deadlocking.
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
    /// Safe to call from within a callback (will take effect after the
    /// current notifyAll() round completes).
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
    mutable std::mutex mutex_;
    std::map<CallbackId, Callback> callbacks_;
    CallbackId nextId_{0};
};

} // namespace camera_wrapper
