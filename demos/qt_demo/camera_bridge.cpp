#include "camera_bridge.h"

#include "camera_wrapper/enumerator_factory.h"
#include "camera_wrapper/frame_queue.h"

#include <QMetaObject>
#include <QString>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>

using namespace camera_wrapper;

// Register cv::Mat as a Qt metatype so it can be passed through
// QueuedConnection signals.  Call this once before the event loop starts.
// (Done in main.cpp via qRegisterMetaType<cv::Mat>().)

// ======================================================================== //
//  CameraSlot                                                                //
// ======================================================================== //

CameraBridge::CameraSlot::CameraSlot()
    : previewQueue(std::make_unique<FrameQueue>(1, OverflowPolicy::DropAll)) {}

// ======================================================================== //
//  CameraBridge – Constructor / Destructor                                   //
// ======================================================================== //

CameraBridge::CameraBridge(QObject* parent)
    : QObject(parent)
    , enumerator_(createEnumerator()) {}

CameraBridge::~CameraBridge() {
    closeAllCameras();
}

// ======================================================================== //
//  Device enumeration                                                        //
// ======================================================================== //

QVector<QString> CameraBridge::enumerateDevices() {
    deviceList_ = enumerator_->enumerate();
    QVector<QString> names;
    names.reserve(static_cast<int>(deviceList_.size()));
    for (const auto& d : deviceList_)
        names.append(QString::fromStdString(d.displayName()));
    return names;
}

// ======================================================================== //
//  Per-camera lifecycle                                                      //
// ======================================================================== //

int CameraBridge::openCamera(int deviceIndex) {
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(deviceList_.size()))
        return -1;

    auto slot = std::make_unique<CameraSlot>();

    slot->camera = enumerator_->createCamera(deviceList_[deviceIndex]);
    if (! slot->camera)
        return -1;

    // Determine the camera index before registering callbacks so the lambdas
    // can capture it by value.
    int cameraIndex = -1;
    {
        std::lock_guard<std::mutex> lk(slotsMutex_);
        cameraIndex = static_cast<int>(slots_.size());
        slots_.push_back(std::move(slot)); // slot is now owned by slots_
    }

    CameraSlot* s = slots_[cameraIndex].get();

    // Register status callback – invoked from the reconnect / SDK thread.
    s->statusCbId = s->camera->registerStatusCallback(
        [this, cameraIndex](const StatusEvent& ev) { onStatusEvent(cameraIndex, ev); });

    if (! s->camera->open()) {
        // Roll back: unregister the status callback and mark slot as empty.
        s->camera->unregisterStatusCallback(s->statusCbId);
        s->statusCbId = -1;
        std::lock_guard<std::mutex> lk(slotsMutex_);
        slots_[cameraIndex].reset();
        return -1;
    }
    TransportConfig transport;
    transport.gige.packetSize = 0; // auto-detect optimal MTU
    transport.gige.heartbeatTimeoutMs = 3000;
    s->camera->applyTransportConfig(transport);

    // Register frame callback – invoked from the SDK callback thread.
    s->frameCbId = s->camera->registerFrameCallback(
        [this, cameraIndex](const ImageFrame& frame) { onFrameArrived(cameraIndex, frame); });

    // Start in StreamCallback mode by default.
    GrabConfig cfg;
    cfg.mode = GrabMode::StreamCallback;
    s->camera->configure(cfg);
    s->camera->startGrabbing();

    return cameraIndex;
}

void CameraBridge::closeCamera(int cameraIndex) {
    CameraSlot* s = slotAt(cameraIndex);
    if (! s)
        return;

    if (s->frameCbId >= 0) {
        s->camera->unregisterFrameCallback(s->frameCbId);
        s->frameCbId = -1;
    }
    if (s->statusCbId >= 0) {
        s->camera->unregisterStatusCallback(s->statusCbId);
        s->statusCbId = -1;
    }

    s->camera->close();

    // Mark slot as empty so slotAt() returns nullptr for this index.
    std::lock_guard<std::mutex> lk(slotsMutex_);
    slots_[cameraIndex].reset();
}

void CameraBridge::closeAllCameras() {
    // Collect valid indices first to avoid holding the mutex while calling
    // closeCamera() (which also acquires it).
    std::vector<int> indices;
    {
        std::lock_guard<std::mutex> lk(slotsMutex_);
        for (int i = 0; i < static_cast<int>(slots_.size()); ++i)
            if (slots_[i])
                indices.push_back(i);
    }
    for (int idx : indices)
        closeCamera(idx);
}

bool CameraBridge::isCameraOpen(int cameraIndex) const {
    const CameraSlot* s = slotAt(cameraIndex);
    return s && s->camera && s->camera->isOpened();
}

int CameraBridge::openCameraCount() const {
    std::lock_guard<std::mutex> lk(slotsMutex_);
    int count = 0;
    for (const auto& slot : slots_)
        if (slot && slot->camera)
            ++count;
    return count;
}

// ======================================================================== //
//  Per-camera acquisition control                                            //
// ======================================================================== //

bool CameraBridge::switchMode(int cameraIndex, GrabMode mode, TriggerSource triggerSource) {
    CameraSlot* s = slotAt(cameraIndex);
    if (! s)
        return false;

    GrabConfig cfg;
    cfg.mode = mode;
    cfg.triggerSource = triggerSource;
    return s->camera->switchMode(cfg);
}

bool CameraBridge::sendSoftTrigger(int cameraIndex) {
    CameraSlot* s = slotAt(cameraIndex);
    if (! s)
        return false;

    // SnapSync mode: snapSync() blocks – run it on a worker thread so the
    // Qt main thread is never stalled.
    if (s->camera->currentMode() == GrabMode::SnapSync) {
        bool expected = false;
        if (! s->snapRunning.compare_exchange_strong(expected, true))
            return false; // previous snap still in flight

        Camera* cam = s->camera.get();
        FrameQueue* queue = s->previewQueue.get();

        QThreadPool::globalInstance()->start([this, cameraIndex, cam, queue]() {
            auto result = cam->snapSync();
            slots_[cameraIndex]->snapRunning.store(false);

            if (! result)
                return;

            queue->push(*result);
            auto popped = queue->pop(0);

            cv::Mat img = popped ? popped->image : result->image;
            quint64 fid = result->frameId;
            quint64 ts = result->timestampNs;

            QMetaObject::invokeMethod(
                this,
                [this, cameraIndex, img, fid, ts, queue]() {
                    emit frameReady(cameraIndex, img, fid, ts);
                    auto st = queue->stats();
                    emit queueStatsUpdated(cameraIndex, st.totalPushed, st.totalPopped,
                                           st.dropCount);
                },
                Qt::QueuedConnection);
        });

        return true;
    }

    // TriggerCallback / StreamCallback: just fire the trigger; the SDK
    // callback will deliver the frame via onFrameArrived().
    return s->camera->sendSoftTrigger();
}

// ======================================================================== //
//  Per-camera parameter control                                              //
// ======================================================================== //

bool CameraBridge::setExposureTime(int cameraIndex, double us) {
    CameraSlot* s = slotAt(cameraIndex);
    return s && s->camera->setExposureTime(us);
}

bool CameraBridge::getExposureTime(int cameraIndex, double& us, double& minUs, double& maxUs) {
    CameraSlot* s = slotAt(cameraIndex);
    return s && s->camera->getExposureTime(us, minUs, maxUs);
}

bool CameraBridge::setGain(int cameraIndex, double dB) {
    CameraSlot* s = slotAt(cameraIndex);
    return s && s->camera->setGain(dB);
}

bool CameraBridge::getGain(int cameraIndex, double& dB, double& minDb, double& maxDb) {
    CameraSlot* s = slotAt(cameraIndex);
    return s && s->camera->getGain(dB, minDb, maxDb);
}

// ======================================================================== //
//  Per-camera statistics / state query                                       //
// ======================================================================== //

FrameQueueStats CameraBridge::queueStats(int cameraIndex) const {
    const CameraSlot* s = slotAt(cameraIndex);
    if (! s)
        return {};
    return s->previewQueue->stats();
}

GrabMode CameraBridge::currentMode(int cameraIndex) const {
    const CameraSlot* s = slotAt(cameraIndex);
    if (! s)
        return GrabMode::StreamCallback;
    return s->camera->currentMode();
}

// ======================================================================== //
//  Private helpers                                                           //
// ======================================================================== //

CameraBridge::CameraSlot* CameraBridge::slotAt(int cameraIndex) {
    std::lock_guard<std::mutex> lk(slotsMutex_);
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(slots_.size()))
        return nullptr;
    return slots_[cameraIndex].get(); // nullptr if slot was closed
}

const CameraBridge::CameraSlot* CameraBridge::slotAt(int cameraIndex) const {
    std::lock_guard<std::mutex> lk(slotsMutex_);
    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(slots_.size()))
        return nullptr;
    return slots_[cameraIndex].get();
}

// ======================================================================== //
//  Frame callback (SDK callback thread)                                      //
// ======================================================================== //

void CameraBridge::onFrameArrived(int cameraIndex, const ImageFrame& frame) {
    // slotAt() acquires slotsMutex_ – avoid holding it across the push/pop
    // by reading the queue pointer first.
    FrameQueue* queue = nullptr;
    {
        std::lock_guard<std::mutex> lk(slotsMutex_);
        if (cameraIndex < 0 || cameraIndex >= static_cast<int>(slots_.size()) ||
            ! slots_[cameraIndex])
            return;
        queue = slots_[cameraIndex]->previewQueue.get();
    }

    // Push into the per-camera preview queue (shallow copy – O(1)).
    // DropAll policy: if the queue already holds a frame, discard it and keep
    // only the newest one.  Pop immediately so totalPopped stays in sync.
    queue->push(frame);
    auto popped = queue->pop(0); // non-blocking; should always succeed

    cv::Mat img = popped ? popped->image : frame.image; // shallow copy
    quint64 fid = frame.frameId;
    quint64 ts = frame.timestampNs;

    // Marshal to the Qt main thread via QueuedConnection.
    // All cameras share the same main-thread event loop; Qt serialises the
    // events automatically – no additional locking needed.
    QMetaObject::invokeMethod(
        this,
        [this, cameraIndex, img, fid, ts, queue]() {
            emit frameReady(cameraIndex, img, fid, ts);
            auto st = queue->stats();
            emit queueStatsUpdated(cameraIndex, st.totalPushed, st.totalPopped, st.dropCount);
        },
        Qt::QueuedConnection);
}

// ======================================================================== //
//  Status callback (reconnect / SDK thread)                                  //
// ======================================================================== //

void CameraBridge::onStatusEvent(int cameraIndex, const StatusEvent& event) {
    QString text;
    switch (event.status) {
        case CameraStatus::Connected:
            text = QStringLiteral("Connected");
            break;
        case CameraStatus::Disconnected:
            text = QStringLiteral("Disconnected");
            break;
        case CameraStatus::Reconnecting:
            text = QStringLiteral("Reconnecting (attempt %1)…").arg(event.retryCount);
            break;
        case CameraStatus::Reconnected:
            text = QStringLiteral("Reconnected");
            break;
        case CameraStatus::Error:
            text = QStringLiteral("Error: %1").arg(QString::fromStdString(event.message));
            break;
    }

    QMetaObject::invokeMethod(
        this, [this, cameraIndex, text]() { emit connectionStateChanged(cameraIndex, text); },
        Qt::QueuedConnection);
}
