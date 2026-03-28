#pragma once

#include "device_info.h"
#include "export.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace camera_wrapper {

class Camera;

/// Abstract device enumerator interface.
///
/// Concrete implementations (e.g. HikEnumerator) wrap vendor-specific SDK
/// enumeration calls and translate results into protocol-agnostic DeviceInfo
/// structures.
class CAMERA_WRAPPER_API CameraEnumerator {
  public:
    virtual ~CameraEnumerator() = default;

    /// Enumerate all reachable devices across all supported protocols.
    /// @return List of discovered devices (may be empty).
    virtual std::vector<DeviceInfo> enumerate() = 0;

    /// Enumerate devices filtered by interface type.
    /// Pass InterfaceType::Unknown to enumerate all.
    virtual std::vector<DeviceInfo> enumerateByType(InterfaceType type) = 0;

    /// Find a single device by serial number.
    /// @return The DeviceInfo if found, or std::nullopt.
    virtual std::optional<DeviceInfo> findBySerial(const std::string& serial) = 0;

    /// Create a camera object for the given device.
    /// The returned camera is NOT yet opened; call Camera::open() explicitly.
    /// @return nullptr if the device type is unsupported.
    virtual std::unique_ptr<Camera> createCamera(const DeviceInfo& info) = 0;
};

} // namespace camera_wrapper
