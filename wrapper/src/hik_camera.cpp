#include "hik_camera.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <thread>

namespace camera_wrapper {

// ======================================================================== //
//  Helpers: SDK type → wrapper type conversions                             //
// ======================================================================== //

static InterfaceType sdkLayerToInterfaceType(unsigned int nTLayerType) {
    switch (nTLayerType) {
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

static unsigned int interfaceTypeToSdkLayer(InterfaceType t) {
    switch (t) {
        case InterfaceType::GigE:
            return MV_GIGE_DEVICE;
        case InterfaceType::USB3:
            return MV_USB_DEVICE;
        case InterfaceType::CameraLink:
            return MV_CAMERALINK_DEVICE;
        default:
            return MV_UNKNOW_DEVICE;
    }
}

/// Build a DeviceInfo from an SDK MV_CC_DEVICE_INFO struct.
static DeviceInfo buildDeviceInfo(const MV_CC_DEVICE_INFO& sdk) {
    DeviceInfo info;
    info.interfaceType = sdkLayerToInterfaceType(sdk.nTLayerType);

    switch (sdk.nTLayerType) {
        case MV_GIGE_DEVICE: {
            const auto& g = sdk.SpecialInfo.stGigEInfo;
            info.serialNumber = reinterpret_cast<const char*>(g.chSerialNumber);
            info.modelName = reinterpret_cast<const char*>(g.chModelName);
            info.vendor = reinterpret_cast<const char*>(g.chManufacturerName);
            info.userDefinedName = reinterpret_cast<const char*>(g.chUserDefinedName);

            // Format IP address from 32-bit integer (big-endian).
            unsigned int ip = g.nCurrentIp;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                          (ip >> 8) & 0xFF, ip & 0xFF);
            info.ipAddress = buf;

            unsigned int sn = g.nCurrentSubNetMask;
            std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (sn >> 24) & 0xFF, (sn >> 16) & 0xFF,
                          (sn >> 8) & 0xFF, sn & 0xFF);
            info.subnetMask = buf;

            unsigned int gw = g.nDefultGateWay;
            std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (gw >> 24) & 0xFF, (gw >> 16) & 0xFF,
                          (gw >> 8) & 0xFF, gw & 0xFF);
            info.defaultGateway = buf;

            // MAC address: nMacAddrHigh and nMacAddrLow are fields of the
            // outer MV_CC_DEVICE_INFO struct, not of MV_GIGE_DEVICE_INFO.
            std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                          (sdk.nMacAddrHigh >> 8) & 0xFF, sdk.nMacAddrHigh & 0xFF,
                          (sdk.nMacAddrLow >> 24) & 0xFF, (sdk.nMacAddrLow >> 16) & 0xFF,
                          (sdk.nMacAddrLow >> 8) & 0xFF, sdk.nMacAddrLow & 0xFF);
            info.macAddress = buf;
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

// ======================================================================== //
//  Constructor / Destructor                                                  //
// ======================================================================== //

HikCamera::HikCamera(const DeviceInfo& info)
    : devInfo_(info) {
    std::memset(&sdkDevInfo_, 0, sizeof(sdkDevInfo_));
}

HikCamera::~HikCamera() {
    // Signal reconnect thread to stop before joining.
    reconnectStop_.store(true);
    {
        std::lock_guard<std::mutex> lk(reconnectMutex_);
        reconnectPending_ = true; // wake the thread so it can exit
    }
    reconnectCv_.notify_all();
    if (reconnectThread_.joinable())
        reconnectThread_.join();

    // Close the device if still open.
    if (handle_) {
        if (grabbing_.load())
            stopGrabbingInternal();
        MV_CC_CloseDevice(handle_);
        MV_CC_DestroyHandle(handle_);
        handle_ = nullptr;
    }
}

// ======================================================================== //
//  Camera – Lifecycle                                                       //
// ======================================================================== //

bool HikCamera::open() {
    if (handle_) {
        std::cerr << "[HikCamera] open() called but handle already exists\n";
        return false;
    }

    // We need to find the SDK device info struct for this DeviceInfo.
    // Enumerate and match by serial number.
    unsigned int layerMask = interfaceTypeToSdkLayer(devInfo_.interfaceType);
    if (layerMask == MV_UNKNOW_DEVICE)
        layerMask = MV_GIGE_DEVICE | MV_USB_DEVICE | MV_CAMERALINK_DEVICE;

    MV_CC_DEVICE_INFO_LIST list{};
    if (MV_CC_EnumDevices(layerMask, &list) != MV_OK || list.nDeviceNum == 0) {
        std::cerr << "[HikCamera] No devices found during open()\n";
        return false;
    }

    bool found = false;
    for (unsigned int i = 0; i < list.nDeviceNum; ++i) {
        DeviceInfo candidate = buildDeviceInfo(*list.pDeviceInfo[i]);
        if (candidate.serialNumber == devInfo_.serialNumber) {
            sdkDevInfo_ = *list.pDeviceInfo[i];
            found = true;
            break;
        }
    }

    if (! found) {
        std::cerr << "[HikCamera] Device SN=" << devInfo_.serialNumber
                  << " not found during open()\n";
        return false;
    }

    void* h = nullptr;
    if (MV_CC_CreateHandle(&h, &sdkDevInfo_) != MV_OK) {
        std::cerr << "[HikCamera] MV_CC_CreateHandle failed\n";
        return false;
    }

    if (MV_CC_OpenDevice(h) != MV_OK) {
        std::cerr << "[HikCamera] MV_CC_OpenDevice failed\n";
        MV_CC_DestroyHandle(h);
        return false;
    }

    handle_ = h;

    // Register SDK exception callback.
    MV_CC_RegisterExceptionCallBack(handle_, sdkExceptionCallback, this);

    connState_.store(ConnectionState::Connected);
    statusCbManager_.notifyAll({CameraStatus::Connected, "Camera opened", 0});

    std::clog << "[HikCamera] Opened SN=" << devInfo_.serialNumber << '\n';

    // Start reconnect thread (it will sleep until needed).
    if (! reconnectThread_.joinable()) {
        reconnectStop_.store(false);
        reconnectThread_ = std::thread(&HikCamera::reconnectLoop, this);
    }

    return true;
}

bool HikCamera::close() {
    // Signal reconnect thread to stop.
    reconnectStop_.store(true);
    {
        std::lock_guard<std::mutex> lk(reconnectMutex_);
        reconnectPending_ = true;
    }
    reconnectCv_.notify_all();
    if (reconnectThread_.joinable())
        reconnectThread_.join();

    if (! handle_)
        return true; // already closed

    if (grabbing_.load())
        stopGrabbingInternal();

    MV_CC_RegisterExceptionCallBack(handle_, nullptr, nullptr);
    MV_CC_RegisterImageCallBackEx(handle_, nullptr, nullptr);

    MV_CC_CloseDevice(handle_);
    MV_CC_DestroyHandle(handle_);
    handle_ = nullptr;

    connState_.store(ConnectionState::Disconnected);
    statusCbManager_.notifyAll({CameraStatus::Disconnected, "Camera closed", 0});

    std::clog << "[HikCamera] Closed SN=" << devInfo_.serialNumber << '\n';
    return true;
}

bool HikCamera::isOpened() const {
    if (! handle_)
        return false;
    return MV_CC_IsDeviceConnected(handle_);
}

// ======================================================================== //
//  Camera – Transport configuration                                         //
// ======================================================================== //

bool HikCamera::applyTransportConfig(const TransportConfig& cfg) {
    if (! handle_)
        return false;

    // Save to desired state.
    {
        std::lock_guard<std::mutex> lk(desiredMutex_);
        desired_.transportConfig = cfg;
    }

    bool ok = true;
    if (devInfo_.interfaceType == InterfaceType::GigE)
        ok = applyGigETransport(cfg.gige);
    else if (devInfo_.interfaceType == InterfaceType::USB3)
        ok = applyUSB3Transport(cfg.usb3);

    return ok;
}

bool HikCamera::applyGigETransport(const GigETransportConfig& cfg) {
    if (! handle_)
        return false;

    // Packet size: 0 means auto-detect.
    int pktSize = cfg.packetSize;
    if (pktSize <= 0)
        pktSize = MV_CC_GetOptimalPacketSize(handle_);

    if (pktSize > 0) {
        if (MV_CC_SetIntValue(handle_, "GevSCPSPacketSize", pktSize) != MV_OK)
            std::cerr << "[HikCamera] Failed to set GevSCPSPacketSize\n";
    }

    // Heartbeat timeout.
    {
        MVCC_INTVALUE_EX hb{};
        if (MV_CC_GetIntValueEx(handle_, "GevHeartbeatTimeout", &hb) == MV_OK) {
            int64_t val = std::max((int64_t) hb.nMin,
                                   std::min((int64_t) cfg.heartbeatTimeoutMs, (int64_t) hb.nMax));
            MV_CC_SetIntValueEx(handle_, "GevHeartbeatTimeout", val);
        }
    }

    // Inter-packet delay.
    if (cfg.interPacketDelayNs > 0) {
        MVCC_INTVALUE_EX ipd{};
        if (MV_CC_GetIntValueEx(handle_, "GevSCPD", &ipd) == MV_OK) {
            int64_t val = std::max((int64_t) ipd.nMin,
                                   std::min((int64_t) cfg.interPacketDelayNs, (int64_t) ipd.nMax));
            MV_CC_SetIntValueEx(handle_, "GevSCPD", val);
        }
    }

    return true;
}

bool HikCamera::applyUSB3Transport(const USB3TransportConfig& cfg) {
    if (! handle_)
        return false;

    if (MV_CC_SetIntValue(handle_, "StreamTransferSize", cfg.transferSize) != MV_OK)
        std::cerr << "[HikCamera] Failed to set StreamTransferSize\n";

    if (MV_CC_SetIntValue(handle_, "StreamTransferCount", cfg.transferCount) != MV_OK)
        std::cerr << "[HikCamera] Failed to set StreamTransferCount\n";

    return true;
}

// ======================================================================== //
//  Camera – Camera parameters                                               //
// ======================================================================== //

bool HikCamera::setExposureTime(double us) {
    if (! handle_)
        return false;

    MVCC_FLOATVALUE fv{};
    if (MV_CC_GetFloatValue(handle_, "ExposureTime", &fv) != MV_OK)
        return false;

    float val = static_cast<float>(std::max((double) fv.fMin, std::min(us, (double) fv.fMax)));

    bool ok = (MV_CC_SetFloatValue(handle_, "ExposureTime", val) == MV_OK);
    if (ok) {
        std::lock_guard<std::mutex> lk(desiredMutex_);
        desired_.exposureUs = us;
    }
    return ok;
}

bool HikCamera::getExposureTime(double& us, double& minUs, double& maxUs) {
    if (! handle_)
        return false;

    MVCC_FLOATVALUE fv{};
    if (MV_CC_GetFloatValue(handle_, "ExposureTime", &fv) != MV_OK)
        return false;

    us = fv.fCurValue;
    minUs = fv.fMin;
    maxUs = fv.fMax;
    return true;
}

bool HikCamera::setGain(double dB) {
    if (! handle_)
        return false;

    MVCC_FLOATVALUE fv{};
    if (MV_CC_GetFloatValue(handle_, "Gain", &fv) != MV_OK)
        return false;

    float val = static_cast<float>(std::max((double) fv.fMin, std::min(dB, (double) fv.fMax)));

    bool ok = (MV_CC_SetFloatValue(handle_, "Gain", val) == MV_OK);
    if (ok) {
        std::lock_guard<std::mutex> lk(desiredMutex_);
        desired_.gainDb = dB;
    }
    return ok;
}

bool HikCamera::getGain(double& dB, double& minDb, double& maxDb) {
    if (! handle_)
        return false;

    MVCC_FLOATVALUE fv{};
    if (MV_CC_GetFloatValue(handle_, "Gain", &fv) != MV_OK)
        return false;

    dB = fv.fCurValue;
    minDb = fv.fMin;
    maxDb = fv.fMax;
    return true;
}

// ======================================================================== //
//  Camera – Acquisition control                                             //
// ======================================================================== //

bool HikCamera::configure(const GrabConfig& cfg) {
    {
        std::lock_guard<std::mutex> lk(desiredMutex_);
        desired_.grabConfig = cfg;
    }

    if (! handle_)
        return true; // Stored; will be applied on next open/reconnect.

    return applyTriggerSettings(cfg);
}

bool HikCamera::applyTriggerSettings(const GrabConfig& cfg) {
    if (! handle_)
        return false;

    bool useTrigger = (cfg.mode == GrabMode::SnapSync || cfg.mode == GrabMode::TriggerCallback);

    // Set trigger mode.
    MV_CC_SetEnumValue(handle_, "TriggerMode",
                       useTrigger ? MV_TRIGGER_MODE_ON : MV_TRIGGER_MODE_OFF);

    if (useTrigger) {
        // Map TriggerSource enum to SDK enum.
        unsigned int sdkSrc = MV_TRIGGER_SOURCE_SOFTWARE;
        switch (cfg.triggerSource) {
            case TriggerSource::Software:
                sdkSrc = MV_TRIGGER_SOURCE_SOFTWARE;
                break;
            case TriggerSource::Line0:
                sdkSrc = MV_TRIGGER_SOURCE_LINE0;
                break;
            case TriggerSource::Line1:
                sdkSrc = MV_TRIGGER_SOURCE_LINE1;
                break;
            case TriggerSource::Line2:
                sdkSrc = MV_TRIGGER_SOURCE_LINE2;
                break;
            case TriggerSource::Line3:
                sdkSrc = MV_TRIGGER_SOURCE_LINE3;
                break;
        }
        MV_CC_SetEnumValue(handle_, "TriggerSource", sdkSrc);
    }

    return true;
}

bool HikCamera::startGrabbing() {
    if (! handle_)
        return false;

    // Read desired config.
    GrabConfig cfg;
    {
        std::lock_guard<std::mutex> lk(desiredMutex_);
        cfg = desired_.grabConfig;
        desired_.grabbing = true;
    }

    applyTriggerSettings(cfg);

    bool ok = startGrabbingInternal();
    if (ok) {
        std::lock_guard<std::mutex> lk(stateMutex_);
        currentMode_ = cfg.mode;
    }
    return ok;
}

bool HikCamera::startGrabbingInternal() {
    if (! handle_)
        return false;

    GrabConfig cfg;
    {
        std::lock_guard<std::mutex> lk(desiredMutex_);
        cfg = desired_.grabConfig;
    }

    // Register (or clear) the SDK image callback depending on mode.
    if (cfg.mode == GrabMode::TriggerCallback || cfg.mode == GrabMode::StreamCallback) {
        int ret = MV_CC_RegisterImageCallBackEx(handle_, sdkFrameCallback, this);
    } else {
        // SnapSync: no SDK callback; frames are fetched synchronously.
        MV_CC_RegisterImageCallBackEx(handle_, nullptr, nullptr);
    }

    int ret = MV_CC_StartGrabbing(handle_);
    if (ret != MV_OK) {
        std::cerr << "[HikCamera] MV_CC_StartGrabbing failed: " << ret << '\n';
        return false;
    }

    grabbing_.store(true);
    return true;
}

bool HikCamera::stopGrabbing() {
    {
        std::lock_guard<std::mutex> lk(desiredMutex_);
        desired_.grabbing = false;
    }
    stopGrabbingInternal();
    return true;
}

void HikCamera::stopGrabbingInternal() {
    if (! grabbing_.load())
        return;

    if (handle_) {
        MV_CC_RegisterImageCallBackEx(handle_, nullptr, nullptr);
        MV_CC_StopGrabbing(handle_);
    }

    grabbing_.store(false);
}

bool HikCamera::isGrabbing() const {
    return grabbing_.load();
}

GrabMode HikCamera::currentMode() const {
    std::lock_guard<std::mutex> lk(stateMutex_);
    return currentMode_;
}

bool HikCamera::switchMode(const GrabConfig& cfg) {
    // Prevent concurrent switchMode() calls.
    bool expected = false;
    if (! switching_.compare_exchange_strong(expected, true)) {
        std::cerr << "[HikCamera] switchMode() already in progress\n";
        return false;
    }

    // RAII guard to clear switching_ on exit.
    struct Guard {
        std::atomic<bool>& flag;
        ~Guard() {
            flag.store(false);
        }
    } guard{switching_};

    // 1. Stop current acquisition (safe even if already stopped).
    stopGrabbingInternal();

    // 2. Update desired state.
    {
        std::lock_guard<std::mutex> lk(desiredMutex_);
        desired_.grabConfig = cfg;
        desired_.grabbing = true;
    }

    // 3. Apply trigger settings (SDK call – no lock held).
    if (! applyTriggerSettings(cfg))
        return false;

    // 4. Start grabbing with new mode.
    bool ok = startGrabbingInternal();
    if (ok) {
        std::lock_guard<std::mutex> lk(stateMutex_);
        currentMode_ = cfg.mode;
    }
    return ok;
}

// ======================================================================== //
//  Camera – Synchronous grab                                                //
// ======================================================================== //

std::optional<ImageFrame> HikCamera::snapSync() {
    if (! handle_ || ! grabbing_.load())
        return std::nullopt;

    GrabConfig cfg;
    {
        std::lock_guard<std::mutex> lk(desiredMutex_);
        cfg = desired_.grabConfig;
    }

    int retries = std::max(1, cfg.snapRetryCount);

    for (int attempt = 0; attempt < retries; ++attempt) {
        // Fire software trigger if configured.
        if (cfg.triggerSource == TriggerSource::Software) {
            if (MV_CC_SetCommandValue(handle_, "TriggerSoftware") != MV_OK)
                std::cerr << "[HikCamera] TriggerSoftware command failed\n";
        }

        MV_FRAME_OUT frame{};
        int ret = MV_CC_GetImageBuffer(handle_, &frame, cfg.snapTimeoutMs);
        if (ret != MV_OK) {
            std::cerr << "[HikCamera] GetImageBuffer failed: " << ret << '\n';
            continue;
        }

        ImageFrame result = convertToFrame(frame.pBufAddr, &frame.stFrameInfo);
        MV_CC_FreeImageBuffer(handle_, &frame);

        if (result.lostPackets == 0 || attempt == retries - 1)
            return result;
    }

    return std::nullopt;
}

// ======================================================================== //
//  Camera – Software trigger                                                //
// ======================================================================== //

bool HikCamera::sendSoftTrigger() {
    if (! handle_)
        return false;
    return MV_CC_SetCommandValue(handle_, "TriggerSoftware") == MV_OK;
}

// ======================================================================== //
//  Camera – Callback registration                                           //
// ======================================================================== //

FrameCallbackManager::CallbackId
HikCamera::registerFrameCallback(FrameCallbackManager::Callback cb) {
    return frameCbManager_.registerCallback(std::move(cb));
}

void HikCamera::unregisterFrameCallback(FrameCallbackManager::CallbackId id) {
    frameCbManager_.unregisterCallback(id);
}

StatusCallbackManager::CallbackId
HikCamera::registerStatusCallback(StatusCallbackManager::Callback cb) {
    return statusCbManager_.registerCallback(std::move(cb));
}

void HikCamera::unregisterStatusCallback(StatusCallbackManager::CallbackId id) {
    statusCbManager_.unregisterCallback(id);
}

// ======================================================================== //
//  Camera – Connection state / device info                                  //
// ======================================================================== //

ConnectionState HikCamera::connectionState() const {
    return connState_.load();
}

DeviceInfo HikCamera::deviceInfo() const {
    return devInfo_;
}

// ======================================================================== //
//  SDK static callbacks                                                      //
// ======================================================================== //

void HikCamera::sdkFrameCallback(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo,
                                 void* pUser) {
    if (! pData || ! pFrameInfo || ! pUser)
        return;

    auto* self = static_cast<HikCamera*>(pUser);

    // Convert SDK buffer → ImageFrame (one deep copy here).
    ImageFrame frame = convertToFrame(pData, pFrameInfo);

    // Notify all subscribers (each subscriber does a shallow copy into its queue).
    self->frameCbManager_.notifyAll(frame);
}

void HikCamera::sdkExceptionCallback(unsigned int nMsgType, void* pUser) {
    if (! pUser)
        return;

    auto* self = static_cast<HikCamera*>(pUser);
    std::cerr << "[HikCamera] SDK exception: 0x" << std::hex << nMsgType << std::dec << '\n';

    if (nMsgType == MV_EXCEPTION_DEV_DISCONNECT) {
        self->onDisconnected();
    }
}

// ======================================================================== //
//  Frame conversion (reused from original mvcamera.cpp)                     //
// ======================================================================== //

ImageFrame HikCamera::convertToFrame(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pInfo) {
    ImageFrame result;
    result.frameId = pInfo->nFrameNum;
    result.timestampNs = pInfo->nDevTimeStampHigh;
    result.timestampNs = (result.timestampNs << 32) | pInfo->nDevTimeStampLow;
    result.width = pInfo->nWidth;
    result.height = pInfo->nHeight;
    result.lostPackets = pInfo->nLostPacket;

    int h = pInfo->nHeight;
    int w = pInfo->nWidth;

    cv::Mat raw;
    switch (pInfo->enPixelType) {
        case PixelType_Gvsp_Mono8:
            raw = cv::Mat(h, w, CV_8UC1, pData);
            result.image = raw.clone();
            result.pixelFormat = PixelFormat::Mono8;
            break;

        case PixelType_Gvsp_Mono10:
            raw = cv::Mat(h, w, CV_16UC1, pData);
            result.image = raw.clone();
            result.pixelFormat = PixelFormat::Mono10;
            break;

        case PixelType_Gvsp_Mono12:
            raw = cv::Mat(h, w, CV_16UC1, pData);
            result.image = raw.clone();
            result.pixelFormat = PixelFormat::Mono12;
            break;

        case PixelType_Gvsp_YUV422_Packed:
            raw = cv::Mat(h, w, CV_8UC2, pData);
            cv::cvtColor(raw, result.image, cv::COLOR_YUV2BGR_UYVY);
            result.pixelFormat = PixelFormat::YUV422_UYVY;
            break;

        case PixelType_Gvsp_YUV422_YUYV_Packed:
            raw = cv::Mat(h, w, CV_8UC2, pData);
            cv::cvtColor(raw, result.image, cv::COLOR_YUV2BGR_YUYV);
            result.pixelFormat = PixelFormat::YUV422_YUYV;
            break;

        case PixelType_Gvsp_BGR8_Packed:
            raw = cv::Mat(h, w, CV_8UC3, pData);
            result.image = raw.clone();
            result.pixelFormat = PixelFormat::BGR8;
            break;

        case PixelType_Gvsp_RGB8_Packed:
            raw = cv::Mat(h, w, CV_8UC3, pData);
            cv::cvtColor(raw, result.image, cv::COLOR_RGB2BGR);
            result.pixelFormat = PixelFormat::RGB8;
            break;

        case PixelType_Gvsp_BayerRG8:
            raw = cv::Mat(h, w, CV_8UC1, pData);
            cv::cvtColor(raw, result.image, cv::COLOR_BayerRG2BGR);
            result.pixelFormat = PixelFormat::BayerRG8;
            break;

        case PixelType_Gvsp_BayerRG12:
            raw = cv::Mat(h, w, CV_16UC1, pData);
            cv::cvtColor(raw, result.image, cv::COLOR_BayerRG2BGR);
            result.pixelFormat = PixelFormat::BayerRG12;
            break;

        case PixelType_Gvsp_BayerRG16:
            raw = cv::Mat(h, w, CV_16UC1, pData);
            cv::cvtColor(raw, result.image, cv::COLOR_BayerRG2BGR);
            result.pixelFormat = PixelFormat::BayerRG16;
            break;

        default:
            result.image = cv::Mat::zeros(h, w, CV_8UC1);
            std::cerr << "[HikCamera] Unsupported pixel format: " << pInfo->enPixelType << '\n';
            break;
    }

    return result;
}

// ======================================================================== //
//  Auto-reconnect                                                            //
// ======================================================================== //

void HikCamera::onDisconnected() {
    connState_.store(ConnectionState::Disconnected);
    statusCbManager_.notifyAll({CameraStatus::Disconnected, "Camera disconnected", 0});

    // Stop grabbing state (SDK handle is no longer valid).
    grabbing_.store(false);

    // Signal the reconnect thread.
    {
        std::lock_guard<std::mutex> lk(reconnectMutex_);
        reconnectPending_ = true;
    }
    reconnectCv_.notify_one();
}

void HikCamera::reconnectLoop() {
    while (! reconnectStop_.load()) {
        // Wait until a reconnect is needed.
        {
            std::unique_lock<std::mutex> lk(reconnectMutex_);
            reconnectCv_.wait(lk, [this] { return reconnectPending_ || reconnectStop_.load(); });
            reconnectPending_ = false;
        }

        if (reconnectStop_.load())
            break;

        // Destroy the old handle (if any).
        if (handle_) {
            MV_CC_RegisterExceptionCallBack(handle_, nullptr, nullptr);
            MV_CC_RegisterImageCallBackEx(handle_, nullptr, nullptr);
            MV_CC_CloseDevice(handle_);
            MV_CC_DestroyHandle(handle_);
            handle_ = nullptr;
        }

        int retryCount = 0;
        while (! reconnectStop_.load()) {
            connState_.store(ConnectionState::Reconnecting);
            statusCbManager_.notifyAll(
                {CameraStatus::Reconnecting, "Attempting reconnect", retryCount});

            // Try to enumerate and find the device.
            unsigned int layerMask = interfaceTypeToSdkLayer(devInfo_.interfaceType);
            if (layerMask == MV_UNKNOW_DEVICE)
                layerMask = MV_GIGE_DEVICE | MV_USB_DEVICE | MV_CAMERALINK_DEVICE;

            MV_CC_DEVICE_INFO_LIST list{};
            bool found = false;

            if (MV_CC_EnumDevices(layerMask, &list) == MV_OK) {
                for (unsigned int i = 0; i < list.nDeviceNum; ++i) {
                    DeviceInfo candidate = buildDeviceInfo(*list.pDeviceInfo[i]);
                    if (candidate.serialNumber == devInfo_.serialNumber) {
                        sdkDevInfo_ = *list.pDeviceInfo[i];
                        found = true;
                        break;
                    }
                }
            }

            if (found) {
                void* h = nullptr;
                if (MV_CC_CreateHandle(&h, &sdkDevInfo_) == MV_OK && MV_CC_OpenDevice(h) == MV_OK) {
                    handle_ = h;
                    MV_CC_RegisterExceptionCallBack(handle_, sdkExceptionCallback, this);

                    // Restore desired state.
                    DesiredState ds;
                    {
                        std::lock_guard<std::mutex> lk(desiredMutex_);
                        ds = desired_;
                    }

                    // Apply transport config.
                    if (ds.transportConfig.gige.heartbeatTimeoutMs > 0 ||
                        ds.transportConfig.gige.packetSize > 0) {
                        applyGigETransport(ds.transportConfig.gige);
                    }

                    // Apply camera parameters.
                    if (ds.exposureUs > 0)
                        setExposureTime(ds.exposureUs);
                    if (ds.gainDb >= 0)
                        setGain(ds.gainDb);

                    // Restore grab mode.
                    if (ds.grabbing) {
                        applyTriggerSettings(ds.grabConfig);
                        startGrabbingInternal();
                        {
                            std::lock_guard<std::mutex> lk(stateMutex_);
                            currentMode_ = ds.grabConfig.mode;
                        }
                    }

                    connState_.store(ConnectionState::Connected);
                    statusCbManager_.notifyAll(
                        {CameraStatus::Reconnected, "Camera reconnected", retryCount});
                    std::clog << "[HikCamera] Reconnected SN=" << devInfo_.serialNumber << '\n';
                    break; // Success – exit retry loop.
                } else {
                    if (h) {
                        MV_CC_DestroyHandle(h);
                        h = nullptr;
                    }
                }
            }

            ++retryCount;
            if (reconnectMaxRetries_ > 0 && retryCount >= reconnectMaxRetries_) {
                statusCbManager_.notifyAll(
                    {CameraStatus::Error, "Max reconnect retries reached", retryCount});
                return;
            }

            // Wait before next attempt.
            std::unique_lock<std::mutex> lk(reconnectMutex_);
            reconnectCv_.wait_for(lk, std::chrono::milliseconds(reconnectIntervalMs_),
                                  [this] { return reconnectStop_.load(); });
        }
    }
}

} // namespace camera_wrapper
