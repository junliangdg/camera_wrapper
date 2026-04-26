#include "main_window.h"

#include "ui_main_window.h"

#include <QDateTime>
#include <QGridLayout>
#include <QImage>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <opencv2/imgproc.hpp>

using namespace camera_wrapper;

// ======================================================================== //
//  CameraPanel                                                               //
// ======================================================================== //

CameraPanel::CameraPanel(int cameraIndex, const QString& title, QWidget* parent)
    : QFrame(parent)
    , cameraIndex_(cameraIndex)
    , imageLabel_(new QLabel(this))
    , statusLabel_(new QLabel(this)) {
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    setMinimumSize(320, 260);

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(4, 4, 4, 4);
    vl->setSpacing(2);

    auto* titleLabel = new QLabel(title, this);
    titleLabel->setAlignment(Qt::AlignCenter);
    QFont f = titleLabel->font();
    f.setBold(true);
    titleLabel->setFont(f);
    vl->addWidget(titleLabel);

    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setText(QStringLiteral("No image"));
    imageLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    imageLabel_->setMinimumSize(300, 200);
    vl->addWidget(imageLabel_, 1);

    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setText(QStringLiteral("Connecting..."));
    vl->addWidget(statusLabel_);
}

void CameraPanel::displayFrame(const cv::Mat& mat) {
    if (mat.empty())
        return;

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
        return;
    }

    imageLabel_->setPixmap(QPixmap::fromImage(qimg).scaled(imageLabel_->size(), Qt::KeepAspectRatio,
                                                           Qt::SmoothTransformation));
}

void CameraPanel::setStatus(const QString& text) {
    statusLabel_->setText(text);
}

// ======================================================================== //
//  MainWindow – Constructor / Destructor                                     //
// ======================================================================== //

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui_(new Ui::MainWindow)
    , bridge_(new CameraBridge(this))
    , statusTimer_(new QTimer(this)) {
    ui_->setupUi(this);

    // ---------------------------------------------------------------- //
    //  Connect bridge signals (multi-camera: all carry cameraIndex)     //
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
    connect(ui_->disconnectAllButton, &QPushButton::clicked, this,
            &MainWindow::onDisconnectAllClicked);

    connect(ui_->cameraListWidget, &QListWidget::currentRowChanged, this,
            [this](int) { onCameraSelectionChanged(); });

    connect(ui_->modeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onModeChanged);
    connect(ui_->softTriggerButton, &QPushButton::clicked, this, &MainWindow::onSoftTriggerClicked);
    connect(ui_->grabOneButton, &QPushButton::clicked, this, &MainWindow::onGrabOneClicked);

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
    statusTimer_->start(500);

    // ---------------------------------------------------------------- //
    //  Initial state                                                     //
    // ---------------------------------------------------------------- //
    setControlsEnabled(false);
    statusBar()->showMessage(QStringLiteral("Ready – click Refresh to enumerate cameras"));

    onRefreshClicked();
}

MainWindow::~MainWindow() {
    bridge_->closeAllCameras();
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
        statusBar()->showMessage(QStringLiteral("No cameras found"));
    else
        statusBar()->showMessage(
            QStringLiteral("Found %1 camera(s) – select one and click Connect").arg(names.size()));
}

void MainWindow::onConnectClicked() {
    int deviceIdx = ui_->deviceComboBox->currentIndex();
    if (deviceIdx < 0) {
        QMessageBox::warning(this, QStringLiteral("No camera selected"),
                             QStringLiteral("Please select a camera from the list."));
        return;
    }

    statusBar()->showMessage(QStringLiteral("Connecting…"));
    int camIdx = bridge_->openCamera(deviceIdx);
    if (camIdx < 0) {
        QMessageBox::critical(this, QStringLiteral("Connection failed"),
                              QStringLiteral("Failed to open the selected camera."));
        statusBar()->showMessage(QStringLiteral("Connection failed"));
        return;
    }

    // Add a row to the open-cameras list.
    QString name = ui_->deviceComboBox->currentText();
    auto* item = new QListWidgetItem(QStringLiteral("[%1] %2").arg(camIdx).arg(name));
    item->setData(Qt::UserRole, camIdx);
    ui_->cameraListWidget->addItem(item);
    ui_->cameraListWidget->setCurrentItem(item);

    // Add preview panel.
    addCameraPanel(camIdx, QStringLiteral("Cam %1: %2").arg(camIdx).arg(name));

    // Initialise per-camera stats entry.
    stats_[camIdx] = CameraStats{};

    // Enable disconnect-all as soon as at least one camera is open.
    ui_->disconnectAllButton->setEnabled(true);

    updateParameterPanel();
    statusBar()->showMessage(QStringLiteral("Camera %1 connected (%2 open)")
                                 .arg(camIdx)
                                 .arg(bridge_->openCameraCount()));
}

void MainWindow::onDisconnectClicked() {
    int camIdx = selectedCameraIndex();
    if (camIdx < 0)
        return;

    bridge_->closeCamera(camIdx);

    // Remove from list widget.
    for (int r = 0; r < ui_->cameraListWidget->count(); ++r) {
        if (ui_->cameraListWidget->item(r)->data(Qt::UserRole).toInt() == camIdx) {
            delete ui_->cameraListWidget->takeItem(r);
            break;
        }
    }

    removeCameraPanel(camIdx);
    stats_.remove(camIdx);

    if (bridge_->openCameraCount() == 0) {
        ui_->disconnectAllButton->setEnabled(false);
        setControlsEnabled(false);
    }

    statusBar()->showMessage(QStringLiteral("Camera %1 disconnected (%2 still open)")
                                 .arg(camIdx)
                                 .arg(bridge_->openCameraCount()));
}

void MainWindow::onDisconnectAllClicked() {
    bridge_->closeAllCameras();
    ui_->cameraListWidget->clear();

    for (auto* panel : panels_)
        panel->deleteLater();
    panels_.clear();
    stats_.clear();

    ui_->disconnectAllButton->setEnabled(false);
    setControlsEnabled(false);
    statusBar()->showMessage(QStringLiteral("All cameras disconnected"));
}

// ======================================================================== //
//  Camera list selection                                                     //
// ======================================================================== //

void MainWindow::onCameraSelectionChanged() {
    int camIdx = selectedCameraIndex();
    setControlsEnabled(camIdx >= 0);
    if (camIdx < 0)
        return;

    updateParameterPanel();

    // Reflect the current mode of the selected camera.
    GrabMode mode = bridge_->currentMode(camIdx);
    updatingControls_ = true;
    switch (mode) {
        case GrabMode::StreamCallback:
            ui_->modeComboBox->setCurrentIndex(0);
            break;
        case GrabMode::TriggerCallback:
            ui_->modeComboBox->setCurrentIndex(1);
            break;
        case GrabMode::SnapSync:
            ui_->modeComboBox->setCurrentIndex(2);
            break;
    }
    updatingControls_ = false;
}

// ======================================================================== //
//  Acquisition mode slots                                                    //
// ======================================================================== //

void MainWindow::onModeChanged(int index) {
    if (updatingControls_)
        return;

    int camIdx = selectedCameraIndex();
    if (camIdx < 0)
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

    bridge_->switchMode(camIdx, mode, src);

    bool triggerMode = (mode == GrabMode::TriggerCallback || mode == GrabMode::SnapSync);
    ui_->softTriggerButton->setEnabled(triggerMode && src == TriggerSource::Software);
}

void MainWindow::onSoftTriggerClicked() {
    int camIdx = selectedCameraIndex();
    if (camIdx >= 0)
        bridge_->sendSoftTrigger(camIdx);
}

void MainWindow::onGrabOneClicked() {
    int camIdx = selectedCameraIndex();
    if (camIdx < 0)
        return;

    // Disable the button while the async grab is in flight to prevent double
    // clicks.  It will be re-enabled via the frameReady signal path or after a
    // short timeout.  Using a simple one-shot approach: just call it and show
    // the current mode in the status bar so the user knows what happened.
    ui_->grabOneButton->setEnabled(false);

    GrabMode mode = bridge_->currentMode(camIdx);
    QString modeStr;
    switch (mode) {
        case GrabMode::StreamCallback:
            modeStr = QStringLiteral("StreamCallback");
            break;
        case GrabMode::TriggerCallback:
            modeStr = QStringLiteral("TriggerCallback");
            break;
        case GrabMode::SnapSync:
            modeStr = QStringLiteral("SnapSync");
            break;
    }
    statusBar()->showMessage(
        QStringLiteral("Cam %1 – GrabOne requested (mode: %2)…").arg(camIdx).arg(modeStr));

    if (! bridge_->grabOne(camIdx)) {
        statusBar()->showMessage(
            QStringLiteral("Cam %1 – GrabOne failed (grab already in flight?)").arg(camIdx));
        ui_->grabOneButton->setEnabled(true);
    }
}

void MainWindow::onTriggerSourceChanged(int /*index*/) {
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
    int camIdx = selectedCameraIndex();
    if (camIdx >= 0)
        bridge_->setExposureTime(camIdx, static_cast<double>(value));
}

void MainWindow::onExposureSpinChanged(double value) {
    if (updatingControls_)
        return;
    updatingControls_ = true;
    ui_->exposureSlider->setValue(static_cast<int>(value));
    updatingControls_ = false;
    int camIdx = selectedCameraIndex();
    if (camIdx >= 0)
        bridge_->setExposureTime(camIdx, value);
}

void MainWindow::onGainSliderChanged(int value) {
    if (updatingControls_)
        return;
    updatingControls_ = true;
    double dB = value / 10.0;
    ui_->gainSpinBox->setValue(dB);
    updatingControls_ = false;
    int camIdx = selectedCameraIndex();
    if (camIdx >= 0)
        bridge_->setGain(camIdx, dB);
}

void MainWindow::onGainSpinChanged(double value) {
    if (updatingControls_)
        return;
    updatingControls_ = true;
    ui_->gainSlider->setValue(static_cast<int>(value * 10));
    updatingControls_ = false;
    int camIdx = selectedCameraIndex();
    if (camIdx >= 0)
        bridge_->setGain(camIdx, value);
}

// ======================================================================== //
//  Bridge signal handlers                                                    //
// ======================================================================== //

void MainWindow::onFrameReady(int cameraIndex, cv::Mat image, quint64 frameId,
                              quint64 /*timestampNs*/) {
    auto& st = stats_[cameraIndex];
    ++st.frameCount;
    st.lastFrameId = frameId;

    if (panels_.contains(cameraIndex))
        panels_[cameraIndex]->displayFrame(image);

    // Re-enable Grab One after any frame delivery from the selected camera
    // (covers both the async grabOne result and regular stream frames).
    if (cameraIndex == selectedCameraIndex())
        ui_->grabOneButton->setEnabled(true);
}

void MainWindow::onConnectionStateChanged(int cameraIndex, QString statusText) {
    if (panels_.contains(cameraIndex))
        panels_[cameraIndex]->setStatus(statusText);

    // Update list item text to reflect state.
    for (int r = 0; r < ui_->cameraListWidget->count(); ++r) {
        auto* item = ui_->cameraListWidget->item(r);
        if (item->data(Qt::UserRole).toInt() == cameraIndex) {
            QString base = item->text().section(QStringLiteral(" ["), 0, 0);
            item->setText(QStringLiteral("%1 [%2]").arg(base).arg(statusText));
            break;
        }
    }

    statusBar()->showMessage(QStringLiteral("Cam %1: %2").arg(cameraIndex).arg(statusText));
}

void MainWindow::onQueueStatsUpdated(int cameraIndex, quint64 pushed, quint64 popped,
                                     quint64 dropped) {
    // Only update the status bar when this camera is the selected one.
    if (cameraIndex != selectedCameraIndex())
        return;

    double fps = stats_.contains(cameraIndex) ? stats_[cameraIndex].fps : 0.0;
    quint64 fid = stats_.contains(cameraIndex) ? stats_[cameraIndex].lastFrameId : 0;

    statusBar()->showMessage(
        QStringLiteral("Cam %1 | Queue: pushed=%2 popped=%3 dropped=%4 | FPS: %5 | Frame#%6")
            .arg(cameraIndex)
            .arg(pushed)
            .arg(popped)
            .arg(dropped)
            .arg(fps, 0, 'f', 1)
            .arg(fid));
}

void MainWindow::onStatusTimer() {
    // Compute per-camera FPS (timer fires every 500 ms → multiply by 2).
    for (auto it = stats_.begin(); it != stats_.end(); ++it) {
        auto& st = it.value();
        st.fps = static_cast<double>(st.frameCount - st.lastCount) * 2.0;
        st.lastCount = st.frameCount;
    }
}

// ======================================================================== //
//  Private helpers                                                           //
// ======================================================================== //

int MainWindow::selectedCameraIndex() const {
    auto* item = ui_->cameraListWidget->currentItem();
    if (! item)
        return -1;
    return item->data(Qt::UserRole).toInt();
}

void MainWindow::updateParameterPanel() {
    int camIdx = selectedCameraIndex();
    if (camIdx < 0)
        return;

    updatingControls_ = true;

    double us = 0, minUs = 0, maxUs = 0;
    if (bridge_->getExposureTime(camIdx, us, minUs, maxUs)) {
        ui_->exposureSpinBox->setMinimum(minUs);
        ui_->exposureSpinBox->setMaximum(maxUs);
        ui_->exposureSpinBox->setValue(us);
        ui_->exposureSlider->setMinimum(static_cast<int>(minUs));
        ui_->exposureSlider->setMaximum(static_cast<int>(maxUs));
        ui_->exposureSlider->setValue(static_cast<int>(us));
    }

    double dB = 0, minDb = 0, maxDb = 0;
    if (bridge_->getGain(camIdx, dB, minDb, maxDb)) {
        ui_->gainSpinBox->setMinimum(minDb);
        ui_->gainSpinBox->setMaximum(maxDb);
        ui_->gainSpinBox->setValue(dB);
        ui_->gainSlider->setMinimum(static_cast<int>(minDb * 10));
        ui_->gainSlider->setMaximum(static_cast<int>(maxDb * 10));
        ui_->gainSlider->setValue(static_cast<int>(dB * 10));
    }

    updatingControls_ = false;
}

void MainWindow::setControlsEnabled(bool cameraSelected) {
    ui_->disconnectButton->setEnabled(cameraSelected);
    ui_->modeGroup->setEnabled(cameraSelected);
    ui_->paramGroup->setEnabled(cameraSelected);

    bool triggerMode = cameraSelected && (ui_->modeComboBox->currentIndex() == 1 ||
                                          ui_->modeComboBox->currentIndex() == 2);
    ui_->softTriggerButton->setEnabled(triggerMode &&
                                       ui_->triggerSourceComboBox->currentIndex() == 0);

    // GrabOne works in any mode – enable whenever a camera is selected.
    ui_->grabOneButton->setEnabled(cameraSelected);
}

void MainWindow::addCameraPanel(int cameraIndex, const QString& name) {
    auto* panel = new CameraPanel(cameraIndex, name, ui_->previewContainer);
    panels_[cameraIndex] = panel;
    reflowGrid();
}

void MainWindow::removeCameraPanel(int cameraIndex) {
    if (! panels_.contains(cameraIndex))
        return;

    auto* layout = qobject_cast<QGridLayout*>(ui_->previewContainer->layout());
    if (layout)
        layout->removeWidget(panels_[cameraIndex]);

    panels_[cameraIndex]->deleteLater();
    panels_.remove(cameraIndex);
    reflowGrid();
}

void MainWindow::reflowGrid() {
    auto* layout = qobject_cast<QGridLayout*>(ui_->previewContainer->layout());
    if (! layout)
        return;

    // Remove all layout items without deleting the widgets.
    while (layout->count() > 0) {
        auto* item = layout->takeAt(0);
        delete item;
    }

    // Re-add all panels in a 2-column grid.
    const int cols = 2;
    int idx = 0;
    for (auto* panel : panels_) {
        layout->addWidget(panel, idx / cols, idx % cols);
        ++idx;
    }
}
