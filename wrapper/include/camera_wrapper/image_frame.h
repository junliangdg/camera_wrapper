#pragma once

#include "export.h"

#include <cstdint>
#include <opencv2/core.hpp>

namespace camera_wrapper {

/// Pixel format of the captured frame (after conversion to OpenCV Mat).
enum class PixelFormat {
    Unknown = 0,
    Mono8,
    Mono10,
    Mono12,
    Mono16,
    BGR8,
    RGB8,
    BayerRG8,
    BayerRG12,
    BayerRG16,
    YUV422_UYVY,
    YUV422_YUYV,
};

/// Metadata + pixel data for a single captured frame.
///
/// The image data is stored in a cv::Mat which uses reference-counted shared
/// memory.  Copying an ImageFrame is therefore a shallow copy (O(1) atomic
/// increment).  If a consumer needs to modify pixels it must call
/// frame.image.clone() first.
///
/// Only ONE deep copy is performed in the entire pipeline: inside the SDK
/// callback, where the SDK buffer is wrapped in a temporary Mat and then
/// clone()d to produce an independent allocation.  All subsequent transfers
/// (queue push, multi-subscriber dispatch, cross-thread delivery) are shallow.
struct CAMERA_WRAPPER_API ImageFrame {
    cv::Mat image; ///< Pixel data (BGR or Mono, depending on camera config)
    uint64_t frameId{0}; ///< Frame counter from the camera hardware
    uint64_t timestampNs{0}; ///< Hardware timestamp in nanoseconds (camera clock)
    int width{0};
    int height{0};
    PixelFormat pixelFormat{PixelFormat::Unknown};
    uint32_t lostPackets{0}; ///< Number of lost packets reported by the transport layer

    ImageFrame() = default;

    /// Shallow copy – shares the underlying pixel buffer.
    ImageFrame(const ImageFrame&) = default;
    ImageFrame& operator=(const ImageFrame&) = default;

    /// Move is also fine.
    ImageFrame(ImageFrame&&) = default;
    ImageFrame& operator=(ImageFrame&&) = default;

    bool empty() const {
        return image.empty();
    }
};

} // namespace camera_wrapper
