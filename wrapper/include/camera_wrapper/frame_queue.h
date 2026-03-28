#pragma once

#include "export.h"
#include "image_frame.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace camera_wrapper {

/// Overflow policy applied when the queue is full and a new frame arrives.
enum class OverflowPolicy {
    /// Drop the oldest frame in the queue to make room for the new one.
    /// Best for real-time preview / positioning guidance – always keeps the
    /// most recent frame available.
    DropOldest,

    /// Clear ALL frames from the queue and keep only the newest one.
    /// Best for AGV navigation or any scenario that only cares about the
    /// instantaneous state.
    DropAll,

    /// Reject the incoming frame; the queue contents are preserved in order.
    /// Best for triggered defect inspection where every frame must be
    /// processed in sequence.
    DropNewest,

    /// Block the producer (SDK callback thread) until a slot is free.
    /// Use with extreme caution – stalls the SDK callback thread.
    /// Only suitable for offline / controlled-rate acquisition.
    Block,
};

/// Statistics counters exposed by FrameQueue.
struct CAMERA_WRAPPER_API FrameQueueStats {
    uint64_t totalPushed{0}; ///< Total frames offered to push()
    uint64_t totalPopped{0}; ///< Total frames successfully returned by pop()
    uint64_t dropCount{0}; ///< Total frames discarded due to overflow
};

/// Thread-safe, bounded frame queue with configurable overflow policy.
///
/// Producer side (SDK callback thread):
///   Call push(frame) – only a Mat shallow-copy is performed, so the call
///   completes in microseconds.
///
/// Consumer side (business / algorithm thread):
///   Call pop(timeoutMs) – blocks until a frame is available or the timeout
///   expires.  Returns std::nullopt on timeout or after stop() is called.
///
/// The queue is an independent utility class; it is NOT owned by any camera
/// class.  Business code creates and holds its own FrameQueue instances and
/// registers them as frame-callback subscribers.
class CAMERA_WRAPPER_API FrameQueue {
  public:
    /// @param capacity      Maximum number of frames held simultaneously.
    /// @param policy        What to do when the queue is full.
    explicit FrameQueue(std::size_t capacity = 8,
                        OverflowPolicy policy = OverflowPolicy::DropOldest);

    ~FrameQueue();

    // Non-copyable, non-movable (owns a mutex and condition_variable).
    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;
    FrameQueue(FrameQueue&&) = delete;
    FrameQueue& operator=(FrameQueue&&) = delete;

    // ------------------------------------------------------------------ //
    //  Runtime configuration (thread-safe, can be called at any time)     //
    // ------------------------------------------------------------------ //

    /// Change the maximum capacity.  Excess frames are dropped immediately
    /// according to the current overflow policy (oldest first).
    void setCapacity(std::size_t capacity);

    /// Change the overflow policy at runtime.
    void setOverflowPolicy(OverflowPolicy policy);

    std::size_t capacity() const;
    OverflowPolicy overflowPolicy() const;

    // ------------------------------------------------------------------ //
    //  Producer API                                                        //
    // ------------------------------------------------------------------ //

    /// Push a frame into the queue.
    /// The frame is shallow-copied (cv::Mat reference count increment only).
    /// If the queue is full the overflow policy is applied.
    /// Thread-safe; may be called from the SDK callback thread.
    void push(const ImageFrame& frame);

    // ------------------------------------------------------------------ //
    //  Consumer API                                                        //
    // ------------------------------------------------------------------ //

    /// Pop the oldest frame from the queue.
    ///
    /// @param timeoutMs  < 0  : wait forever
    ///                   == 0 : non-blocking (return immediately)
    ///                   > 0  : wait up to timeoutMs milliseconds
    ///
    /// @return The frame, or std::nullopt on timeout / stop.
    std::optional<ImageFrame> pop(int timeoutMs = -1);

    /// Current number of frames in the queue.
    std::size_t size() const;

    bool empty() const;

    /// Discard all frames currently in the queue.
    void clear();

    /// Unblock any threads waiting in pop() and prevent further pushes.
    /// Call this before destroying the queue while consumer threads are
    /// still running.
    void stop();

    /// Re-enable pushing after a stop().
    void resume();

    // ------------------------------------------------------------------ //
    //  Statistics                                                          //
    // ------------------------------------------------------------------ //

    FrameQueueStats stats() const;
    void resetStats();

  private:
    void enforceCapacity_locked(); ///< Must be called with mutex_ held.

    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::deque<ImageFrame> queue_;
    std::size_t capacity_;
    OverflowPolicy policy_;
    bool stopped_{false};

    // Statistics (updated under mutex_)
    uint64_t totalPushed_{0};
    uint64_t totalPopped_{0};
    uint64_t dropCount_{0};
};

} // namespace camera_wrapper
