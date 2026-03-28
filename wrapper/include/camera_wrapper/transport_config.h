#pragma once

namespace camera_wrapper {

/// GigE Vision transport-layer parameters.
/// Only applied when the camera uses the GigE interface.
struct GigETransportConfig {
    /// Packet size in bytes.  Set to 0 to let the wrapper auto-detect the
    /// optimal MTU via MV_CC_GetOptimalPacketSize (recommended).
    int packetSize{0};

    /// Enable packet resend (lost-packet retransmission).
    bool packetResendEnable{true};

    /// Maximum number of resend attempts per packet.
    int packetResendMaxRetry{3};

    /// Resend request timeout in milliseconds.
    int packetResendTimeoutMs{500};

    /// GigE heartbeat timeout in milliseconds.
    /// Set to a large value (e.g. 60 000) when debugging with breakpoints to
    /// prevent the camera from dropping the connection.
    int heartbeatTimeoutMs{3000};

    /// Frame receive timeout in milliseconds.
    int frameTimeoutMs{3000};

    /// Inter-packet delay in nanoseconds.
    /// Used to spread bandwidth when multiple GigE cameras share one NIC.
    /// Set to 0 to disable.
    int interPacketDelayNs{0};
};

/// USB3 Vision transport-layer parameters.
/// Only applied when the camera uses the USB3 interface.
struct USB3TransportConfig {
    /// Transfer size in bytes (typically a multiple of 1 MiB).
    int transferSize{1 * 1024 * 1024};

    /// Number of simultaneous USB transfer requests.
    int transferCount{8};
};

/// Aggregated transport configuration passed to Camera::applyTransportConfig().
/// The camera implementation selects the relevant sub-struct based on the
/// actual interface type; mismatched sub-structs are silently ignored.
struct TransportConfig {
    GigETransportConfig gige;
    USB3TransportConfig usb3;
};

} // namespace camera_wrapper
