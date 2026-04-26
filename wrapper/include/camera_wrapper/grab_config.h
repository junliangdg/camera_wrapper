#pragma once

namespace camera_wrapper {

/// The three acquisition modes supported by the wrapper.
enum class GrabMode {
    /// Caller-driven synchronous grab via SDK polling.
    /// grabOne() delegates to the internal snapSync() path which blocks
    /// until one frame is returned, with automatic lost-packet retry.
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

    /// Timeout for the internal snapSync() polling path in milliseconds.
    /// Used by grabOne() when the current mode is SnapSync.
    /// Ignored in callback modes (grabOne's own timeoutMs applies instead).
    int snapTimeoutMs{3000};

    /// Number of retry attempts for lost-packet frames in SnapSync mode.
    /// Used by grabOne() → snapSync() internally.
    int snapRetryCount{3};
};

} // namespace camera_wrapper
