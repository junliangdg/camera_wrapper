#pragma once

#include "camera_enumerator.h"
#include "export.h"

#include <memory>

namespace camera_wrapper {

/// Supported camera enumerator implementations.
enum class EnumeratorType {
    Hikrobot, ///< Hikrobot MVS SDK (GigE, USB3, CameraLink)
    // Basler,     ///< Basler Pylon SDK (future)
    // Teledyne,   ///< Teledyne FLIR SDK (future)
};

/// Create a camera enumerator for the specified vendor.
/// @param type The vendor type (default: Hikrobot)
/// @return A CameraEnumerator instance, or nullptr if the type is not supported
///         or initialization fails.
CAMERA_WRAPPER_API std::unique_ptr<CameraEnumerator>
createEnumerator(EnumeratorType type = EnumeratorType::Hikrobot);

} // namespace camera_wrapper
