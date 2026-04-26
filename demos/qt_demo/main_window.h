#pragma once

#include "camera_bridge.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QTimer>
#include <opencv2/core.hpp>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// ======================================================================== //
//  CameraPanel – one preview tile in the grid for each open camera         //
// ======================================================================== //

/// A lightweight widget that displays a single camera's live feed plus a
/// one-line status label underneath.  Created dynamically when a camera is
/// opened and destroyed when it is closed.
class CameraPanel : public QFrame {
    Q_OBJECT
  public:
    explicit CameraPanel(int cameraIndex, const QString& title, QWidget* parent = nullptr);

    int cameraIndex() const {
        return cameraIndex_;
    }

    /// Update the preview image (called from MainWindow::onFrameReady).
    void displayFrame(const cv::Mat& mat);

    /// Update the one-line status text below the image.
    void setStatus(const QString& text);

  private:
    int cameraIndex_;
    QLabel* imageLabel_;
    QLabel* statusLabel_;
};

// ======================================================================== //
//  MainWindow                                                                //
// ======================================================================== //

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

  private slots:
    // Device management
    void onRefreshClicked();
    void onConnectClicked();
    void onDisconnectClicked();
    void onDisconnectAllClicked();

    // Open-cameras list selection
    void onCameraSelectionChanged();

    // Mode selection (applies to the currently selected camera)
    void onModeChanged(int index);

    // Soft trigger button
    void onSoftTriggerClicked();

    // Grab One button – works in any acquisition mode
    void onGrabOneClicked();

    // Parameter controls (apply to the currently selected camera)
    void onExposureSliderChanged(int value);
    void onExposureSpinChanged(double value);
    void onGainSliderChanged(int value);
    void onGainSpinChanged(double value);
    void onTriggerSourceChanged(int index);

    // Bridge signals – all carry cameraIndex to identify the source camera
    void onFrameReady(int cameraIndex, cv::Mat image, quint64 frameId, quint64 timestampNs);
    void onConnectionStateChanged(int cameraIndex, QString statusText);
    void onQueueStatsUpdated(int cameraIndex, quint64 pushed, quint64 popped, quint64 dropped);

    // Status bar refresh timer
    void onStatusTimer();

  private:
    // ---------------------------------------------------------------- //
    //  Helpers                                                           //
    // ---------------------------------------------------------------- //

    /// Return the camera index of the currently selected row in the list,
    /// or -1 if nothing is selected.
    int selectedCameraIndex() const;

    /// Reload the parameter panel from the currently selected camera.
    void updateParameterPanel();

    /// Enable/disable the mode + parameter groups based on whether a camera
    /// is currently selected in the list.
    void setControlsEnabled(bool cameraSelected);

    /// Add a panel tile to the preview grid for @p cameraIndex.
    void addCameraPanel(int cameraIndex, const QString& name);

    /// Remove the panel tile for @p cameraIndex from the grid.
    void removeCameraPanel(int cameraIndex);

    /// Reflow all existing panels into the grid (called after add/remove).
    void reflowGrid();

    // ---------------------------------------------------------------- //
    //  Member data                                                       //
    // ---------------------------------------------------------------- //

    Ui::MainWindow* ui_;
    CameraBridge* bridge_;
    QTimer* statusTimer_;

    // cameraIndex -> panel widget
    QMap<int, CameraPanel*> panels_;

    // Per-camera frame counters for FPS calculation
    struct CameraStats {
        quint64 frameCount{0};
        quint64 lastCount{0};
        double fps{0.0};
        quint64 lastFrameId{0};
    };
    QMap<int, CameraStats> stats_;

    // Suppress feedback loops when programmatically updating sliders/spins
    bool updatingControls_{false};
};
