#pragma once

namespace camera_wrapper {

/// The three acquisition modes supported by the wrapper.
enum class GrabMode {
    /// Caller-driven synchronous grab.
    /// snapSync() blocks until one frame is returned.
    /// The camera is put into trigger mode; if the trigger source is
    /// SoftwareTrigger, snapSync() fires the trigger automatically.
    SnapSync,

    /// Trigger-driven callback mode.
    /// The camera waits for a trigger (hardware or software).  Each trigger
    /// produces exactly one frame delivered via the registered frame callbacks.
    /// Use sendSoftTrigger() to fire a software trigger.
    TriggerCallback,

    /// Free-running continuous stream callback mode.
    /// The camera acquires at its maximum frame rate and delivers every frame
    /// via the registered frame callbacks.
    StreamCallback,
};

/// Trigger source selection.
enum class TriggerSource {
    Software, ///< Trigger fired by sendSoftTrigger()
    Line0, ///< External hardware trigger on Line 0
    Line1, ///< External hardware trigger on Line 1
    Line2,
    Line3,
};

/// Configuration for a grab session.
/// Passed to Camera::configure() or Camera::switchMode().
struct GrabConfig {
    GrabMode mode{GrabMode::StreamCallback};
    TriggerSource triggerSource{TriggerSource::Software};

    /// Timeout for snapSync() in milliseconds.
    /// Ignored in callback modes.
    int snapTimeoutMs{3000};

    /// Number of retry attempts in snapSync() when a frame has lost packets.
    int snapRetryCount{3};
};

} // namespace camera_wrapper
