#pragma once

// Internal header – not installed as a public header.

#include "camera_wrapper/camera_enumerator.h"
#include "camera_wrapper/export.h"

namespace camera_wrapper {

/// Hikrobot MVS SDK device enumerator.
///
/// Enumerates GigE, USB3, and CameraLink cameras managed by the MVS SDK.
class CAMERA_WRAPPER_API HikEnumerator : public CameraEnumerator {
  public:
    HikEnumerator() = default;
    ~HikEnumerator() = default;

    std::vector<DeviceInfo> enumerate() override;
    std::vector<DeviceInfo> enumerateByType(InterfaceType type) override;
    std::optional<DeviceInfo> findBySerial(const std::string& serial) override;
    std::unique_ptr<Camera> createCamera(const DeviceInfo& info) override;
};

} // namespace camera_wrapper
