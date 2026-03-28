#include "camera_wrapper/device_info.h"

namespace camera_wrapper {

std::string DeviceInfo::displayName() const {
    std::string proto;
    switch (interfaceType) {
        case InterfaceType::GigE:
            proto = "GigE";
            break;
        case InterfaceType::USB3:
            proto = "USB3";
            break;
        case InterfaceType::CameraLink:
            proto = "CameraLink";
            break;
        case InterfaceType::GenTL:
            proto = "GenTL";
            break;
        default:
            proto = "Unknown";
            break;
    }

    std::string location;
    if (interfaceType == InterfaceType::GigE && ! ipAddress.empty())
        location = ipAddress;
    else if (! serialNumber.empty())
        location = serialNumber;

    std::string name = proto + " | " + modelName;
    if (! location.empty())
        name += " | " + location;
    if (! serialNumber.empty() && location != serialNumber)
        name += " (" + serialNumber + ")";

    return name;
}

} // namespace camera_wrapper
