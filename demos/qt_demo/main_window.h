#pragma once

#include "camera_bridge.h"

#include <QLabel>
#include <QMainWindow>
#include <QTimer>
#include <opencv2/core.hpp>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

  private slots:
    // Toolbar / control panel
    void onRefreshClicked();
    void onConnectClicked();
    void onDisconnectClicked();

    // Mode selection
    void onModeChanged(int index);

    // Soft trigger button
    void onSoftTriggerClicked();

    // Parameter controls
    void onExposureSliderChanged(int value);
    void onExposureSpinChanged(double value);
    void onGainSliderChanged(int value);
    void onGainSpinChanged(double value);
    void onTriggerSourceChanged(int index);

    // Bridge signals
    void onFrameReady(cv::Mat image, quint64 frameId, quint64 timestampNs);
    void onConnectionStateChanged(QString statusText);
    void onQueueStatsUpdated(quint64 pushed, quint64 popped, quint64 dropped);

    // Status bar refresh timer
    void onStatusTimer();

  private:
    void updateParameterPanel();
    void setControlsEnabled(bool cameraOpen);
    void displayFrame(const cv::Mat& mat);

    Ui::MainWindow* ui_;
    CameraBridge* bridge_;
    QTimer* statusTimer_;

    // Track last known frame info for status bar
    quint64 lastFrameId_{0};
    quint64 frameCount_{0};
    qint64 lastFrameTimeMs_{0};
    double displayedFps_{0.0};

    // Suppress feedback loops when programmatically updating sliders/spins
    bool updatingControls_{false};
};
