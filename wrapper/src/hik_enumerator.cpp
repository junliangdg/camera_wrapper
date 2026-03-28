#include "hik_enumerator.h"

#include "hik_camera.h"

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4828)
#endif
#include "MvCameraControl.h"
#ifdef _WIN32
#pragma warning(pop)
#endif

#include <memory>

namespace camera_wrapper {

// Forward-declare the helper that lives in HikCamera.cpp.
// We redeclare it here as a static free function to avoid exposing it in a
// shared header.  Both translation units are compiled into the same library,
// so the linker resolves the reference.
static DeviceInfo buildDeviceInfoFromSdk(const MV_CC_DEVICE_INFO& sdk);

// ---- local copy of the conversion helper (same logic as in HikCamera.cpp) --

static InterfaceType sdkLayerToType(unsigned int t) {
    switch (t) {
        case MV_GIGE_DEVICE:
            return InterfaceType::GigE;
        case MV_USB_DEVICE:
            return InterfaceType::USB3;
        case MV_CAMERALINK_DEVICE:
            return InterfaceType::CameraLink;
        default:
            return InterfaceType::Unknown;
    }
}

static DeviceInfo buildDeviceInfoFromSdk(const MV_CC_DEVICE_INFO& sdk) {
    DeviceInfo info;
    info.interfaceType = sdkLayerToType(sdk.nTLayerType);

    switch (sdk.nTLayerType) {
        case MV_GIGE_DEVICE: {
            const auto& g = sdk.SpecialInfo.stGigEInfo;
            info.serialNumber = reinterpret_cast<const char*>(g.chSerialNumber);
            info.modelName = reinterpret_cast<const char*>(g.chModelName);
            info.vendor = reinterpret_cast<const char*>(g.chManufacturerName);
            info.userDefinedName = reinterpret_cast<const char*>(g.chUserDefinedName);

            unsigned int ip = g.nCurrentIp;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                          (ip >> 8) & 0xFF, ip & 0xFF);
            info.ipAddress = buf;
            break;
        }
        case MV_USB_DEVICE: {
            const auto& u = sdk.SpecialInfo.stUsb3VInfo;
            info.serialNumber = reinterpret_cast<const char*>(u.chSerialNumber);
            info.modelName = reinterpret_cast<const char*>(u.chModelName);
            info.vendor = reinterpret_cast<const char*>(u.chManufacturerName);
            info.userDefinedName = reinterpret_cast<const char*>(u.chUserDefinedName);
            break;
        }
        case MV_CAMERALINK_DEVICE: {
            const auto& c = sdk.SpecialInfo.stCamLInfo;
            info.serialNumber = reinterpret_cast<const char*>(c.chSerialNumber);
            info.modelName = reinterpret_cast<const char*>(c.chModelName);
            info.vendor = reinterpret_cast<const char*>(c.chManufacturerName);
            break;
        }
        default:
            break;
    }
    return info;
}

static unsigned int typeToSdkLayer(InterfaceType t) {
    switch (t) {
        case InterfaceType::GigE:
            return MV_GIGE_DEVICE;
        case InterfaceType::USB3:
            return MV_USB_DEVICE;
        case InterfaceType::CameraLink:
            return MV_CAMERALINK_DEVICE;
        default:
            return MV_GIGE_DEVICE | MV_USB_DEVICE | MV_CAMERALINK_DEVICE;
    }
}

static std::vector<DeviceInfo> doEnumerate(unsigned int layerMask) {
    MV_CC_DEVICE_INFO_LIST list{};
    std::vector<DeviceInfo> result;

    if (MV_CC_EnumDevices(layerMask, &list) != MV_OK)
        return result;

    for (unsigned int i = 0; i < list.nDeviceNum; ++i)
        result.push_back(buildDeviceInfoFromSdk(*list.pDeviceInfo[i]));

    return result;
}

// ======================================================================== //
//  HikEnumerator                                                             //
// ======================================================================== //

std::vector<DeviceInfo> HikEnumerator::enumerate() {
    return doEnumerate(MV_GIGE_DEVICE | MV_USB_DEVICE | MV_CAMERALINK_DEVICE);
}

std::vector<DeviceInfo> HikEnumerator::enumerateByType(InterfaceType type) {
    return doEnumerate(typeToSdkLayer(type));
}

std::optional<DeviceInfo> HikEnumerator::findBySerial(const std::string& serial) {
    auto all = enumerate();
    for (auto& d : all)
        if (d.serialNumber == serial)
            return d;
    return std::nullopt;
}

std::unique_ptr<Camera> HikEnumerator::createCamera(const DeviceInfo& info) {
    return std::make_unique<HikCamera>(info);
}

} // namespace camera_wrapper
