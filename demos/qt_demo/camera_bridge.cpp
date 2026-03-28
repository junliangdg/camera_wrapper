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

CameraBridge::CameraBridge(QObject* parent)
    : QObject(parent)
    , enumerator_(createEnumerator())
    , previewQueue_(std::make_unique<FrameQueue>(1, OverflowPolicy::DropAll)) {}

CameraBridge::~CameraBridge() {
    closeCamera();
}

// ------------------------------------------------------------------ //
//  Device management                                                   //
// ------------------------------------------------------------------ //

QVector<QString> CameraBridge::enumerateDevices() {
    deviceList_ = enumerator_->enumerate();
    QVector<QString> names;
    names.reserve(static_cast<int>(deviceList_.size()));
    for (const auto& d : deviceList_)
        names.append(QString::fromStdString(d.displayName()));
    return names;
}

bool CameraBridge::openCamera(int index) {
    if (index < 0 || index >= static_cast<int>(deviceList_.size()))
        return false;

    closeCamera(); // close any previously open camera

    camera_ = enumerator_->createCamera(deviceList_[index]);
    if (! camera_)
        return false;

    // Register status callback – invoked from the reconnect/SDK thread.
    // Use QueuedConnection to marshal to the Qt main thread.
    statusCbId_ =
        camera_->registerStatusCallback([this](const StatusEvent& ev) { onStatusEvent(ev); });

    if (! camera_->open()) {
        camera_.reset();
        return false;
    }

    // Apply default transport config.
    TransportConfig transport;
    transport.gige.packetSize = 0; // auto-detect
    transport.gige.heartbeatTimeoutMs = 3000;
    camera_->applyTransportConfig(transport);

    // Register frame callback – invoked from the SDK callback thread.
    frameCbId_ =
        camera_->registerFrameCallback([this](const ImageFrame& frame) { onFrameArrived(frame); });

    // Start in StreamCallback mode by default.
    GrabConfig cfg;
    cfg.mode = GrabMode::StreamCallback;
    camera_->configure(cfg);
    camera_->startGrabbing();

    return true;
}

void CameraBridge::closeCamera() {
    if (! camera_)
        return;

    if (frameCbId_ >= 0) {
        camera_->unregisterFrameCallback(frameCbId_);
        frameCbId_ = -1;
    }
    if (statusCbId_ >= 0) {
        camera_->unregisterStatusCallback(statusCbId_);
        statusCbId_ = -1;
    }

    camera_->close();
    camera_.reset();
    previewQueue_->clear();
}

bool CameraBridge::isCameraOpen() const {
    return camera_ && camera_->isOpened();
}

// ------------------------------------------------------------------ //
//  Acquisition control                                                 //
// ------------------------------------------------------------------ //

bool CameraBridge::switchMode(GrabMode mode, TriggerSource triggerSource) {
    if (! camera_)
        return false;

    GrabConfig cfg;
    cfg.mode = mode;
    cfg.triggerSource = triggerSource;
    return camera_->switchMode(cfg);
}

bool CameraBridge::sendSoftTrigger() {
    if (! camera_)
        return false;

    // In SnapSync mode the SDK fires no frame callback.
    // We must call snapSync() ourselves to retrieve the frame.
    // Do it on a worker thread so the Qt main thread is never blocked.
    if (camera_->currentMode() == GrabMode::SnapSync) {
        // Prevent overlapping snap calls (one at a time).
        bool expected = false;
        if (! snapRunning_.compare_exchange_strong(expected, true))
            return false; // previous snap still in flight – ignore

        // snapSync() internally fires the software trigger and blocks until
        // the frame arrives (or times out).  Run it off the main thread.
        Camera* cam = camera_.get();
        QThreadPool::globalInstance()->start([this, cam]() {
            auto result = cam->snapSync();

            // Always clear the flag before returning.
            snapRunning_.store(false);

            if (! result)
                return; // timeout or error – nothing to display

            // Push into the preview queue so statistics are updated.
            previewQueue_->push(*result);
            // Pop immediately – we are the sole consumer.
            auto popped = previewQueue_->pop(0);

            // Marshal the frame to the Qt main thread.
            cv::Mat img = popped ? popped->image : result->image;
            quint64 fid = result->frameId;
            quint64 ts = result->timestampNs;

            QMetaObject::invokeMethod(
                this,
                [this, img, fid, ts]() {
                    emit frameReady(img, fid, ts);

                    auto s = previewQueue_->stats();
                    emit queueStatsUpdated(s.totalPushed, s.totalPopped, s.dropCount);
                },
                Qt::QueuedConnection);
        });

        return true; // trigger dispatched asynchronously
    }

    // TriggerCallback / StreamCallback: just fire the trigger;
    // the SDK callback will deliver the frame via onFrameArrived().
    return camera_->sendSoftTrigger();
}

// ------------------------------------------------------------------ //
//  Camera parameters                                                   //
// ------------------------------------------------------------------ //

bool CameraBridge::setExposureTime(double us) {
    return camera_ && camera_->setExposureTime(us);
}

bool CameraBridge::getExposureTime(double& us, double& minUs, double& maxUs) {
    return camera_ && camera_->getExposureTime(us, minUs, maxUs);
}

bool CameraBridge::setGain(double dB) {
    return camera_ && camera_->setGain(dB);
}

bool CameraBridge::getGain(double& dB, double& minDb, double& maxDb) {
    return camera_ && camera_->getGain(dB, minDb, maxDb);
}

// ------------------------------------------------------------------ //
//  Statistics                                                          //
// ------------------------------------------------------------------ //

FrameQueueStats CameraBridge::queueStats() const {
    return previewQueue_->stats();
}

GrabMode CameraBridge::currentMode() const {
    if (! camera_)
        return GrabMode::StreamCallback;
    return camera_->currentMode();
}

// ------------------------------------------------------------------ //
//  Private: frame callback (SDK callback thread)                       //
// ------------------------------------------------------------------ //

void CameraBridge::onFrameArrived(const ImageFrame& frame) {
    // Push into the preview queue (shallow copy – O(1)).
    // The queue has capacity 1 with DropAll policy, so it always holds the
    // most recent frame.  We are the sole consumer, so pop() immediately
    // after push() to keep totalPopped in sync with totalPushed.
    previewQueue_->push(frame);
    auto popped = previewQueue_->pop(0); // non-blocking; should always succeed

    // Use the popped frame for display (falls back to the original if the
    // pop somehow raced and returned nullopt, which cannot happen here but
    // is handled defensively).
    cv::Mat img = popped ? popped->image : frame.image; // shallow copy
    quint64 fid = frame.frameId;
    quint64 ts = frame.timestampNs;

    // Marshal to the Qt main thread via QueuedConnection.
    QMetaObject::invokeMethod(
        this,
        [this, img, fid, ts]() {
            emit frameReady(img, fid, ts);

            auto s = previewQueue_->stats();
            emit queueStatsUpdated(s.totalPushed, s.totalPopped, s.dropCount);
        },
        Qt::QueuedConnection);
}

// ------------------------------------------------------------------ //
//  Private: status callback (reconnect / SDK thread)                   //
// ------------------------------------------------------------------ //

void CameraBridge::onStatusEvent(const StatusEvent& event) {
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
        this, [this, text]() { emit connectionStateChanged(text); }, Qt::QueuedConnection);
}
