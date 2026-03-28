#pragma once

#include "export.h"

#include <string>

namespace camera_wrapper {

/// Transport-layer protocol of a camera device.
enum class InterfaceType {
    Unknown = 0,
    GigE = 1,
    USB3 = 4,
    CameraLink = 8,
    GenTL = 16,
};

/// Protocol-agnostic device descriptor returned by the enumerator.
///
/// GigE-specific fields are only valid when interfaceType == GigE.
/// USB3-specific fields are only valid when interfaceType == USB3.
struct CAMERA_WRAPPER_API DeviceInfo {
    InterfaceType interfaceType{InterfaceType::Unknown};

    std::string serialNumber; ///< Unique serial number (all protocols)
    std::string modelName; ///< Camera model string
    std::string vendor; ///< Manufacturer name
    std::string userDefinedName; ///< User-configurable label (may be empty)

    // GigE-specific -------------------------------------------------------
    std::string ipAddress; ///< Dotted-decimal, e.g. "192.168.1.100"
    std::string subnetMask;
    std::string defaultGateway;
    std::string macAddress; ///< Colon-separated hex, e.g. "00:11:22:33:44:55"

    // USB3-specific -------------------------------------------------------
    std::string usbPortId; ///< Platform-specific port identifier

    // ------------------------------------------------------------------ //

    /// Generate a human-readable display name suitable for UI lists.
    /// Format: "<Protocol> | <Model> | <IP or SN> (<SN>)"
    /// Example: "GigE | MV-CS060-10GC | 192.168.1.100 (DA0012345)"
    std::string displayName() const;
};

} // namespace camera_wrapper
