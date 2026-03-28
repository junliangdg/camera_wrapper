#include "main_window.h"

#include "ui_main_window.h"

#include <QDateTime>
#include <QImage>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>
#include <QTimer>
#include <opencv2/imgproc.hpp>

using namespace camera_wrapper;

// ======================================================================== //
//  Constructor / Destructor                                                  //
// ======================================================================== //

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui_(new Ui::MainWindow)
    , bridge_(new CameraBridge(this))
    , statusTimer_(new QTimer(this)) {
    ui_->setupUi(this);

    // ---------------------------------------------------------------- //
    //  Connect bridge signals                                            //
    // ---------------------------------------------------------------- //
    connect(bridge_, &CameraBridge::frameReady, this, &MainWindow::onFrameReady);
    connect(bridge_, &CameraBridge::connectionStateChanged, this,
            &MainWindow::onConnectionStateChanged);
    connect(bridge_, &CameraBridge::queueStatsUpdated, this, &MainWindow::onQueueStatsUpdated);

    // ---------------------------------------------------------------- //
    //  Connect UI controls                                               //
    // ---------------------------------------------------------------- //
    connect(ui_->refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(ui_->connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui_->disconnectButton, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);

    connect(ui_->modeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onModeChanged);
    connect(ui_->softTriggerButton, &QPushButton::clicked, this, &MainWindow::onSoftTriggerClicked);

    connect(ui_->exposureSlider, &QSlider::valueChanged, this,
            &MainWindow::onExposureSliderChanged);
    connect(ui_->exposureSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::onExposureSpinChanged);
    connect(ui_->gainSlider, &QSlider::valueChanged, this, &MainWindow::onGainSliderChanged);
    connect(ui_->gainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::onGainSpinChanged);
    connect(ui_->triggerSourceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onTriggerSourceChanged);

    // ---------------------------------------------------------------- //
    //  Status bar timer                                                  //
    // ---------------------------------------------------------------- //
    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::onStatusTimer);
    statusTimer_->start(500); // update every 500 ms

    // ---------------------------------------------------------------- //
    //  Initial state                                                     //
    // ---------------------------------------------------------------- //
    setControlsEnabled(false);
    statusBar()->showMessage("Ready – click Refresh to enumerate cameras");

    // Enumerate on startup.
    onRefreshClicked();
}

MainWindow::~MainWindow() {
    bridge_->closeCamera();
    delete ui_;
}

// ======================================================================== //
//  Device management slots                                                   //
// ======================================================================== //

void MainWindow::onRefreshClicked() {
    auto names = bridge_->enumerateDevices();
    ui_->deviceComboBox->clear();
    for (const auto& n : names)
        ui_->deviceComboBox->addItem(n);

    if (names.isEmpty())
        statusBar()->showMessage("No cameras found");
    else
        statusBar()->showMessage(QString("Found %1 camera(s)").arg(names.size()));
}

void MainWindow::onConnectClicked() {
    int idx = ui_->deviceComboBox->currentIndex();
    if (idx < 0) {
        QMessageBox::warning(this, "No camera selected", "Please select a camera from the list.");
        return;
    }

    statusBar()->showMessage("Connecting…");
    if (! bridge_->openCamera(idx)) {
        QMessageBox::critical(this, "Connection failed", "Failed to open the selected camera.");
        statusBar()->showMessage("Connection failed");
        return;
    }

    setControlsEnabled(true);
    updateParameterPanel();
    statusBar()->showMessage("Connected");
}

void MainWindow::onDisconnectClicked() {
    bridge_->closeCamera();
    setControlsEnabled(false);
    ui_->imageLabel->setText("No image");
    statusBar()->showMessage("Disconnected");
}

// ======================================================================== //
//  Acquisition mode slots                                                    //
// ======================================================================== //

void MainWindow::onModeChanged(int index) {
    if (! bridge_->isCameraOpen())
        return;

    GrabMode mode;
    switch (index) {
        case 0:
            mode = GrabMode::StreamCallback;
            break;
        case 1:
            mode = GrabMode::TriggerCallback;
            break;
        case 2:
            mode = GrabMode::SnapSync;
            break;
        default:
            return;
    }

    TriggerSource src = TriggerSource::Software;
    switch (ui_->triggerSourceComboBox->currentIndex()) {
        case 1:
            src = TriggerSource::Line0;
            break;
        case 2:
            src = TriggerSource::Line1;
            break;
        default:
            break;
    }

    bridge_->switchMode(mode, src);

    // Soft trigger button is only useful in trigger / snap modes.
    bool triggerMode = (mode == GrabMode::TriggerCallback || mode == GrabMode::SnapSync);
    ui_->softTriggerButton->setEnabled(triggerMode && src == TriggerSource::Software);
}

void MainWindow::onSoftTriggerClicked() {
    bridge_->sendSoftTrigger();
}

void MainWindow::onTriggerSourceChanged(int /*index*/) {
    // Re-apply mode with new trigger source.
    onModeChanged(ui_->modeComboBox->currentIndex());
}

// ======================================================================== //
//  Parameter slots                                                           //
// ======================================================================== //

void MainWindow::onExposureSliderChanged(int value) {
    if (updatingControls_)
        return;
    updatingControls_ = true;
    ui_->exposureSpinBox->setValue(static_cast<double>(value));
    updatingControls_ = false;
    bridge_->setExposureTime(static_cast<double>(value));
}

void MainWindow::onExposureSpinChanged(double value) {
    if (updatingControls_)
        return;
    updatingControls_ = true;
    ui_->exposureSlider->setValue(static_cast<int>(value));
    updatingControls_ = false;
    bridge_->setExposureTime(value);
}

void MainWindow::onGainSliderChanged(int value) {
    if (updatingControls_)
        return;
    updatingControls_ = true;
    double dB = value / 10.0;
    ui_->gainSpinBox->setValue(dB);
    updatingControls_ = false;
    bridge_->setGain(dB);
}

void MainWindow::onGainSpinChanged(double value) {
    if (updatingControls_)
        return;
    updatingControls_ = true;
    ui_->gainSlider->setValue(static_cast<int>(value * 10));
    updatingControls_ = false;
    bridge_->setGain(value);
}

// ======================================================================== //
//  Bridge signal handlers                                                    //
// ======================================================================== //

void MainWindow::onFrameReady(cv::Mat image, quint64 frameId, quint64 /*timestampNs*/) {
    ++frameCount_;
    lastFrameId_ = frameId;
    lastFrameTimeMs_ = QDateTime::currentMSecsSinceEpoch();

    displayFrame(image);
}

void MainWindow::onConnectionStateChanged(QString statusText) {
    statusBar()->showMessage(statusText);

    bool connected = (statusText == "Connected" || statusText == "Reconnected");
    setControlsEnabled(connected);

    if (statusText.startsWith("Reconnect")) {
        // Show reconnecting state in the image area.
        ui_->imageLabel->setText(statusText);
    }
}

void MainWindow::onQueueStatsUpdated(quint64 pushed, quint64 popped, quint64 dropped) {
    // Update status bar with queue info.
    QString msg = QString("Queue: pushed=%1 popped=%2 dropped=%3  |  FPS: %4  |  Frame#%5")
                      .arg(pushed)
                      .arg(popped)
                      .arg(dropped)
                      .arg(displayedFps_, 0, 'f', 1)
                      .arg(lastFrameId_);
    statusBar()->showMessage(msg);
}

void MainWindow::onStatusTimer() {
    // Compute approximate display FPS from frame counter.
    static quint64 lastCount = 0;
    quint64 current = frameCount_;
    displayedFps_ = (current - lastCount) * 2.0; // timer fires every 500 ms
    lastCount = current;
}

// ======================================================================== //
//  Private helpers                                                           //
// ======================================================================== //

void MainWindow::updateParameterPanel() {
    updatingControls_ = true;

    double us = 0, minUs = 0, maxUs = 0;
    if (bridge_->getExposureTime(us, minUs, maxUs)) {
        ui_->exposureSpinBox->setMinimum(minUs);
        ui_->exposureSpinBox->setMaximum(maxUs);
        ui_->exposureSpinBox->setValue(us);
        ui_->exposureSlider->setMinimum(static_cast<int>(minUs));
        ui_->exposureSlider->setMaximum(static_cast<int>(maxUs));
        ui_->exposureSlider->setValue(static_cast<int>(us));
    }

    double dB = 0, minDb = 0, maxDb = 0;
    if (bridge_->getGain(dB, minDb, maxDb)) {
        ui_->gainSpinBox->setMinimum(minDb);
        ui_->gainSpinBox->setMaximum(maxDb);
        ui_->gainSpinBox->setValue(dB);
        ui_->gainSlider->setMinimum(static_cast<int>(minDb * 10));
        ui_->gainSlider->setMaximum(static_cast<int>(maxDb * 10));
        ui_->gainSlider->setValue(static_cast<int>(dB * 10));
    }

    updatingControls_ = false;
}

void MainWindow::setControlsEnabled(bool cameraOpen) {
    ui_->connectButton->setEnabled(! cameraOpen);
    ui_->disconnectButton->setEnabled(cameraOpen);
    ui_->deviceComboBox->setEnabled(! cameraOpen);
    ui_->refreshButton->setEnabled(! cameraOpen);
    ui_->modeGroup->setEnabled(cameraOpen);
    ui_->paramGroup->setEnabled(cameraOpen);

    // Soft trigger only makes sense in trigger/snap mode with software source.
    bool triggerMode = cameraOpen && (ui_->modeComboBox->currentIndex() == 1 ||
                                      ui_->modeComboBox->currentIndex() == 2);
    ui_->softTriggerButton->setEnabled(triggerMode &&
                                       ui_->triggerSourceComboBox->currentIndex() == 0);
}

void MainWindow::displayFrame(const cv::Mat& mat) {
    if (mat.empty())
        return;

    // Convert BGR/Mono to QImage.
    QImage qimg;
    if (mat.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        qimg =
            QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888)
                .copy();
    } else if (mat.type() == CV_8UC1) {
        qimg = QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step),
                      QImage::Format_Grayscale8)
                   .copy();
    } else {
        return; // unsupported format for display
    }

    // Scale to fit the label while keeping aspect ratio.
    QPixmap pix = QPixmap::fromImage(qimg).scaled(ui_->imageLabel->size(), Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation);

    ui_->imageLabel->setPixmap(pix);
}
