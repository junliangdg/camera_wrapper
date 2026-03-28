#include "../include/camera_wrapper/enumerator_factory.h"

#include "hik_enumerator.h"

namespace camera_wrapper {

std::unique_ptr<CameraEnumerator> createEnumerator(EnumeratorType type) {
    switch (type) {
        case EnumeratorType::Hikrobot:
            return std::make_unique<HikEnumerator>();

            // Future implementations:
            // case EnumeratorType::Basler:
            //     return std::make_unique<BaslerEnumerator>();
            // case EnumeratorType::Teledyne:
            //     return std::make_unique<TelemetryEnumerator>();

        default:
            return nullptr; // Unsupported type
    }
}

} // namespace camera_wrapper
