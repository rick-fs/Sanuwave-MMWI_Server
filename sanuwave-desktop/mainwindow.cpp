// Copyright 2026 Sanuwave Medical LLC.
//
// Portions of this code were generated with Claude.ai and
// reviewed and edited.
//
// mainwindow.cpp
#include "mainwindow.h"
#include "logger.h"
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QScrollBar>
#include <QMetaType>
#include <QMenuBar>
#include <QMenu>
#include <QFileDialog>
#include <QDir>
#include <QFormLayout>
#include <QCloseEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QStatusBar>
#include <QJsonArray>
#include "expandablegroupbox.h"
#include "image_decoding.h"
#include "camera_param_router.h"
#include "camera_ui_controller.h"
#include "dng_exporter.h"
#include "calibration_viewer_dialog.h"
#include "protocol_constants.h"
#include "aboutdialog.h"
#include "version.h"
#include "motion_chart_widget.h"
#include "motion_settings_dialog.h"

#include "uvbf_vblank_dialog.h"
#include "imu_config_dialog.h"


namespace Camera = sanuwave::protocol::Camera;
namespace Command = sanuwave::protocol::Command;
namespace Param = sanuwave::protocol::Param;
namespace Colormap = sanuwave::protocol::Colormap;
namespace StreamFormat = sanuwave::protocol::StreamFormat;
namespace ParamMode = sanuwave::protocol::ParamMode;
namespace Modality = sanuwave::protocol::Modality;
namespace ImuParam     = sanuwave::protocol::ImuParam;
namespace ImuField     = sanuwave::protocol::ImuField;
namespace ImuEventKind = sanuwave::protocol::ImuEventKind;


namespace {
    // Bump this whenever you make changes that could invalidate saved
    // window state (added/removed dock widgets, renamed objectNames,
    // major layout changes). Existing users get clean defaults on next launch.
    constexpr int kWindowStateVersion = 2;
}



MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      serverConnection(new ServerConnection(this)),
      settings(new QSettings("Sanuwave", "SanuwaveClient", this)),
      streamingActive(false),
      streamFrameCount(0), distanceStreaming(false), distanceInitialized(false), uvInitialized(false)
{
    LOG_INFO << "Sanuwave Medical Imaging Client started" << std::endl;

    setupUI();

    createMenuBar();
    loadSettings();

    rgbDecoder = new StreamFrameDecoder(this);
    thermalDecoder = new StreamFrameDecoder(this);

    // Both decoders deliver to the same slot
    connect(rgbDecoder, &StreamFrameDecoder::frameDecoded,
            this, &MainWindow::onStreamFrameDecoded, Qt::QueuedConnection);
    connect(thermalDecoder, &StreamFrameDecoder::frameDecoded,
            this, &MainWindow::onStreamFrameDecoded, Qt::QueuedConnection);

    connect(serverConnection, &ServerConnection::connected,
            this, &MainWindow::onServerConnected);
    connect(serverConnection, &ServerConnection::disconnected,
            this, &MainWindow::onServerDisconnected);
    connect(serverConnection, &ServerConnection::errorOccurred,
            this, &MainWindow::onServerError);
    connect(serverConnection, &ServerConnection::imageReceived,
            this, &MainWindow::onImageReceived);
    connect(serverConnection, &ServerConnection::streamFrameReceived,
            this, &MainWindow::onStreamFrameReceived);
    connect(serverConnection, &ServerConnection::intervalFrameReceived,
            this, &MainWindow::onIntervalFrameReceived);
    connect(serverConnection, &ServerConnection::distanceDataReceived,
            this, &MainWindow::onDistanceDataReceived);
    connect(serverConnection, &ServerConnection::uvDataReceived,
            this, &MainWindow::onUVDataReceived);
    connect(serverConnection, &ServerConnection::statusReceived,
            this, &MainWindow::onStatusReceived);
    connect(serverConnection, &ServerConnection::serverError,
            this, &MainWindow::onServerErrorMessage);

    connect(serverConnection, &ServerConnection::captureComplete,
            this, &MainWindow::onCaptureComplete);
    connect(serverConnection, &ServerConnection::sensorTimingReceived,
            this, &MainWindow::onSensorTimingReceived);
    connect(serverConnection, &ServerConnection::cameraTemperatureReceived,
            this, &MainWindow::onCameraTemperatureReceived);
    connect(serverConnection, &ServerConnection::alsDataReceived,
        this, &MainWindow::onALSDataReceived);
    
    connect(serverConnection, &ServerConnection::imuDataReceived,
            this, &MainWindow::onImuDataReceived);
    connect(serverConnection, &ServerConnection::imuEventReceived,
            this, &MainWindow::onImuEventReceived);
    distanceStreamTimer = new QTimer(this);
    connect(distanceStreamTimer, &QTimer::timeout, this, &MainWindow::onDistanceStreamTick);

    streamFpsTimer = new QTimer(this);
    connect(streamFpsTimer, &QTimer::timeout, [this]()
            {
        if (streamingActive && streamFrameCount > 0) {
            streamFpsLabel->setText(QString("FPS: %1").arg(streamFrameCount));
            streamFrameCount = 0;
        } });
    streamFpsTimer->start(1000);

    // Motion indicator: decay timer clears the display if no rgb-stream
    // frame arrives for a while (stream stopped, network stall, etc.).
    // Thresholds come from QSettings so they can be tuned without a
    // client release.
    motionDecayTimer_ = new QTimer(this);
    motionDecayTimer_->setSingleShot(true);
    connect(motionDecayTimer_, &QTimer::timeout,
            this, &MainWindow::onMotionDecayTimeout);

    motionEnterMoving_ = settings->value("motion/enterMovingPx", 1.5).toDouble();
    motionExitMoving_  = settings->value("motion/exitMovingPx",  0.5).toDouble();
    motionConfFloor_   = settings->value("motion/confFloor",     0.05).toDouble();
    motionDecayMs_     = settings->value("motion/decayMs",       1000).toInt();

    // Push the loaded thresholds to the chart now that QSettings has been
    // consulted (setupStreamingGroup ran before settings load and would
    // otherwise have only the compile-time defaults).
    if (motionChart_)
        motionChart_->setThresholds(motionEnterMoving_, motionExitMoving_);

    imageViewerWindow = new ImageViewerWindow(this);
    setupSettingsManager();
    sessionManager = new SessionManager(this);
    connect(sessionManager, &SessionManager::sessionStarted,
            this, &MainWindow::onSessionStarted);
    connect(sessionManager, &SessionManager::frameSaved,
            this, &MainWindow::onFrameSaved);
    connect(sessionManager, &SessionManager::sessionError,
            this, [this](const QString &msg)
            { addLog(msg, "error"); });
    addLog("Application started. Ready to connect.", "info");
    restoreWindowGeometry();
}

MainWindow::~MainWindow()
{
    LOG_INFO << "Shutting down Sanuwave client" << std::endl;

    // Set shutdown flag FIRST - before anything else
    shuttingDown = true;

    // Stop all timers first
    if (streamFpsTimer)
    {
        streamFpsTimer->stop();
        streamFpsTimer->disconnect();
    }

    if (distanceStreamTimer)
    {
        distanceStreamTimer->stop();
        distanceStreamTimer->disconnect();
    }

    streamingActive = false;
    isDualStreaming = false;
    distanceStreaming = false;
}

void MainWindow::registerCameraWidgets()
{
    uiController = new CameraUIController();

    // RGB Streaming
    uiController->registerWidget(CameraParam::RGB_STREAMING_EXPOSURE, rgbStreamingExposureSpinBox);
    uiController->registerWidget(CameraParam::RGB_STREAMING_LENS_POSITION, rgbStreamingLensPositionSpinBox);
    uiController->registerWidget(CameraParam::RGB_STREAMING_ANALOG_GAIN, rgbStreamingAnalogGainSpinBox);
    uiController->registerWidget(CameraParam::RGB_STREAMING_DIGITAL_GAIN, rgbStreamingDigitalGainSpinBox);
    uiController->registerWidget(CameraParam::RGB_STREAMING_RED_GAIN, rgbStreamingRedGainSpinBox);
    uiController->registerWidget(CameraParam::RGB_STREAMING_BLUE_GAIN, rgbStreamingBlueGainSpinBox);

    // RGB Capture
    uiController->registerWidget(CameraParam::RGB_CAPTURE_EXPOSURE, rgbCaptureExposureSpinBox);
    uiController->registerWidget(CameraParam::RGB_CAPTURE_LENS_POSITION, rgbCaptureLensPositionSpinBox);
    uiController->registerWidget(CameraParam::RGB_CAPTURE_ANALOG_GAIN, rgbCaptureAnalogGainSpinBox);
    uiController->registerWidget(CameraParam::RGB_CAPTURE_DIGITAL_GAIN, rgbCaptureDigitalGainSpinBox);
    uiController->registerWidget(CameraParam::RGB_CAPTURE_RED_GAIN, rgbCaptureRedGainSpinBox);
    uiController->registerWidget(CameraParam::RGB_CAPTURE_BLUE_GAIN, rgbCaptureBlueGainSpinBox);
    uiController->registerWidget(CameraParam::RGB_CAPTURE_RAW_MODE, rgbCaptureRawModeCheckBox);
    // Arducam Streaming
    uiController->registerWidget(CameraParam::ARDUCAM_STREAMING_EXPOSURE, arducamStreamingExposureSpinBox);
    uiController->registerWidget(CameraParam::ARDUCAM_STREAMING_RED_GAIN, arducamStreamingRedGainSpinBox);
    uiController->registerWidget(CameraParam::ARDUCAM_STREAMING_BLUE_GAIN, arducamStreamingBlueGainSpinBox);

    // Arducam Capture
    uiController->registerWidget(CameraParam::ARDUCAM_CAPTURE_EXPOSURE, arducamCaptureExposureSpinBox);
    uiController->registerWidget(CameraParam::ARDUCAM_CAPTURE_RED_GAIN, arducamCaptureRedGainSpinBox);
    uiController->registerWidget(CameraParam::ARDUCAM_CAPTURE_BLUE_GAIN, arducamCaptureBlueGainSpinBox);
    uiController->registerWidget(CameraParam::ARDUCAM_CAPTURE_RAW_MODE, arducamCaptureRawModeCheckBox);

    // Thermal
    uiController->registerWidget(CameraParam::THERMAL_STREAMING_ALARM_TEMP, thermalStreamingAlarmTempSpinBox);
    uiController->registerWidget(CameraParam::THERMAL_CAPTURE_ALARM_TEMP, thermalCaptureAlarmTempSpinBox);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    LOG_INFO << "Main window closing, saving geometry" << std::endl;
    shuttingDown = true;
    saveWindowGeometry();
    QMainWindow::closeEvent(event);
}

ExpandableGroupBox *MainWindow::setupConnectionGroup()
{
    ExpandableGroupBox *connectionGroup = new ExpandableGroupBox("Connection", this);
    QHBoxLayout *layout = new QHBoxLayout(connectionGroup->getContentWidget());
    serverInfoLabel = new QLabel("Server: Not configured");
    layout->addWidget(serverInfoLabel);
    layout->addStretch();
    statusLabel = new QLabel("⚫ Disconnected");
    layout->addWidget(statusLabel);
    connectButton = new QPushButton("Connect");
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    layout->addWidget(connectButton);
    disconnectButton = new QPushButton("Disconnect");
    disconnectButton->setEnabled(false);
    connect(disconnectButton, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    layout->addWidget(disconnectButton);
    return connectionGroup;
}

ExpandableGroupBox *MainWindow::setUpStreamingSettingsGroup(QWidget *parent)
{
    ExpandableGroupBox *group = new ExpandableGroupBox("Stream Settings", parent);
    QGridLayout *layout = new QGridLayout(group->getContentWidget());

    layout->addWidget(new QLabel("Camera:"), 0, 0);
    streamCameraSelector = new QComboBox();
    streamCameraSelector->addItem("📷 RGB Camera (IMX708)", Camera::IMX708);
    streamCameraSelector->addItem("📸 Arducam (IMX219)", Camera::IMX219);
    streamCameraSelector->addItem("🌡️ Thermal Camera", Camera::THERMAL);
    streamCameraSelector->setCurrentIndex(0);
    connect(streamCameraSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onStreamCameraChanged);
    layout->addWidget(streamCameraSelector, 0, 1, 1, 3);

    layout->addWidget(new QLabel("Resolution:"), 1, 0);
    streamResolutionCombo = new QComboBox();
    streamResolutionCombo->addItem("🏆 Maximum (4608×2592) - 12MP", QVariantList() << 4608 << 2592);
    streamResolutionCombo->addItem("High (2304×1296) - 6MP", QVariantList() << 2304 << 1296);
    streamResolutionCombo->addItem("HD 1080p (1920×1080)", QVariantList() << 1920 << 1080);
    streamResolutionCombo->addItem("HD 720p (1280×720)", QVariantList() << 1280 << 720);
    streamResolutionCombo->addItem("VGA (640×480)", QVariantList() << 640 << 480);
    connect(streamResolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onStreamResolutionChanged);
    layout->addWidget(streamResolutionCombo, 1, 1);

    layout->addWidget(new QLabel("Quality:"), 2, 0);
    streamQualitySpinBox = new QSpinBox();
    streamQualitySpinBox->setRange(1, 100);
    streamQualitySpinBox->setValue(80);
    streamQualitySpinBox->setSuffix("%");
    connect(streamQualitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onStreamQualityChanged);
    layout->addWidget(streamQualitySpinBox, 2, 1);

    layout->addWidget(new QLabel("Orientation:"), 3, 0);
    streamRotate180CheckBox = new QCheckBox("Rotate image 180°");
    streamRotate180CheckBox->setChecked(false);
    layout->addWidget(streamRotate180CheckBox, 3, 1);
    connect(streamRotate180CheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onRotationChanged(c, "stream"); });

    layout->addWidget(new QLabel("Motion:"), 4, 0);
    streamMotionEnabledCheckBox = new QCheckBox("Measure motion (rgb only)");
    streamMotionEnabledCheckBox->setChecked(
        settings->value("motion/enabledAtStart", false).toBool());
    streamMotionEnabledCheckBox->setToolTip(
        tr("Server-side phase correlation on a centered ROI of each preview "
           "frame. Indicates how much the camera has moved between frames. "
           "Adds ~2-3 ms per frame on the Pi 5. RGB streams only."));
    layout->addWidget(streamMotionEnabledCheckBox, 4, 1);
    // Persist the user's last setting so it survives a client restart.
    connect(streamMotionEnabledCheckBox, &QCheckBox::toggled,
            this, [this](bool c) {
                settings->setValue("motion/enabledAtStart", c);
            });

    // Settings button: opens a non-modal dialog for live threshold tuning
    // and server-side ROI/reference adjustment. The dialog reuses the same
    // QSettings keys this MainWindow reads on startup.
    motionSettingsBtn_ = new QPushButton(tr("Settings\u2026"), this);
    motionSettingsBtn_->setToolTip(
        tr("Tune motion-indicator thresholds and measurement parameters."));
    connect(motionSettingsBtn_, &QPushButton::clicked,
            this, &MainWindow::onMotionSettingsClicked);
    layout->addWidget(motionSettingsBtn_, 4, 2);

    return group;
}

// ============================================================================
// STREAMING RGB CONTROLS GROUP
// ============================================================================
ExpandableGroupBox *MainWindow::setUpStreamingRGBControlsGroup(QWidget *parent)
{
    streamingRGBCameraControlsGroupBox = new ExpandableGroupBox("RGB Controls (Streaming)", parent);
    QFormLayout *controlsLayout = new QFormLayout(streamingRGBCameraControlsGroupBox->getContentWidget());

    // Exposure
    ExpandableGroupBox *exposureGroup = new ExpandableGroupBox("Exposure", streamingRGBCameraControlsGroupBox);
    QFormLayout *expLayout = new QFormLayout(exposureGroup->getContentWidget());
    rgbStreamingAutoExposureCheckBox = new QCheckBox();
    rgbStreamingAutoExposureCheckBox->setChecked(true);
    expLayout->addRow("Auto Exposure:", rgbStreamingAutoExposureCheckBox);
    rgbStreamingEvCompensationSpinBox = new QDoubleSpinBox();
    rgbStreamingEvCompensationSpinBox->setObjectName("rgbStreamingEvCompensationSpinBox");
    rgbStreamingEvCompensationSpinBox->setRange(-4.0, 4.0);
    rgbStreamingEvCompensationSpinBox->setSingleStep(0.5);
    rgbStreamingEvCompensationSpinBox->setValue(0.0);
    expLayout->addRow("EV Compensation:", rgbStreamingEvCompensationSpinBox);
    rgbStreamingExposureSpinBox = new QSpinBox();
    rgbStreamingExposureSpinBox->setObjectName("rgbStreamingExposureSpinBox");
    rgbStreamingExposureSpinBox->setRange(1, 1000000);
    rgbStreamingExposureSpinBox->setValue(10000);
    rgbStreamingExposureSpinBox->setSuffix(" µs");
    rgbStreamingExposureSpinBox->setEnabled(false);

    rgbStreamingExposureHintLabel = new QLabel("Also disable Auto Analog Gain for full manual control");
    rgbStreamingExposureHintLabel->setStyleSheet("color: #e6224d; font-style: italic; font-size: 11px;");
    rgbStreamingExposureHintLabel->setWordWrap(true);
    rgbStreamingExposureHintLabel->setVisible(false);
    expLayout->addRow("", rgbStreamingExposureHintLabel);

    expLayout->addRow("Exposure Time:", rgbStreamingExposureSpinBox);
    controlsLayout->addRow(exposureGroup);

    connect(rgbStreamingExposureSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v)
            {
                onCameraParamChanged(CameraParam::RGB_STREAMING_EXPOSURE, v);
            });

    connect(rgbStreamingAutoExposureCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            {
                onCameraParamChanged(CameraParam::RGB_STREAMING_AUTO_EXPOSURE, c);
                rgbStreamingExposureSpinBox->setEnabled(!c);
                rgbStreamingEvCompensationSpinBox->setEnabled(c); // Add this line
                updateImx708StreamingExposureHint(); });

    connect(rgbStreamingEvCompensationSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_STREAMING_EV_COMPENSATION, v); });

    // Focus
    ExpandableGroupBox *focusGroup = new ExpandableGroupBox("Focus", streamingRGBCameraControlsGroupBox);
    QFormLayout *focusLayout = new QFormLayout(focusGroup->getContentWidget());
    rgbStreamingAutoFocusCheckBox = new QCheckBox();
    rgbStreamingAutoFocusCheckBox->setChecked(true);
    focusLayout->addRow("Auto Focus:", rgbStreamingAutoFocusCheckBox);
    rgbStreamingAutoFocusCheckBox->setObjectName("rgbStreamingAutoFocusCheckBox");
    rgbStreamingLensPositionSpinBox = new QDoubleSpinBox();
    rgbStreamingLensPositionSpinBox->setObjectName("rgbStreamingLensPositionSpinBox");
    rgbStreamingLensPositionSpinBox->setRange(0.0, 12.0);
    rgbStreamingLensPositionSpinBox->setSingleStep(0.1);
    rgbStreamingLensPositionSpinBox->setValue(0.0);
    rgbStreamingLensPositionSpinBox->setEnabled(false);
    focusLayout->addRow("Lens Position:", rgbStreamingLensPositionSpinBox);
    addFocusSweepWidgets(focusLayout,
                         rgbStreamingSweepButton, rgbStreamingSweepLabel, rgbStreamingSweepBar,
                         CameraParam::RGB_STREAMING_AUTO_FOCUS,
                         CameraParam::RGB_STREAMING_LENS_POSITION,
                         rgbStreamingLensPositionSpinBox, Camera::IMX708);
    controlsLayout->addRow(focusGroup);

    connect(rgbStreamingAutoFocusCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onCameraParamChanged(CameraParam::RGB_STREAMING_AUTO_FOCUS, c); });
    connect(rgbStreamingLensPositionSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_STREAMING_LENS_POSITION, v); });

    // Gain
    ExpandableGroupBox *gainGroup = new ExpandableGroupBox("Gain", streamingRGBCameraControlsGroupBox);
    QFormLayout *gainLayout = new QFormLayout(gainGroup->getContentWidget());
    rgbStreamingAutoAnalogGainCheckBox = new QCheckBox();
    rgbStreamingAutoAnalogGainCheckBox->setObjectName("rgbStreamingAutoAnalogGainCheckBox");
    rgbStreamingAutoAnalogGainCheckBox->setChecked(true);
    gainLayout->addRow("Auto Analog Gain:", rgbStreamingAutoAnalogGainCheckBox);
    rgbStreamingAnalogGainSpinBox = new QDoubleSpinBox();
    rgbStreamingAnalogGainSpinBox->setObjectName("rgbStreamingAnalogGainSpinBox");
    rgbStreamingAnalogGainSpinBox->setRange(1.0, 8.57);
    rgbStreamingAnalogGainSpinBox->setSingleStep(0.1);
    rgbStreamingAnalogGainSpinBox->setValue(1.0);
    rgbStreamingAnalogGainSpinBox->setEnabled(false);
    gainLayout->addRow("Analog Gain:", rgbStreamingAnalogGainSpinBox);
    rgbStreamingDigitalGainSpinBox = new QDoubleSpinBox();
    rgbStreamingDigitalGainSpinBox->setObjectName("rgbStreamingDigitalGainSpinBox");
    rgbStreamingDigitalGainSpinBox->setRange(1.0, 4.0);
    rgbStreamingDigitalGainSpinBox->setSingleStep(0.1);
    rgbStreamingDigitalGainSpinBox->setValue(1.0);
    rgbStreamingDigitalGainSpinBox->setEnabled(false);
    gainLayout->addRow("Digital Gain:", rgbStreamingDigitalGainSpinBox);
    controlsLayout->addRow(gainGroup);

    connect(rgbStreamingAutoAnalogGainCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { 
                onCameraParamChanged(CameraParam::RGB_STREAMING_AUTO_ANALOG_GAIN, c); 
                updateImx708StreamingExposureHint(); });

    connect(rgbStreamingAnalogGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_STREAMING_ANALOG_GAIN, v); });
    connect(rgbStreamingDigitalGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_STREAMING_DIGITAL_GAIN, v); });

    // White Balance
    ExpandableGroupBox *wbGroup = new ExpandableGroupBox("White Balance", streamingRGBCameraControlsGroupBox);
    QFormLayout *wbLayout = new QFormLayout(wbGroup->getContentWidget());
    rgbStreamingAutoWhiteBalanceCheckBox = new QCheckBox();
    rgbStreamingAutoWhiteBalanceCheckBox->setObjectName("rgbStreamingAutoWhiteBalanceCheckBox");
    rgbStreamingAutoWhiteBalanceCheckBox->setChecked(true);
    wbLayout->addRow("Auto WB:", rgbStreamingAutoWhiteBalanceCheckBox);

    rgbStreamingRedGainSpinBox = new QDoubleSpinBox();
    rgbStreamingRedGainSpinBox->setObjectName("rgbStreamingRedGainSpinBox");
    rgbStreamingRedGainSpinBox->setRange(0.0, 32.0);
    rgbStreamingRedGainSpinBox->setSingleStep(0.1);
    rgbStreamingRedGainSpinBox->setValue(1.0);
    wbLayout->addRow("Red Gain:", rgbStreamingRedGainSpinBox);
    rgbStreamingBlueGainSpinBox = new QDoubleSpinBox();
    rgbStreamingBlueGainSpinBox->setObjectName("rgbStreamingBlueGainSpinBox");
    rgbStreamingBlueGainSpinBox->setRange(0.0, 32.0);
    rgbStreamingBlueGainSpinBox->setSingleStep(0.1);
    rgbStreamingBlueGainSpinBox->setValue(1.0);
    wbLayout->addRow("Blue Gain:", rgbStreamingBlueGainSpinBox);
    controlsLayout->addRow(wbGroup);

    connect(rgbStreamingAutoWhiteBalanceCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onCameraParamChanged(CameraParam::RGB_STREAMING_AUTO_WB, c); });
    connect(rgbStreamingRedGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_STREAMING_RED_GAIN, v); });
    connect(rgbStreamingBlueGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_STREAMING_BLUE_GAIN, v); });

    return streamingRGBCameraControlsGroupBox;
}

// ============================================================================
// STREAMING ARDUCAM CONTROLS GROUP
// ============================================================================
ExpandableGroupBox *MainWindow::setUpStreamingArducamControlsGroup(QWidget *parent)
{
    streamingArducamControlsGroupBox = new ExpandableGroupBox("Arducam Controls (Streaming)", parent);
    streamingArducamControlsGroupBox->setVisible(false);
    QFormLayout *controlsLayout = new QFormLayout(streamingArducamControlsGroupBox->getContentWidget());

    // Exposure
    ExpandableGroupBox *exposureGroup = new ExpandableGroupBox("Exposure", streamingArducamControlsGroupBox);
    QFormLayout *expLayout = new QFormLayout(exposureGroup->getContentWidget());
    arducamStreamingAutoExposureCheckBox = new QCheckBox();
    arducamStreamingAutoExposureCheckBox->setObjectName("arducamStreamingAutoExposureCheckBox");
    arducamStreamingAutoExposureCheckBox->setChecked(true);
    expLayout->addRow("Auto Exposure:", arducamStreamingAutoExposureCheckBox);
    arducamStreamingEvCompensationSpinBox = new QDoubleSpinBox();
    arducamStreamingEvCompensationSpinBox->setObjectName("arducamStreamingEvCompensationSpinBox");
    arducamStreamingEvCompensationSpinBox->setRange(-4.0, 4.0);
    arducamStreamingEvCompensationSpinBox->setSingleStep(0.5);
    arducamStreamingEvCompensationSpinBox->setValue(0.0);
    expLayout->addRow("EV Compensation:", arducamStreamingEvCompensationSpinBox);

    arducamStreamingExposureSpinBox = new QSpinBox();
    arducamStreamingExposureSpinBox->setObjectName("arducamStreamingExposureSpinBox");
    arducamStreamingExposureSpinBox->setRange(1, 1000000);
    arducamStreamingExposureSpinBox->setValue(10000);
    arducamStreamingExposureSpinBox->setSuffix(" µs");
    arducamStreamingExposureSpinBox->setEnabled(false);
    expLayout->addRow("Exposure Time:", arducamStreamingExposureSpinBox);
    arducamStreamingExposureHintLabel = new QLabel("Manual exposure works best with manual gain settings below");
    arducamStreamingExposureHintLabel->setStyleSheet("color: #e62222; font-style: italic; font-size: 11px;");
    arducamStreamingExposureHintLabel->setWordWrap(true);
    arducamStreamingExposureHintLabel->setVisible(false);
    expLayout->addRow("", arducamStreamingExposureHintLabel);

    controlsLayout->addRow(exposureGroup);

    connect(arducamStreamingExposureSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v)
            { onCameraParamChanged(CameraParam::ARDUCAM_STREAMING_EXPOSURE, v); });
    connect(arducamStreamingAutoExposureCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { 
                onCameraParamChanged(CameraParam::ARDUCAM_STREAMING_AUTO_EXPOSURE, c); 
                arducamStreamingExposureSpinBox->setEnabled(!c);
                arducamStreamingEvCompensationSpinBox->setEnabled(c);
                updateImx219StreamingExposureHint(); });

    connect(arducamStreamingEvCompensationSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::ARDUCAM_STREAMING_EV_COMPENSATION, v); });

    // Focus
    ExpandableGroupBox *focusGroup = new ExpandableGroupBox("Focus", streamingArducamControlsGroupBox);
    QFormLayout *focusLayout = new QFormLayout(focusGroup->getContentWidget());
    arducamStreamingAutoFocusCheckBox = new QCheckBox();
    arducamStreamingAutoFocusCheckBox->setObjectName("arducamStreamingAutoFocusCheckBox");
    arducamStreamingAutoFocusCheckBox->setChecked(true);
    focusLayout->addRow("Auto Focus:", arducamStreamingAutoFocusCheckBox);
    arducamStreamingLensPositionSpinBox = new QDoubleSpinBox();
    arducamStreamingLensPositionSpinBox->setObjectName("arducamStreamingLensPositionSpinBox");
    arducamStreamingLensPositionSpinBox->setRange(0.0, 12.0);
    arducamStreamingLensPositionSpinBox->setSingleStep(0.1);
    arducamStreamingLensPositionSpinBox->setValue(0.0);
    arducamStreamingLensPositionSpinBox->setEnabled(false);
    focusLayout->addRow("Lens Position:", arducamStreamingLensPositionSpinBox);
    addFocusSweepWidgets(focusLayout,
                         arducamStreamingSweepButton, arducamStreamingSweepLabel, arducamStreamingSweepBar,
                         CameraParam::ARDUCAM_STREAMING_AUTO_FOCUS,
                         CameraParam::ARDUCAM_STREAMING_LENS_POSITION,
                         arducamStreamingLensPositionSpinBox, Camera::IMX219);
    controlsLayout->addRow(focusGroup);

    connect(arducamStreamingAutoFocusCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            {
                onCameraParamChanged(CameraParam::ARDUCAM_STREAMING_AUTO_FOCUS, c);
                arducamStreamingLensPositionSpinBox->setEnabled(!c); });
    connect(arducamStreamingLensPositionSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::ARDUCAM_STREAMING_LENS_POSITION, v); });

    // Gain
    ExpandableGroupBox *gainGroup = new ExpandableGroupBox("Gain", streamingArducamControlsGroupBox);
    QFormLayout *gainLayout = new QFormLayout(gainGroup->getContentWidget());
    arducamStreamingAnalogGainSpinBox = new QDoubleSpinBox();
    arducamStreamingAnalogGainSpinBox->setObjectName("arducamStreamingAnalogGainSpinBox");
    arducamStreamingAnalogGainSpinBox->setRange(1.0, 10.66);
    arducamStreamingAnalogGainSpinBox->setSingleStep(0.1);
    arducamStreamingAnalogGainSpinBox->setValue(1.0);
    gainLayout->addRow("Analog Gain:", arducamStreamingAnalogGainSpinBox);
    QLabel *digitalGainHintLabel = new QLabel("Digital gain is not supported on IMX219 (controlled by ISP)");
    digitalGainHintLabel->setStyleSheet("color: #888888; font-style: italic; font-size: 11px;");
    digitalGainHintLabel->setWordWrap(true);
    gainLayout->addRow("Digital Gain:", digitalGainHintLabel);
    controlsLayout->addRow(gainGroup);

    connect(arducamStreamingAnalogGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::ARDUCAM_STREAMING_ANALOG_GAIN, v); });

    // White Balance
    ExpandableGroupBox *wbGroup = new ExpandableGroupBox("White Balance", streamingArducamControlsGroupBox);
    QFormLayout *wbLayout = new QFormLayout(wbGroup->getContentWidget());
    arducamStreamingAutoWhiteBalanceCheckBox = new QCheckBox();
    arducamStreamingAutoWhiteBalanceCheckBox->setObjectName("arducamStreamingAutoWhiteBalanceCheckBox");
    arducamStreamingAutoWhiteBalanceCheckBox->setChecked(true);
    wbLayout->addRow("Auto WB:", arducamStreamingAutoWhiteBalanceCheckBox);
    arducamStreamingRedGainSpinBox = new QDoubleSpinBox();
    arducamStreamingRedGainSpinBox->setRange(0.0, 32.0);
    arducamStreamingRedGainSpinBox->setSingleStep(0.1);
    arducamStreamingRedGainSpinBox->setValue(1.0);
    wbLayout->addRow("Red Gain:", arducamStreamingRedGainSpinBox);
    arducamStreamingBlueGainSpinBox = new QDoubleSpinBox();
    arducamStreamingBlueGainSpinBox->setRange(0.0, 32.0);
    arducamStreamingBlueGainSpinBox->setSingleStep(0.1);
    arducamStreamingBlueGainSpinBox->setValue(1.0);
    wbLayout->addRow("Blue Gain:", arducamStreamingBlueGainSpinBox);
    controlsLayout->addRow(wbGroup);

    connect(arducamStreamingAutoWhiteBalanceCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onCameraParamChanged(CameraParam::ARDUCAM_STREAMING_AUTO_WB, c); });
    connect(arducamStreamingRedGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::ARDUCAM_STREAMING_RED_GAIN, v); });
    connect(arducamStreamingBlueGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::ARDUCAM_STREAMING_BLUE_GAIN, v); });

    return streamingArducamControlsGroupBox;
}

// ============================================================================
// STREAMING THERMAL CONTROLS GROUP
// ============================================================================
ExpandableGroupBox *MainWindow::setUpStreamingThermalControlsGroup(QWidget *parent)
{
    streamingThermalControlsGroupBox = new ExpandableGroupBox("Thermal Controls (Streaming)", parent);
    streamingThermalControlsGroupBox->setVisible(false);
    QFormLayout *controlsLayout = new QFormLayout(streamingThermalControlsGroupBox->getContentWidget());

    // Emissivity
    ExpandableGroupBox *emGroup = new ExpandableGroupBox("Emissivity", streamingThermalControlsGroupBox);
    QFormLayout *emLayout = new QFormLayout(emGroup->getContentWidget());
    thermalStreamingEmissivitySpinBox = new QDoubleSpinBox();
    thermalStreamingEmissivitySpinBox->setObjectName("thermalStreamingEmissivitySpinBox");
    thermalStreamingEmissivitySpinBox->setRange(0.1, 1.0);
    thermalStreamingEmissivitySpinBox->setSingleStep(0.01);
    thermalStreamingEmissivitySpinBox->setValue(0.95);
    emLayout->addRow("Emissivity:", thermalStreamingEmissivitySpinBox);
    controlsLayout->addRow(emGroup);

    connect(thermalStreamingEmissivitySpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::THERMAL_STREAMING_EMISSIVITY, v); });

    // Temperature Range
    ExpandableGroupBox *tempGroup = new ExpandableGroupBox("Temperature Range", streamingThermalControlsGroupBox);
    QFormLayout *tempLayout = new QFormLayout(tempGroup->getContentWidget());
    thermalStreamingMinTempSpinBox = new QDoubleSpinBox();
    thermalStreamingMinTempSpinBox->setObjectName("thermalStreamingMinTempSpinBox");
    thermalStreamingMinTempSpinBox->setRange(-40.0, 300.0);
    thermalStreamingMinTempSpinBox->setSingleStep(1.0);
    thermalStreamingMinTempSpinBox->setValue(20.0);
    thermalStreamingMinTempSpinBox->setSuffix(" °C");
    tempLayout->addRow("Min Temperature:", thermalStreamingMinTempSpinBox);
    thermalStreamingMaxTempSpinBox = new QDoubleSpinBox();
    thermalStreamingMaxTempSpinBox->setObjectName("thermalStreamingMaxTempSpinBox");
    thermalStreamingMaxTempSpinBox->setRange(-40.0, 300.0);
    thermalStreamingMaxTempSpinBox->setSingleStep(1.0);
    thermalStreamingMaxTempSpinBox->setValue(40.0);
    thermalStreamingMaxTempSpinBox->setSuffix(" °C");
    tempLayout->addRow("Max Temperature:", thermalStreamingMaxTempSpinBox);
    controlsLayout->addRow(tempGroup);

    connect(thermalStreamingMinTempSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::THERMAL_STREAMING_MIN_TEMP, v); });
    connect(thermalStreamingMaxTempSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::THERMAL_STREAMING_MAX_TEMP, v); });

    // Colormap
    ExpandableGroupBox *cmGroup = new ExpandableGroupBox("Visualization", streamingThermalControlsGroupBox);
    QFormLayout *cmLayout = new QFormLayout(cmGroup->getContentWidget());
    thermalStreamingColormapCombo = new QComboBox();
    thermalStreamingColormapCombo->setObjectName("thermalStreamingColormapCombo");
    thermalStreamingColormapCombo->addItem("Ironbow", Colormap::IRONBOW);
    thermalStreamingColormapCombo->addItem("Rainbow", Colormap::RAINBOW);
    thermalStreamingColormapCombo->addItem("Grayscale", Colormap::GRAYSCALE);
    thermalStreamingColormapCombo->addItem("Hot", Colormap::HOT);
    thermalStreamingColormapCombo->addItem("Jet", Colormap::JET);
    cmLayout->addRow("Colormap:", thermalStreamingColormapCombo);
    controlsLayout->addRow(cmGroup);

    connect(thermalStreamingColormapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i)
            { onCameraParamChanged(CameraParam::THERMAL_STREAMING_COLORMAP,
                                   thermalStreamingColormapCombo->itemData(i).toString()); });

    // NUC
    ExpandableGroupBox *nucGroup = new ExpandableGroupBox("Calibration", streamingThermalControlsGroupBox);
    QFormLayout *nucLayout = new QFormLayout(nucGroup->getContentWidget());
    thermalStreamingNucEnabledCheckBox = new QCheckBox();
    thermalStreamingNucEnabledCheckBox->setChecked(true);
    nucLayout->addRow("Enable NUC:", thermalStreamingNucEnabledCheckBox);
    controlsLayout->addRow(nucGroup);

    connect(thermalStreamingNucEnabledCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onCameraParamChanged(CameraParam::THERMAL_STREAMING_NUC_ENABLED, c); });

    // Alarm
    ExpandableGroupBox *alarmGroup = new ExpandableGroupBox("Temperature Alarm", streamingThermalControlsGroupBox);
    QFormLayout *alarmLayout = new QFormLayout(alarmGroup->getContentWidget());
    thermalStreamingAlarmEnabledCheckBox = new QCheckBox();
    thermalStreamingAlarmEnabledCheckBox->setChecked(false);
    alarmLayout->addRow("Enable Alarm:", thermalStreamingAlarmEnabledCheckBox);
    thermalStreamingAlarmTempSpinBox = new QDoubleSpinBox();
    thermalStreamingAlarmTempSpinBox->setRange(-40.0, 300.0);
    thermalStreamingAlarmTempSpinBox->setSingleStep(0.5);
    thermalStreamingAlarmTempSpinBox->setValue(38.0);
    thermalStreamingAlarmTempSpinBox->setSuffix(" °C");
    thermalStreamingAlarmTempSpinBox->setEnabled(false);
    alarmLayout->addRow("Alarm Temperature:", thermalStreamingAlarmTempSpinBox);
    controlsLayout->addRow(alarmGroup);

    connect(thermalStreamingAlarmEnabledCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { 
                onCameraParamChanged(CameraParam::THERMAL_STREAMING_ALARM_ENABLED, c);
                thermalStreamingAlarmTempSpinBox->setEnabled(c); });
    connect(thermalStreamingAlarmTempSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::THERMAL_STREAMING_ALARM_TEMP, v); });

    return streamingThermalControlsGroupBox;
}

// ============================================================================
// setupStreamingGroup()
// ============================================================================
ExpandableGroupBox *MainWindow::setupStreamingGroup()
{
    ExpandableGroupBox *streamingGroup = new ExpandableGroupBox("📹 Video Streaming", this);
    QVBoxLayout *mainLayout = new QVBoxLayout(streamingGroup->getContentWidget());

    mainLayout->addWidget(setUpStreamingSettingsGroup(streamingGroup));
    mainLayout->addWidget(setUpStreamingRGBControlsGroup(streamingGroup));
    mainLayout->addWidget(setUpStreamingArducamControlsGroup(streamingGroup));
    mainLayout->addWidget(setUpStreamingThermalControlsGroup(streamingGroup));
    mainLayout->addWidget(setUpStreamingFrameDurationGroup(streamingGroup));
    streamingFrameDurationGroupBox->setVisible(false);
    // Simplified streaming buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    streamStartButton = new QPushButton("▶ Start Stream");
    streamStartButton->setMinimumHeight(50);
    connect(streamStartButton, &QPushButton::clicked, this, &MainWindow::onStreamStart);
    buttonLayout->addWidget(streamStartButton);

    streamDualButton = new QPushButton("▶ Dual Stream");
    streamDualButton->setMinimumHeight(50);
    connect(streamDualButton, &QPushButton::clicked, this, &MainWindow::onStreamStartDual);
    buttonLayout->addWidget(streamDualButton);

    streamStopButton = new QPushButton("⏹ Stop Stream");
    streamStopButton->setMinimumHeight(50);
    streamStopButton->setEnabled(false);
    connect(streamStopButton, &QPushButton::clicked, this, &MainWindow::onStreamStop);
    buttonLayout->addWidget(streamStopButton);

    buttonLayout->addStretch();

    streamStatusLabel = new QLabel("Idle");
    streamStatusLabel->setStyleSheet("QLabel { padding: 10px; background-color: #ecf0f1; border-radius: 5px; font-weight: bold; }");
    buttonLayout->addWidget(streamStatusLabel);

    streamFpsLabel = new QLabel("FPS: 0");
    streamFpsLabel->setStyleSheet("QLabel { padding: 10px; background-color: #ecf0f1; border-radius: 5px; font-family: monospace; }");
    buttonLayout->addWidget(streamFpsLabel);

    // Numeric motion indicator. Fixed minimum width so the layout does not
    // jitter as the value changes. Updated only on rgb-stream frames; the
    // viewer overlay badge mirrors the state.
    motionLabel_ = new QLabel("Motion: -");
    motionLabel_->setMinimumWidth(140);
    motionLabel_->setStyleSheet("QLabel { padding: 10px; background-color: #ecf0f1; border-radius: 5px; font-family: monospace; }");
    buttonLayout->addWidget(motionLabel_);

    mainLayout->addLayout(buttonLayout);

    // Motion history chart: ECG-style strip chart, scrolling left, showing
    // the last 30 s of trans_px values. Collapsed by default so it does
    // not take up vertical real estate unless the operator opens it.
    motionHistoryGroup_ = new ExpandableGroupBox("Motion History", streamingGroup);
    QVBoxLayout* mhLayout = new QVBoxLayout(motionHistoryGroup_->getContentWidget());
    mhLayout->setContentsMargins(4, 4, 4, 4);
    motionChart_ = new MotionChartWidget(motionHistoryGroup_);
    motionChart_->setThresholds(motionEnterMoving_, motionExitMoving_);
    mhLayout->addWidget(motionChart_);
    mainLayout->addWidget(motionHistoryGroup_);

    return streamingGroup;
}

// ============================================================================
// SINGLE CAPTURE SETTINGS GROUP
// ============================================================================
ExpandableGroupBox *MainWindow::setUpSingleCaptureSettingsGroup(QWidget *parent)
{
    ExpandableGroupBox *group = new ExpandableGroupBox("Capture Settings", parent);
    QGridLayout *layout = new QGridLayout(group->getContentWidget());

    layout->addWidget(new QLabel("Camera:"), 0, 0);
    singleCaptureCameraSelector = new QComboBox();
    singleCaptureCameraSelector->addItem("📷 RGB Camera (IMX708)", Camera::IMX708);
    singleCaptureCameraSelector->addItem("📸 Arducam (IMX219)", Camera::IMX219);
    singleCaptureCameraSelector->addItem("🌡️ Thermal Camera", Camera::THERMAL);
    connect(singleCaptureCameraSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSingleCaptureCameraChanged);
    layout->addWidget(singleCaptureCameraSelector, 0, 1, 1, 3);

    layout->addWidget(new QLabel("Resolution:"), 1, 0);
    singleCaptureResolutionCombo = new QComboBox();
    singleCaptureResolutionCombo->addItem("🏆 Maximum (4608×2592) - 12MP", QVariantList() << 4608 << 2592);
    singleCaptureResolutionCombo->addItem("High (2304×1296) - 6MP", QVariantList() << 2304 << 1296);
    singleCaptureResolutionCombo->addItem("HD 1080p (1920×1080)", QVariantList() << 1920 << 1080);
    singleCaptureResolutionCombo->addItem("HD 720p (1280×720)", QVariantList() << 1280 << 720);
    singleCaptureResolutionCombo->addItem("VGA (640×480)", QVariantList() << 640 << 480);
    singleCaptureResolutionCombo->setCurrentIndex(0);
    connect(singleCaptureResolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSingleCaptureResolutionChanged);
    layout->addWidget(singleCaptureResolutionCombo, 1, 1);

    layout->addWidget(new QLabel("Quality:"), 2, 0);
    singleCaptureQualitySpinBox = new QSpinBox();
    singleCaptureQualitySpinBox->setRange(1, 100);
    singleCaptureQualitySpinBox->setValue(95);
    singleCaptureQualitySpinBox->setSuffix("%");
    layout->addWidget(singleCaptureQualitySpinBox, 2, 1);

    layout->addWidget(new QLabel("Orientation:"), 3, 0);
    captureRotate180CheckBox = new QCheckBox("Rotate image 180°");
    captureRotate180CheckBox->setChecked(false);
    layout->addWidget(captureRotate180CheckBox, 3, 1);
    connect(captureRotate180CheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onRotationChanged(c, "capture"); });

    layout->addWidget(new QLabel("LED Mode:"), 4, 0);
    ledGpioModeCombo = new QComboBox();
    ledGpioModeCombo->addItem("Off", "off");
    ledGpioModeCombo->addItem("Torch", "torch");
    ledGpioModeCombo->addItem("Strobe", "strobe");
    ledGpioModeCombo->setToolTip(
        "Torch: GPIO stays high during capture (continuous light)\n"
        "Strobe: GPIO pulses high before capture (flash, hardware timeout)");
    connect(ledGpioModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onLedGpioModeChanged);
    layout->addWidget(ledGpioModeCombo, 4, 1);

    layout->addWidget(new QLabel("Pre-frame Delay:"), 4, 2);
    ledPreFrameDelaySpinBox = new QSpinBox();
    ledPreFrameDelaySpinBox->setRange(0, 500);
    ledPreFrameDelaySpinBox->setValue(0);
    ledPreFrameDelaySpinBox->setSuffix(" ms");
    ledPreFrameDelaySpinBox->setEnabled(false);
    ledPreFrameDelaySpinBox->setToolTip(
        "Time to wait after asserting GPIO before capturing.\n"
        "Allows LEDs to stabilize.");
    connect(ledPreFrameDelaySpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onLedGpioModeChanged);
    layout->addWidget(ledPreFrameDelaySpinBox, 4, 3);

    ledPostCaptureOffCheckBox = new QCheckBox("Turn off after capture");
    ledPostCaptureOffCheckBox->setChecked(false);
    ledPostCaptureOffCheckBox->setEnabled(false);
    ledPostCaptureOffCheckBox->setToolTip("Torch mode only: turn off LEDs after each capture");
    connect(ledPostCaptureOffCheckBox, &QCheckBox::toggled,
            this, &MainWindow::onLedGpioModeChanged);
    layout->addWidget(ledPostCaptureOffCheckBox, 4, 4);
    ledAutoExposureWarningLabel = new QLabel(
        "⚠ Auto exposure will converge under ambient light, not LED. "
        "Consider using manual exposure/gain for consistent LED captures.");
    ledAutoExposureWarningLabel->setStyleSheet(
        "QLabel { color: #e67e22; font-style: italic; font-size: 11px; }");
    ledAutoExposureWarningLabel->setWordWrap(true);
    ledAutoExposureWarningLabel->setVisible(true);
    layout->addWidget(ledAutoExposureWarningLabel, 5, 0, 1, 5); // span full row

    return group;
}

// ============================================================================
// SINGLE CAPTURE RGB CONTROLS GROUP
// ============================================================================
ExpandableGroupBox *MainWindow::setUpSingleCaptureRGBControlsGroup(QWidget *parent)
{
    singleCaptureRGBControlsGroupBox = new ExpandableGroupBox("RGB Controls (Capture)", parent);
    QFormLayout *controlsLayout = new QFormLayout(singleCaptureRGBControlsGroupBox->getContentWidget());

    // Exposure
    ExpandableGroupBox *exposureGroup = new ExpandableGroupBox("Exposure", singleCaptureRGBControlsGroupBox);
    QFormLayout *expLayout = new QFormLayout(exposureGroup->getContentWidget());
    rgbCaptureAutoExposureCheckBox = new QCheckBox();
    rgbCaptureAutoExposureCheckBox->setChecked(true);
    expLayout->addRow("Auto Exposure:", rgbCaptureAutoExposureCheckBox);
    rgbCaptureEvCompensationSpinBox = new QDoubleSpinBox();
    rgbCaptureEvCompensationSpinBox->setRange(-4.0, 4.0);
    rgbCaptureEvCompensationSpinBox->setSingleStep(0.5);
    rgbCaptureEvCompensationSpinBox->setValue(0.0);
    expLayout->addRow("EV Compensation:", rgbCaptureEvCompensationSpinBox);
    rgbCaptureExposureSpinBox = new QSpinBox();
    rgbCaptureExposureSpinBox->setRange(1, 1000000);
    rgbCaptureExposureSpinBox->setValue(100000);
    rgbCaptureExposureSpinBox->setSuffix(" µs");
    rgbCaptureExposureSpinBox->setEnabled(false);
    expLayout->addRow("Exposure Time:", rgbCaptureExposureSpinBox);
    controlsLayout->addRow(exposureGroup);
    rgbCaptureExposureHintLabel = new QLabel("Also disable Auto Analog Gain for full manual control");
    rgbCaptureExposureHintLabel->setStyleSheet("color: #e6224d; font-style: italic; font-size: 11px;");
    rgbCaptureExposureHintLabel->setWordWrap(true);
    rgbCaptureExposureHintLabel->setVisible(false);
    expLayout->addRow("", rgbCaptureExposureHintLabel);
    connect(rgbCaptureExposureSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v)
            { onCameraParamChanged(CameraParam::RGB_CAPTURE_EXPOSURE, v); });
    connect(rgbCaptureAutoExposureCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            {
                onCameraParamChanged(CameraParam::RGB_CAPTURE_AUTO_EXPOSURE, c);
                rgbCaptureExposureSpinBox->setEnabled(!c);
                rgbCaptureEvCompensationSpinBox->setEnabled(c);
                updateimx708ExposureHint(); });
    connect(rgbCaptureEvCompensationSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_CAPTURE_EV_COMPENSATION, v); });
    connect(rgbCaptureAutoExposureCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { updateLedAutoExposureWarning(); });

    // Focus
    ExpandableGroupBox *focusGroup = new ExpandableGroupBox("Focus", singleCaptureRGBControlsGroupBox);
    QFormLayout *focusLayout = new QFormLayout(focusGroup->getContentWidget());
    rgbCaptureAutoFocusCheckBox = new QCheckBox();
    rgbCaptureAutoFocusCheckBox->setChecked(true);
    focusLayout->addRow("Auto Focus:", rgbCaptureAutoFocusCheckBox);
    rgbCaptureLensPositionSpinBox = new QDoubleSpinBox();
    rgbCaptureLensPositionSpinBox->setRange(0.0, 12.0);
    rgbCaptureLensPositionSpinBox->setSingleStep(0.1);
    rgbCaptureLensPositionSpinBox->setValue(0.0);
    rgbCaptureLensPositionSpinBox->setEnabled(false);
    focusLayout->addRow("Lens Position:", rgbCaptureLensPositionSpinBox);
    controlsLayout->addRow(focusGroup);

    connect(rgbCaptureAutoFocusCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onCameraParamChanged(CameraParam::RGB_CAPTURE_AUTO_FOCUS, c); });
    connect(rgbCaptureLensPositionSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_CAPTURE_LENS_POSITION, v); });

    // Gain
    ExpandableGroupBox *gainGroup = new ExpandableGroupBox("Gain", singleCaptureRGBControlsGroupBox);
    QFormLayout *gainLayout = new QFormLayout(gainGroup->getContentWidget());
    rgbCaptureAutoAnalogGainCheckBox = new QCheckBox();
    rgbCaptureAutoAnalogGainCheckBox->setChecked(true);
    gainLayout->addRow("Auto Analog Gain:", rgbCaptureAutoAnalogGainCheckBox);
    rgbCaptureAnalogGainSpinBox = new QDoubleSpinBox();
    rgbCaptureAnalogGainSpinBox->setRange(1.0, 8.57);
    rgbCaptureAnalogGainSpinBox->setSingleStep(0.1);
    rgbCaptureAnalogGainSpinBox->setValue(1.0);
    rgbCaptureAnalogGainSpinBox->setEnabled(false);
    gainLayout->addRow("Analog Gain:", rgbCaptureAnalogGainSpinBox);
    rgbCaptureDigitalGainSpinBox = new QDoubleSpinBox();
    rgbCaptureDigitalGainSpinBox->setRange(1.0, 4.0);
    rgbCaptureDigitalGainSpinBox->setSingleStep(0.1);
    rgbCaptureDigitalGainSpinBox->setValue(1.0);
    rgbCaptureDigitalGainSpinBox->setEnabled(false);
    gainLayout->addRow("Digital Gain:", rgbCaptureDigitalGainSpinBox);
    controlsLayout->addRow(gainGroup);

    connect(rgbCaptureAutoAnalogGainCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onCameraParamChanged(CameraParam::RGB_CAPTURE_AUTO_ANALOG_GAIN, c); });
    connect(rgbCaptureAnalogGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_CAPTURE_ANALOG_GAIN, v); });
    connect(rgbCaptureDigitalGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_CAPTURE_DIGITAL_GAIN, v); });

    // White Balance
    ExpandableGroupBox *wbGroup = new ExpandableGroupBox("White Balance", singleCaptureRGBControlsGroupBox);
    QFormLayout *wbLayout = new QFormLayout(wbGroup->getContentWidget());
    rgbCaptureAutoWhiteBalanceCheckBox = new QCheckBox();
    rgbCaptureAutoWhiteBalanceCheckBox->setChecked(true);
    wbLayout->addRow("Auto WB:", rgbCaptureAutoWhiteBalanceCheckBox);
    rgbCaptureRedGainSpinBox = new QDoubleSpinBox();
    rgbCaptureRedGainSpinBox->setRange(0.0, 32.0);
    rgbCaptureRedGainSpinBox->setSingleStep(0.1);
    rgbCaptureRedGainSpinBox->setValue(1.0);
    wbLayout->addRow("Red Gain:", rgbCaptureRedGainSpinBox);
    rgbCaptureBlueGainSpinBox = new QDoubleSpinBox();
    rgbCaptureBlueGainSpinBox->setRange(0.0, 32.0);
    rgbCaptureBlueGainSpinBox->setSingleStep(0.1);
    rgbCaptureBlueGainSpinBox->setValue(1.0);
    wbLayout->addRow("Blue Gain:", rgbCaptureBlueGainSpinBox);
    controlsLayout->addRow(wbGroup);

    connect(rgbCaptureAutoWhiteBalanceCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onCameraParamChanged(CameraParam::RGB_CAPTURE_AUTO_WB, c); });
    connect(rgbCaptureRedGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_CAPTURE_RED_GAIN, v); });
    connect(rgbCaptureBlueGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::RGB_CAPTURE_BLUE_GAIN, v); });

    ExpandableGroupBox *rawGroup = new ExpandableGroupBox("Raw Capture", singleCaptureRGBControlsGroupBox);
    QFormLayout *rawLayout = new QFormLayout(rawGroup->getContentWidget());

    rgbCaptureRawModeCheckBox = new QCheckBox();
    rgbCaptureRawModeCheckBox->setChecked(false);
    rawLayout->addRow("Raw Bayer Mode:", rgbCaptureRawModeCheckBox);

    QLabel *rawInfoLabel = new QLabel("Captures 10-bit RGGB Bayer data");
    rawInfoLabel->setStyleSheet("color: gray; font-style: italic;");
    rawLayout->addRow("", rawInfoLabel);

    controlsLayout->addRow(rawGroup);

    connect(rgbCaptureRawModeCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { 
            onCameraParamChanged(CameraParam::RGB_CAPTURE_RAW_MODE, c);
            singleCaptureQualitySpinBox->setEnabled(!c); });

    return singleCaptureRGBControlsGroupBox;
}

// ============================================================================
// SINGLE CAPTURE ARDUCAM CONTROLS GROUP
// ============================================================================
ExpandableGroupBox *MainWindow::setUpSingleCaptureArducamControlsGroup(QWidget *parent)
{
    singleCaptureArducamControlsGroupBox = new ExpandableGroupBox("Arducam Controls (Capture)", parent);
    singleCaptureArducamControlsGroupBox->setVisible(false);
    QFormLayout *controlsLayout = new QFormLayout(singleCaptureArducamControlsGroupBox->getContentWidget());

    // Exposure
    ExpandableGroupBox *exposureGroup = new ExpandableGroupBox("Exposure", singleCaptureArducamControlsGroupBox);
    QFormLayout *expLayout = new QFormLayout(exposureGroup->getContentWidget());
    arducamCaptureAutoExposureCheckBox = new QCheckBox();
    arducamCaptureAutoExposureCheckBox->setChecked(true);
    expLayout->addRow("Auto Exposure:", arducamCaptureAutoExposureCheckBox);
    arducamCaptureEvCompensationSpinBox = new QDoubleSpinBox();
    arducamCaptureEvCompensationSpinBox->setObjectName("arducamCaptureEvCompensationSpinBox");
    arducamCaptureEvCompensationSpinBox->setRange(-4.0, 4.0);
    arducamCaptureEvCompensationSpinBox->setSingleStep(0.5);
    arducamCaptureEvCompensationSpinBox->setValue(0.0);
    expLayout->addRow("EV Compensation:", arducamCaptureEvCompensationSpinBox);

    arducamCaptureExposureSpinBox = new QSpinBox();
    arducamCaptureExposureSpinBox->setRange(1, 1000000);
    arducamCaptureExposureSpinBox->setValue(10000);
    arducamCaptureExposureSpinBox->setSuffix(" µs");
    arducamCaptureExposureSpinBox->setEnabled(false);
    expLayout->addRow("Exposure Time:", arducamCaptureExposureSpinBox);
    arducamCaptureExposureHintLabel = new QLabel("Manual exposure works best with manual gain settings below");
    arducamCaptureExposureHintLabel->setStyleSheet("color: #e62222; font-style: italic; font-size: 11px;");
    arducamCaptureExposureHintLabel->setWordWrap(true);
    arducamCaptureExposureHintLabel->setVisible(false);
    expLayout->addRow("", arducamCaptureExposureHintLabel);
    controlsLayout->addRow(exposureGroup);

    connect(arducamCaptureExposureSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v)
            { onCameraParamChanged(CameraParam::ARDUCAM_CAPTURE_EXPOSURE, v); });
    connect(arducamCaptureAutoExposureCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { 
                onCameraParamChanged(CameraParam::ARDUCAM_CAPTURE_AUTO_EXPOSURE, c); 
                arducamCaptureExposureSpinBox->setEnabled(!c);
                arducamCaptureEvCompensationSpinBox->setEnabled(c);
                updateimx219ExposureHint(); });

    connect(arducamCaptureEvCompensationSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::ARDUCAM_CAPTURE_EV_COMPENSATION, v); });
    // Insert after: controlsLayout->addRow(exposureGroup);
    // Insert before: the Gain ExpandableGroupBox

    // Focus
    ExpandableGroupBox *focusGroup = new ExpandableGroupBox("Focus", singleCaptureArducamControlsGroupBox);
    QFormLayout *focusLayout = new QFormLayout(focusGroup->getContentWidget());

    arducamCaptureAutoFocusCheckBox = new QCheckBox();
    arducamCaptureAutoFocusCheckBox->setObjectName("arducamCaptureAutoFocusCheckBox");
    arducamCaptureAutoFocusCheckBox->setChecked(true);
    focusLayout->addRow("Auto Focus:", arducamCaptureAutoFocusCheckBox);

    arducamCaptureLensPositionSpinBox = new QDoubleSpinBox();
    arducamCaptureLensPositionSpinBox->setObjectName("arducamCaptureLensPositionSpinBox");
    arducamCaptureLensPositionSpinBox->setRange(0.0, 12.0);
    arducamCaptureLensPositionSpinBox->setSingleStep(0.1);
    arducamCaptureLensPositionSpinBox->setValue(0.0);
    arducamCaptureLensPositionSpinBox->setEnabled(false);
    focusLayout->addRow("Lens Position:", arducamCaptureLensPositionSpinBox);

    controlsLayout->addRow(focusGroup);

    connect(arducamCaptureAutoFocusCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            {
                onCameraParamChanged(CameraParam::ARDUCAM_CAPTURE_AUTO_FOCUS, c);
                arducamCaptureLensPositionSpinBox->setEnabled(!c); });
    connect(arducamCaptureLensPositionSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::ARDUCAM_CAPTURE_LENS_POSITION, v); });

    // Gain
    ExpandableGroupBox *gainGroup = new ExpandableGroupBox("Gain", singleCaptureArducamControlsGroupBox);
    QFormLayout *gainLayout = new QFormLayout(gainGroup->getContentWidget());
    arducamCaptureAnalogGainSpinBox = new QDoubleSpinBox();
    arducamCaptureAnalogGainSpinBox->setRange(1.0, 10.66);
    arducamCaptureAnalogGainSpinBox->setSingleStep(0.1);
    arducamCaptureAnalogGainSpinBox->setValue(1.0);
    gainLayout->addRow("Analog Gain:", arducamCaptureAnalogGainSpinBox);
    QLabel *digitalGainHintLabel = new QLabel("Digital gain is not supported on IMX219 (controlled by ISP)");
    digitalGainHintLabel->setStyleSheet("color: #888888; font-style: italic; font-size: 11px;");
    digitalGainHintLabel->setWordWrap(true);
    gainLayout->addRow("Digital Gain:", digitalGainHintLabel);
    controlsLayout->addRow(gainGroup);

    connect(arducamCaptureAnalogGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::ARDUCAM_CAPTURE_ANALOG_GAIN, v); });

    // White Balance
    ExpandableGroupBox *wbGroup = new ExpandableGroupBox("White Balance", singleCaptureArducamControlsGroupBox);
    QFormLayout *wbLayout = new QFormLayout(wbGroup->getContentWidget());
    arducamCaptureAutoWhiteBalanceCheckBox = new QCheckBox();
    arducamCaptureAutoWhiteBalanceCheckBox->setChecked(true);
    wbLayout->addRow("Auto WB:", arducamCaptureAutoWhiteBalanceCheckBox);
    arducamCaptureRedGainSpinBox = new QDoubleSpinBox();
    arducamCaptureRedGainSpinBox->setRange(0.0, 32.0);
    arducamCaptureRedGainSpinBox->setSingleStep(0.1);
    arducamCaptureRedGainSpinBox->setValue(1.0);
    wbLayout->addRow("Red Gain:", arducamCaptureRedGainSpinBox);
    arducamCaptureBlueGainSpinBox = new QDoubleSpinBox();
    arducamCaptureBlueGainSpinBox->setRange(0.0, 32.0);
    arducamCaptureBlueGainSpinBox->setSingleStep(0.1);
    arducamCaptureBlueGainSpinBox->setValue(1.0);
    wbLayout->addRow("Blue Gain:", arducamCaptureBlueGainSpinBox);
    controlsLayout->addRow(wbGroup);

    connect(arducamCaptureAutoWhiteBalanceCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onCameraParamChanged(CameraParam::ARDUCAM_CAPTURE_AUTO_WB, c); });
    connect(arducamCaptureRedGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::ARDUCAM_CAPTURE_RED_GAIN, v); });
    connect(arducamCaptureBlueGainSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::ARDUCAM_CAPTURE_BLUE_GAIN, v); });

    ExpandableGroupBox *rawGroup = new ExpandableGroupBox("Raw Capture", singleCaptureArducamControlsGroupBox);
    QFormLayout *rawLayout = new QFormLayout(rawGroup->getContentWidget());

    arducamCaptureRawModeCheckBox = new QCheckBox();
    arducamCaptureRawModeCheckBox->setChecked(false);
    rawLayout->addRow("Raw Bayer Mode:", arducamCaptureRawModeCheckBox);

    QLabel *rawInfoLabel = new QLabel("Captures 10-bit RGGB Bayer data");
    rawInfoLabel->setStyleSheet("color: gray; font-style: italic;");
    rawLayout->addRow("", rawInfoLabel);

    controlsLayout->addRow(rawGroup);

    connect(arducamCaptureRawModeCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { 
            onCameraParamChanged(CameraParam::ARDUCAM_CAPTURE_RAW_MODE, c);
            singleCaptureQualitySpinBox->setEnabled(!c); });

    return singleCaptureArducamControlsGroupBox;
}

ExpandableGroupBox *MainWindow::setUpIntervalCaptureGroup(QWidget *parent)
{
    auto *group = new ExpandableGroupBox("Interval Capture", parent);
    group->setExpanded(false);

    auto *layout = new QGridLayout(group->getContentWidget());
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    // Row 0 — interval duration
    layout->addWidget(new QLabel("Interval:"), 0, 0);

    intervalStillIntervalSpinBox = new QDoubleSpinBox();
    intervalStillIntervalSpinBox->setRange(0.1, 60.0);
    intervalStillIntervalSpinBox->setValue(1.0);
    intervalStillIntervalSpinBox->setSingleStep(0.5);
    intervalStillIntervalSpinBox->setDecimals(1);
    intervalStillIntervalSpinBox->setSuffix(" s");
    intervalStillIntervalSpinBox->setToolTip(
        "Time between captures (uses Single Capture camera, resolution, and quality settings)");
    layout->addWidget(intervalStillIntervalSpinBox, 0, 1);

    // Row 1 — start / stop buttons
    intervalStillStartButton = new QPushButton("▶  Start Interval");
    intervalStillStartButton->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; }");

    intervalStillStopButton = new QPushButton("■  Stop Interval");
    intervalStillStopButton->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; }");
    intervalStillStopButton->setEnabled(false);

    layout->addWidget(intervalStillStartButton, 1, 0);
    layout->addWidget(intervalStillStopButton, 1, 1);

    // Row 2 — live status
    intervalStillStatusLabel = new QLabel("Idle");
    intervalStillStatusLabel->setStyleSheet("color: grey; font-style: italic;");
    layout->addWidget(intervalStillStatusLabel, 2, 0, 1, 2);

    connect(intervalStillStartButton, &QPushButton::clicked,
            this, &MainWindow::onIntervalStillStart);
    connect(intervalStillStopButton, &QPushButton::clicked,
            this, &MainWindow::onIntervalStillStop);

    // Row 3 — auto-save checkbox + folder button on same row
    intervalStillAutoSaveCheckBox = new QCheckBox("Auto-Save Images");
    intervalStillAutoSaveCheckBox->setChecked(false);
    connect(intervalStillAutoSaveCheckBox, &QCheckBox::toggled,
            this, [this](bool checked)
            {
                intervalStillFolderButton->setEnabled(checked);
                intervalStillFolderLabel->setEnabled(checked);
                if (checked && intervalStillSaveFolder.isEmpty())
                    onSelectIntervalStillFolder(); // prompt immediately if no folder yet
            });
    layout->addWidget(intervalStillAutoSaveCheckBox, 3, 0);

    intervalStillFolderButton = new QPushButton("📁  Select Folder…");
    intervalStillFolderButton->setEnabled(false); // disabled until checkbox on
    intervalStillFolderButton->setToolTip("Select folder for auto-saved images");
    connect(intervalStillFolderButton, &QPushButton::clicked,
            this, &MainWindow::onSelectIntervalStillFolder);
    layout->addWidget(intervalStillFolderButton, 3, 1);

    // Row 4 — current folder display
    intervalStillFolderLabel = new QLabel("No folder selected");
    intervalStillFolderLabel->setStyleSheet("color: grey; font-style: italic;");
    intervalStillFolderLabel->setEnabled(false);
    intervalStillFolderLabel->setWordWrap(true);
    layout->addWidget(intervalStillFolderLabel, 4, 0, 1, 2);
    return group;
}

ExpandableGroupBox *MainWindow::setUpSessionGroup(QWidget *parent)
{
    auto *group = new ExpandableGroupBox("Session", parent);
    group->setExpanded(true);
    auto *layout = new QGridLayout(group->getContentWidget());
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    // Row 0 — session ID display
    layout->addWidget(new QLabel("Session ID:"), 0, 0);
    sessionIdLabel = new QLabel("—");
    sessionIdLabel->setStyleSheet("font-family: monospace; color: #27ae60;");
    sessionIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(sessionIdLabel, 0, 1, 1, 2);

    // Row 1 — output folder
    layout->addWidget(new QLabel("Output Folder:"), 1, 0);
    sessionFolderLabel = new QLabel("No folder selected");
    sessionFolderLabel->setStyleSheet("color: grey; font-style: italic;");
    sessionFolderLabel->setWordWrap(true);
    layout->addWidget(sessionFolderLabel, 1, 1);

    QPushButton *folderBtn = new QPushButton("📁 Select…");
    folderBtn->setMaximumWidth(90);
    connect(folderBtn, &QPushButton::clicked,
            this, &MainWindow::onSelectSessionOutputFolder);
    layout->addWidget(folderBtn, 1, 2);

    // Row 2 — frame count display
    sessionFrameCountLabel = new QLabel("");
    sessionFrameCountLabel->setStyleSheet("color: grey; font-style: italic; font-size: 11px;");
    layout->addWidget(sessionFrameCountLabel, 2, 0, 1, 3);

    return group;
}

// ============================================================
//   LED Control Group setup
// ============================================================

ExpandableGroupBox *MainWindow::setupLedControlGroup()
{
    ExpandableGroupBox *ledGroup = new ExpandableGroupBox("💡 LED Control (LM3643)", this);
    ledGroup->setExpanded(false);
    QVBoxLayout *mainLayout = new QVBoxLayout(ledGroup->getContentWidget());

// --- External control checkbox ---
    ledExternalControlCheckBox = new QCheckBox("LEDs managed by external application");
    ledExternalControlCheckBox->setToolTip(
        "When checked, LED arming (brightness, mode) is handled by the\n"
        "customer's out-of-band application. Manual LM3643 controls below\n"
        "are disabled. GPIO trigger settings in Single Capture remain active.");
    ledExternalControlCheckBox->setChecked(
        settings->value("led/external_control", false).toBool());

    QLabel *externalInfoLabel = new QLabel(
        "GPIO trigger (LED Mode, Pre-frame Delay) in Single Capture is still active.");
    externalInfoLabel->setStyleSheet("color: #666; font-style: italic; font-size: 11px;");
    externalInfoLabel->setWordWrap(true);
    externalInfoLabel->setVisible(ledExternalControlCheckBox->isChecked());

    mainLayout->addWidget(ledExternalControlCheckBox);
    mainLayout->addWidget(externalInfoLabel);

    connect(ledExternalControlCheckBox, &QCheckBox::toggled,
            this, [this, externalInfoLabel](bool checked)
            {
                settings->setValue("led/external_control", checked);
                externalInfoLabel->setVisible(checked);
                applyLedExternalControlState(checked);
            });


    // Top-level controls: LED selector, brightness, All-Off
    QGridLayout *topLayout = new QGridLayout();

    topLayout->addWidget(new QLabel("LED ID (0-31):"), 0, 0);
    ledIdSpinBox = new QSpinBox();
    ledIdSpinBox->setRange(0, 31);
    ledIdSpinBox->setValue(0);
    ledIdSpinBox->setEnabled(false);
    topLayout->addWidget(ledIdSpinBox, 0, 1);

    topLayout->addWidget(new QLabel("Brightness:"), 0, 2);
    ledBrightnessSlider = new QSlider(Qt::Horizontal);
    ledBrightnessSlider->setRange(0, 255);
    ledBrightnessSlider->setValue(128);
    ledBrightnessSlider->setEnabled(false);
    topLayout->addWidget(ledBrightnessSlider, 0, 3);

    ledBrightnessValueLabel = new QLabel("128");
    ledBrightnessValueLabel->setMinimumWidth(30);
    topLayout->addWidget(ledBrightnessValueLabel, 0, 4);

    connect(ledBrightnessSlider, &QSlider::valueChanged,
            this, [this](int v)
            { ledBrightnessValueLabel->setText(QString::number(v)); });

    ledAllOffButton = new QPushButton("All LEDs Off");
    ledAllOffButton->setEnabled(false);
    connect(ledAllOffButton, &QPushButton::clicked, this, &MainWindow::onLedAllOff);
    topLayout->addWidget(ledAllOffButton, 0, 5);

    ledStatusLabel = new QLabel("Not available");
    topLayout->addWidget(ledStatusLabel, 0, 6);

    mainLayout->addLayout(topLayout);

    // Torch control
    ExpandableGroupBox *torchGroup = new ExpandableGroupBox("Torch Control", ledGroup);
    QHBoxLayout *torchBtnLayout = new QHBoxLayout(torchGroup->getContentWidget());

    ledTorchButton = new QPushButton("🔦 Set Torch");
    ledTorchButton->setEnabled(false);
    connect(ledTorchButton, &QPushButton::clicked, this, &MainWindow::onLedTorch);
    torchBtnLayout->addWidget(ledTorchButton);

    ledOffButton = new QPushButton("Turn Off");
    ledOffButton->setEnabled(false);
    connect(ledOffButton, &QPushButton::clicked, this, &MainWindow::onLedOff);
    torchBtnLayout->addWidget(ledOffButton);

    torchBtnLayout->addStretch();
    mainLayout->addWidget(torchGroup);

    // Flash control
    ExpandableGroupBox *flashGroup = new ExpandableGroupBox("Flash Control", ledGroup);
    QGridLayout *flashLayout = new QGridLayout(flashGroup->getContentWidget());

    flashLayout->addWidget(new QLabel("Duration:"), 0, 0);
    ledFlashDurationSpinBox = new QSpinBox();
    ledFlashDurationSpinBox->setRange(1, 400000);   // 1 µs – 400 ms
    ledFlashDurationSpinBox->setValue(200000);        // 200 ms default
    ledFlashDurationSpinBox->setSuffix(" µs");
    ledFlashDurationSpinBox->setSingleStep(1000);    // 1 ms coarse step; user can type finer values
    ledFlashDurationSpinBox->setEnabled(false);
    flashLayout->addWidget(ledFlashDurationSpinBox, 0, 1);

    // Convenience label showing the value in ms
    QLabel *flashDurationMsLabel = new QLabel("(75.000 ms)");
    flashDurationMsLabel->setStyleSheet("color: #666; font-style: italic; font-size: 11px;");
    flashLayout->addWidget(flashDurationMsLabel, 0, 2);

    connect(ledFlashDurationSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [flashDurationMsLabel](int us)
            {
                flashDurationMsLabel->setText(
                    QString("(%1 ms)").arg(us / 1000.0, 0, 'f', 3));
            });

    ledFlashButton = new QPushButton("⚡ Flash");
    ledFlashButton->setEnabled(false);
    connect(ledFlashButton, &QPushButton::clicked, this, &MainWindow::onLedGpioFlash);
    flashLayout->addWidget(ledFlashButton, 0, 3);

    mainLayout->addWidget(flashGroup);

    return ledGroup;
}

ExpandableGroupBox *MainWindow::setUpStreamingFrameDurationGroup(QWidget *parent)
{
    streamingFrameDurationGroupBox = new ExpandableGroupBox("Frame Duration", parent);
    streamingFrameDurationGroupBox->setExpanded(false);
    QFormLayout *layout = new QFormLayout(streamingFrameDurationGroupBox->getContentWidget());

    // ── Frame duration lock ──────────────────────────────────────────────────
    frameDurationLockCheckBox = new QCheckBox();
    frameDurationLockCheckBox->setChecked(false);
    layout->addRow("Lock Frame Duration:", frameDurationLockCheckBox);

    QHBoxLayout *spinRow = new QHBoxLayout();
    frameDurationSpinBox = new QDoubleSpinBox();
    frameDurationSpinBox->setRange(33.0, 10000.0);
    frameDurationSpinBox->setSingleStep(1.0);
    frameDurationSpinBox->setDecimals(1);
    frameDurationSpinBox->setValue(33.3);
    frameDurationSpinBox->setSuffix(" ms");
    frameDurationSpinBox->setEnabled(false);
    spinRow->addWidget(frameDurationSpinBox);

    frameDurationFpsLabel = new QLabel("30.0 fps");
    frameDurationFpsLabel->setStyleSheet("color: #666; font-style: italic;");
    spinRow->addWidget(frameDurationFpsLabel);
    spinRow->addStretch();

    QWidget *spinWidget = new QWidget();
    spinWidget->setLayout(spinRow);
    layout->addRow("Duration:", spinWidget);

    // VBlank gap hint — hidden until lock is engaged and timing is known
    frameDurationVBlankLabel = new QLabel();
    frameDurationVBlankLabel->setStyleSheet("color: green; font-size: 11px; font-style: italic;");
    frameDurationVBlankLabel->setVisible(false);
    layout->addRow("", frameDurationVBlankLabel);

    // ── VBlank strobe sync ───────────────────────────────────────────────────
    strobeVBlankCheckBox = new QCheckBox();
    strobeVBlankCheckBox->setChecked(false);
    strobeVBlankCheckBox->setEnabled(false); // requires frame duration lock
    layout->addRow("VBlank Strobe Sync:", strobeVBlankCheckBox);

    QHBoxLayout *flashRow = new QHBoxLayout();
    strobeLeadTimeSpinBox = new QDoubleSpinBox();
    strobeLeadTimeSpinBox->setRange(0.1, 5.0);
    strobeLeadTimeSpinBox->setSingleStep(0.1);
    strobeLeadTimeSpinBox->setValue(1.0);
    strobeLeadTimeSpinBox->setDecimals(1);
    strobeLeadTimeSpinBox->setSuffix(" ms");
    strobeLeadTimeSpinBox->setEnabled(false);
    flashRow->addWidget(strobeLeadTimeSpinBox);
    flashRow->addStretch();

    QWidget *flashWidget = new QWidget();
    flashWidget->setLayout(flashRow);
    layout->addRow("Strobe Lead Time:", flashWidget);

    // Flash fit hint — hidden until strobe is on and timing is known
    frameDurationFlashHintLabel = new QLabel();
    frameDurationFlashHintLabel->setWordWrap(true);
    frameDurationFlashHintLabel->setVisible(false);
    layout->addRow("", frameDurationFlashHintLabel);

    QLabel *strobeHintLabel = new QLabel(
        "Turns strobe on just before readout starts and off at VBlank, "
        "illuminating the full frame. Requires frame duration lock.");
    strobeHintLabel->setStyleSheet("color: #666; font-style: italic; font-size: 11px;");
    strobeHintLabel->setWordWrap(true);
    layout->addRow("", strobeHintLabel);

    // ── Connections ──────────────────────────────────────────────────────────
    connect(frameDurationLockCheckBox, &QCheckBox::toggled,
            this, [this](bool locked)
            {
                frameDurationSpinBox->setEnabled(locked);
                strobeVBlankCheckBox->setEnabled(locked);

                // Turning off lock also disables strobe
                if (!locked && strobeVBlankCheckBox->isChecked())
                {
                    strobeVBlankCheckBox->blockSignals(true);
                    strobeVBlankCheckBox->setChecked(false);
                    strobeLeadTimeSpinBox->setEnabled(false);
                    strobeVBlankCheckBox->blockSignals(false);
                    // Notify server
                    QJsonObject cmd;
                    cmd[Param::COMMAND] = Command::LED_STROBE_SYNC_ENABLE;
                    cmd["enabled"] = false;
                    sendCommand(cmd);
                }

                QString camera = streamCameraSelector->currentData().toString();
                CameraParam enabledParam = (camera == Camera::IMX219)
                    ? CameraParam::ARDUCAM_STREAMING_FRAME_DURATION_ENABLED
                    : CameraParam::RGB_STREAMING_FRAME_DURATION_ENABLED;
                onCameraParamChanged(enabledParam, locked);
                if (locked)
                {
                    CameraParam durParam = (camera == Camera::IMX219)
                        ? CameraParam::ARDUCAM_STREAMING_FRAME_DURATION_US
                        : CameraParam::RGB_STREAMING_FRAME_DURATION_US;
                    int64_t us = static_cast<int64_t>(frameDurationSpinBox->value() * 1000.0);
                    onCameraParamChanged(durParam, QString::number(us));
                }
                updateFrameDurationTimingHints();
            });

    connect(frameDurationSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double ms)
            {
                if (ms > 0.0)
                    frameDurationFpsLabel->setText(
                        QString("%1 fps").arg(1000.0 / ms, 0, 'f', 1));

                if (!frameDurationLockCheckBox->isChecked())
                    return;

                QString camera = streamCameraSelector->currentData().toString();
                CameraParam durParam = (camera == Camera::IMX219)
                    ? CameraParam::ARDUCAM_STREAMING_FRAME_DURATION_US
                    : CameraParam::RGB_STREAMING_FRAME_DURATION_US;
                int64_t us = static_cast<int64_t>(ms * 1000.0);
                onCameraParamChanged(durParam, QString::number(us));
                updateFrameDurationTimingHints();
            });

    connect(strobeVBlankCheckBox, &QCheckBox::toggled,
        this, &MainWindow::onStrobeVBlankToggled);

    // Flash duration — update hint in addition to existing server command
    connect(strobeLeadTimeSpinBox,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double ms)
            {
                updateFrameDurationTimingHints();

                if (!strobeVBlankCheckBox->isChecked())
                    return;

                QJsonObject cmd;
                cmd[Param::COMMAND] = Command::LED_STROBE_SYNC_ENABLE;
                cmd["enabled"] = true;
                cmd[Param::LED_STROBE_LEAD_TIME_MS] = ms;
                sendCommand(cmd);
            });

    return streamingFrameDurationGroupBox;
}

// ============================================================================
// SINGLE CAPTURE THERMAL CONTROLS GROUP
// ============================================================================
ExpandableGroupBox *MainWindow::setUpSingleCaptureThermalControlsGroup(QWidget *parent)
{
    singleCaptureThermalControlsGroupBox = new ExpandableGroupBox("Thermal Controls (Capture)", parent);
    singleCaptureThermalControlsGroupBox->setVisible(false);
    QFormLayout *controlsLayout = new QFormLayout(singleCaptureThermalControlsGroupBox->getContentWidget());

    // Emissivity
    ExpandableGroupBox *emGroup = new ExpandableGroupBox("Emissivity", singleCaptureThermalControlsGroupBox);
    QFormLayout *emLayout = new QFormLayout(emGroup->getContentWidget());
    thermalCaptureEmissivitySpinBox = new QDoubleSpinBox();
    thermalCaptureEmissivitySpinBox->setRange(0.1, 1.0);
    thermalCaptureEmissivitySpinBox->setSingleStep(0.01);
    thermalCaptureEmissivitySpinBox->setValue(0.95);
    emLayout->addRow("Emissivity:", thermalCaptureEmissivitySpinBox);
    controlsLayout->addRow(emGroup);

    connect(thermalCaptureEmissivitySpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::THERMAL_CAPTURE_EMISSIVITY, v); });

    // Temperature Range
    ExpandableGroupBox *tempGroup = new ExpandableGroupBox("Temperature Range", singleCaptureThermalControlsGroupBox);
    QFormLayout *tempLayout = new QFormLayout(tempGroup->getContentWidget());
    thermalCaptureMinTempSpinBox = new QDoubleSpinBox();
    thermalCaptureMinTempSpinBox->setRange(-40.0, 300.0);
    thermalCaptureMinTempSpinBox->setSingleStep(1.0);
    thermalCaptureMinTempSpinBox->setValue(20.0);
    thermalCaptureMinTempSpinBox->setSuffix(" °C");
    tempLayout->addRow("Min Temperature:", thermalCaptureMinTempSpinBox);
    thermalCaptureMaxTempSpinBox = new QDoubleSpinBox();
    thermalCaptureMaxTempSpinBox->setRange(-40.0, 300.0);
    thermalCaptureMaxTempSpinBox->setSingleStep(1.0);
    thermalCaptureMaxTempSpinBox->setValue(40.0);
    thermalCaptureMaxTempSpinBox->setSuffix(" °C");
    tempLayout->addRow("Max Temperature:", thermalCaptureMaxTempSpinBox);
    controlsLayout->addRow(tempGroup);

    connect(thermalCaptureMinTempSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::THERMAL_CAPTURE_MIN_TEMP, v); });
    connect(thermalCaptureMaxTempSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::THERMAL_CAPTURE_MAX_TEMP, v); });

    // Colormap
    ExpandableGroupBox *cmGroup = new ExpandableGroupBox("Visualization", singleCaptureThermalControlsGroupBox);
    QFormLayout *cmLayout = new QFormLayout(cmGroup->getContentWidget());
    thermalCaptureColormapCombo = new QComboBox();
    thermalCaptureColormapCombo->addItem("Ironbow", Colormap::IRONBOW);
    thermalCaptureColormapCombo->addItem("Rainbow", Colormap::RAINBOW);
    thermalCaptureColormapCombo->addItem("Grayscale", Colormap::GRAYSCALE);
    thermalCaptureColormapCombo->addItem("Hot", Colormap::HOT);
    thermalCaptureColormapCombo->addItem("Jet", Colormap::JET);
    cmLayout->addRow("Colormap:", thermalCaptureColormapCombo);
    controlsLayout->addRow(cmGroup);

    connect(thermalCaptureColormapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i)
            { onCameraParamChanged(CameraParam::THERMAL_CAPTURE_COLORMAP,
                                   thermalCaptureColormapCombo->itemData(i).toString()); });

    // NUC
    ExpandableGroupBox *nucGroup = new ExpandableGroupBox("Calibration", singleCaptureThermalControlsGroupBox);
    QFormLayout *nucLayout = new QFormLayout(nucGroup->getContentWidget());
    thermalCaptureNucEnabledCheckBox = new QCheckBox();
    thermalCaptureNucEnabledCheckBox->setChecked(true);
    nucLayout->addRow("Enable NUC:", thermalCaptureNucEnabledCheckBox);
    controlsLayout->addRow(nucGroup);

    connect(thermalCaptureNucEnabledCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { onCameraParamChanged(CameraParam::THERMAL_CAPTURE_NUC_ENABLED, c); });

    // Alarm
    ExpandableGroupBox *alarmGroup = new ExpandableGroupBox("Temperature Alarm", singleCaptureThermalControlsGroupBox);
    QFormLayout *alarmLayout = new QFormLayout(alarmGroup->getContentWidget());
    thermalCaptureAlarmEnabledCheckBox = new QCheckBox();
    thermalCaptureAlarmEnabledCheckBox->setChecked(false);
    alarmLayout->addRow("Enable Alarm:", thermalCaptureAlarmEnabledCheckBox);
    thermalCaptureAlarmTempSpinBox = new QDoubleSpinBox();
    thermalCaptureAlarmTempSpinBox->setRange(-40.0, 300.0);
    thermalCaptureAlarmTempSpinBox->setSingleStep(0.5);
    thermalCaptureAlarmTempSpinBox->setValue(38.0);
    thermalCaptureAlarmTempSpinBox->setSuffix(" °C");
    thermalCaptureAlarmTempSpinBox->setEnabled(false);
    alarmLayout->addRow("Alarm Temperature:", thermalCaptureAlarmTempSpinBox);
    controlsLayout->addRow(alarmGroup);

    connect(thermalCaptureAlarmEnabledCheckBox, &QCheckBox::toggled,
            this, [this](bool c)
            { 
                onCameraParamChanged(CameraParam::THERMAL_CAPTURE_ALARM_ENABLED, c);
                thermalCaptureAlarmTempSpinBox->setEnabled(c); });
    connect(thermalCaptureAlarmTempSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v)
            { onCameraParamChanged(CameraParam::THERMAL_CAPTURE_ALARM_TEMP, v); });

    return singleCaptureThermalControlsGroupBox;
}


void MainWindow::addFocusSweepWidgets(
    QFormLayout *focusLayout,
    QPushButton *&btn, QLabel *&lbl, QProgressBar *&bar,
    CameraParam autoParam, CameraParam lensParam,
    QDoubleSpinBox *&lensSpinBox, const QString &camera)
{
    // --- Sweep button row ---
    QHBoxLayout *sweepRow = new QHBoxLayout();

    btn = new QPushButton("🔍 Test Focus Sweep");
    btn->setToolTip("Steps lens from 0 → 10 in 0.5 increments.\nAuto focus is temporarily disabled.");
    sweepRow->addWidget(btn);

    lbl = new QLabel("—");
    lbl->setMinimumWidth(55);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sweepRow->addWidget(lbl);

    bar = new QProgressBar();
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(false);
    bar->setMaximumHeight(12);
    bar->setMaximumWidth(120);
    sweepRow->addWidget(bar);

    QWidget *sweepWidget = new QWidget();
    sweepWidget->setLayout(sweepRow);
    focusLayout->addRow(sweepWidget);

    // --- Save images row (shared state, independent widgets per panel) ---
    QHBoxLayout *saveRow = new QHBoxLayout();

    QCheckBox *saveCheck = new QCheckBox("Save sweep images");
    saveCheck->setChecked(false);
    saveRow->addWidget(saveCheck);

    QPushButton *folderBtn = new QPushButton("📁 Folder…");
    folderBtn->setEnabled(false);
    folderBtn->setMaximumWidth(90);
    saveRow->addWidget(folderBtn);

    QLabel *folderLbl = new QLabel("No folder selected");
    folderLbl->setStyleSheet("color: grey; font-style: italic; font-size: 11px;");
    folderLbl->setWordWrap(false);
    saveRow->addWidget(folderLbl, 1);

    QWidget *saveWidget = new QWidget();
    saveWidget->setLayout(saveRow);
    focusLayout->addRow(saveWidget);

    // Wire save checkbox — enables/disables the folder button and prompts
    // immediately if no folder selected yet
    connect(saveCheck, &QCheckBox::toggled, this, [this, folderBtn, folderLbl](bool checked)
            {
        folderBtn->setEnabled(checked);
        focusSweepSaveCheckBox = checked ? qobject_cast<QCheckBox*>(sender()) : nullptr;
        if (checked && focusSweepSaveFolder.isEmpty())
            onSelectFocusSweepFolder();
        // Sync the label to whatever folder is currently set
        if (!focusSweepSaveFolder.isEmpty()) {
            QString display = focusSweepSaveFolder;
            if (display.length() > 40) display = "…" + display.right(37);
            folderLbl->setText(display);
            folderLbl->setStyleSheet("color: #27ae60; font-style: normal; font-size: 11px;");
        } });

    // Wire folder button
    connect(folderBtn, &QPushButton::clicked, this, [this, folderLbl]()
            {
        onSelectFocusSweepFolder();
        if (!focusSweepSaveFolder.isEmpty()) {
            QString display = focusSweepSaveFolder;
            if (display.length() > 40) display = "…" + display.right(37);
            folderLbl->setText(display);
            folderLbl->setStyleSheet("color: #27ae60; font-style: normal; font-size: 11px;");
        } });

    // Wire sweep button — captures widget pointers by value at connection time
    QPushButton *sweepBtn = btn;
    QLabel *sweepLbl = lbl;
    QProgressBar *sweepBar = bar;
    QDoubleSpinBox *sweepSpinBox = lensSpinBox;
    connect(btn, &QPushButton::clicked, this,
            [this, autoParam, lensParam, sweepSpinBox, camera, saveCheck,
             sweepBtn, sweepLbl, sweepBar]()
            {
                startFocusSweep(autoParam, lensParam, sweepSpinBox,
                                sweepBtn, sweepLbl, sweepBar, camera,
                                saveCheck->isChecked() && !focusSweepSaveFolder.isEmpty());
            });
}

void MainWindow::startFocusSweep(
    CameraParam autoParam, CameraParam lensParam,
    QDoubleSpinBox *lensSpinBox,
    QPushButton *btn, QLabel *lbl, QProgressBar *bar,
    const QString &camera, bool saveImages)
{
    LOG_INFO << "startFocusSweep: camera=" << camera.toStdString()
             << " saveImages=" << saveImages
             << " streamingActive=" << streamingActive
             << " sweepActiveButton=" << sweepActiveButton
             << " timerActive=" << (focusSweepTimer ? focusSweepTimer->isActive() : false)
             << std::endl;

    if (!streamingActive)
    {
        QMessageBox::warning(this, "Streaming Required",
            "Please start a camera stream before running the focus sweep.");
        return;
    }
    
    // Stop if already running
    if (sweepActiveButton != nullptr)
    {
        LOG_INFO << "startFocusSweep: sweep already running, stopping" << std::endl;
        stopFocusSweep();
        return;
    }

    focusSweepStep = 0.5;
    focusSweepMax = (camera == Camera::IMX708) ? imx708LensMax : imx219LensMax;
    focusSweepPos = (camera == Camera::IMX708) ? imx708LensMin : imx219LensMin;

    LOG_INFO << "startFocusSweep: pos=" << focusSweepPos
             << " step=" << focusSweepStep
             << " max=" << focusSweepMax << std::endl;

    // Disable AF for the duration of the sweep
    QCheckBox *afBox = nullptr;
    if (autoParam == CameraParam::RGB_STREAMING_AUTO_FOCUS)
        afBox = rgbStreamingAutoFocusCheckBox;
    else if (autoParam == CameraParam::RGB_CAPTURE_AUTO_FOCUS)
        afBox = rgbCaptureAutoFocusCheckBox;
    else if (autoParam == CameraParam::ARDUCAM_STREAMING_AUTO_FOCUS)
        afBox = arducamStreamingAutoFocusCheckBox;
    else if (autoParam == CameraParam::ARDUCAM_CAPTURE_AUTO_FOCUS)
        afBox = arducamCaptureAutoFocusCheckBox;

    focusSweepWasAuto = afBox ? afBox->isChecked() : false;
    if (afBox && focusSweepWasAuto)
        afBox->setChecked(false);

    sweepAutoFocusParam = autoParam;
    sweepLensPositionParam = lensParam;
    sweepLensSpinBox = lensSpinBox;
    sweepActiveButton = btn;
    sweepActiveLabel = lbl;
    sweepActiveBar = bar;
    sweepActiveCamera = camera;
    sweepSaveImages = saveImages;
    pendingSweepSave = false;

    btn->setText("⏹ Stop Sweep");
    lbl->setText(QString("%1").arg(focusSweepPos, 0, 'f', 1));
    bar->setValue(0);

    // Kick off the first step — stream frames will drive the rest
    sweepFrameCounter = 0;
    sweepSettleFrames = 10;  // ~10 frames to let focus settle at each position
    advanceFocusSweep();
    LOG_INFO << "startFocusSweep: started, frame-driven" << std::endl;
}


void MainWindow::advanceFocusSweep()
{
    if (focusSweepPos > focusSweepMax + 1e-6)
    {
        LOG_INFO << "advanceFocusSweep: sweep complete" << std::endl;
        stopFocusSweep();
        return;
    }

    LOG_INFO << "advanceFocusSweep: pos=" << focusSweepPos << std::endl;

    // Update spinbox — fires valueChanged which sends set_parameter
    if (sweepLensSpinBox)
        sweepLensSpinBox->setValue(focusSweepPos);

    // Update progress widgets
    int pct = static_cast<int>((focusSweepPos / focusSweepMax) * 100.0);
    if (sweepActiveLabel)
        sweepActiveLabel->setText(QString("%1").arg(focusSweepPos, 0, 'f', 1));
    if (sweepActiveBar)
        sweepActiveBar->setValue(pct);

    if (sweepSaveImages)
    {
        pendingSweepSave = true;
        pendingSweepSaveLensPos = focusSweepPos;
    }
    // In no-save mode, onStreamFrameReceived will advance after settle frames
}

void MainWindow::onSelectFocusSweepFolder()
{
    QFileDialog dialog(this, "Select Focus Sweep Save Folder");
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setDirectory(focusSweepSaveFolder.isEmpty()
                            ? QDir::homePath()
                            : focusSweepSaveFolder);
    if (dialog.exec() == QDialog::Accepted)
    {
        QString folder = dialog.selectedFiles().first();
        if (!folder.isEmpty())
        {
            focusSweepSaveFolder = folder;
            addLog("Focus sweep save folder: " + folder, "success");
        }
    }
}

void MainWindow::saveFocusSweepImage(const QByteArray &jpegData, double lensPos,
                                     const QString &camera)
{
    if (focusSweepSaveFolder.isEmpty())
        return;

    QDir dir(focusSweepSaveFolder);
    if (!dir.exists())
    {
        addLog("Focus sweep folder missing: " + focusSweepSaveFolder, "error");
        return;
    }

    // e.g. imx708_focus_05.0.jpg, imx219_focus_07.5.jpg
    QString filename = QString("%1_focus_%2.jpg")
                           .arg(camera)
                           .arg(lensPos, 4, 'f', 1, '0');
    QString fullPath = dir.filePath(filename);

    QFile file(fullPath);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(jpegData);
        file.close();
        addLog(QString("Sweep image saved: %1").arg(filename), "info");
    }
    else
    {
        addLog("Failed to save sweep image: " + fullPath, "error");
    }
}

void MainWindow::stopFocusSweep()
{
    LOG_INFO << "stopFocusSweep called" << std::endl;
    if (focusSweepTimer)
        focusSweepTimer->stop();
    sweepSaveImages = false;
    pendingSweepSave = false;

    if (sweepActiveButton)
        sweepActiveButton->setText("🔍 Test Focus Sweep");
    if (sweepActiveLabel)
        sweepActiveLabel->setText("—");
    if (sweepActiveBar)
        sweepActiveBar->setValue(0);

    // Restore AF if it was on before the sweep
    if (focusSweepWasAuto)
    {
        QCheckBox *afBox = nullptr;
        if (sweepAutoFocusParam == CameraParam::RGB_STREAMING_AUTO_FOCUS)
            afBox = rgbStreamingAutoFocusCheckBox;
        else if (sweepAutoFocusParam == CameraParam::RGB_CAPTURE_AUTO_FOCUS)
            afBox = rgbCaptureAutoFocusCheckBox;
        else if (sweepAutoFocusParam == CameraParam::ARDUCAM_STREAMING_AUTO_FOCUS)
            afBox = arducamStreamingAutoFocusCheckBox;
        else if (sweepAutoFocusParam == CameraParam::ARDUCAM_CAPTURE_AUTO_FOCUS)
            afBox = arducamCaptureAutoFocusCheckBox;

        if (afBox)
            afBox->setChecked(true);
    }

    sweepActiveButton = nullptr;
    sweepActiveLabel = nullptr;
    sweepActiveBar = nullptr;
    sweepLensSpinBox = nullptr;
    sweepActiveCamera.clear();
}

// ===============================================================
// setUpSingleCaptureGroup()
// ============================================================================
ExpandableGroupBox *MainWindow::setUpSingleCaptureGroup()
{
    ExpandableGroupBox *captureGroup = new ExpandableGroupBox("📸 Single Capture", this);
    QVBoxLayout *mainLayout = new QVBoxLayout(captureGroup->getContentWidget());

    mainLayout->addWidget(setUpSingleCaptureSettingsGroup(captureGroup));
    mainLayout->addWidget(setUpSingleCaptureRGBControlsGroup(captureGroup));
    mainLayout->addWidget(setUpSingleCaptureArducamControlsGroup(captureGroup));
    mainLayout->addWidget(setUpSingleCaptureThermalControlsGroup(captureGroup));
    mainLayout->addWidget(setUpIntervalCaptureGroup(captureGroup)); // <-- ADD HERE
    mainLayout->addWidget(setUpSessionGroup(captureGroup));
    QHBoxLayout *btnLayout = new QHBoxLayout();
    singleCaptureButton = new QPushButton("📷 Capture");
    singleCaptureButton->setMinimumHeight(50);
    connect(singleCaptureButton, &QPushButton::clicked, this, &MainWindow::onCaptureSingle);
    btnLayout->addWidget(singleCaptureButton);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    return captureGroup;
}

ExpandableGroupBox *MainWindow::setUpImuGroup(QWidget *parent)
{
    ExpandableGroupBox *group = new ExpandableGroupBox("🧭 LSM6DS3TR-C IMU", parent);
    group->setExpanded(false);

    QVBoxLayout *mainLayout = new QVBoxLayout(group->getContentWidget());

    // -- Control row --------------------------------------------------------
    QHBoxLayout *ctrlLayout = new QHBoxLayout();

    imuInitButton = new QPushButton("Initialize");
    connect(imuInitButton, &QPushButton::clicked,
            this, &MainWindow::onImuInit);
    ctrlLayout->addWidget(imuInitButton);

    imuStartButton = new QPushButton("▶ Start Stream");
    imuStartButton->setEnabled(false);
    connect(imuStartButton, &QPushButton::clicked,
            this, &MainWindow::onImuStartStreaming);
    ctrlLayout->addWidget(imuStartButton);

    imuStopButton = new QPushButton("⏹ Stop Stream");
    imuStopButton->setEnabled(false);
    connect(imuStopButton, &QPushButton::clicked,
            this, &MainWindow::onImuStopStreaming);
    ctrlLayout->addWidget(imuStopButton);

    ctrlLayout->addStretch();
    imuStatusLabel = new QLabel("Not initialized");
    ctrlLayout->addWidget(imuStatusLabel);
    mainLayout->addLayout(ctrlLayout);

    // -- Numeric readout ----------------------------------------------------
    QFrame *readoutFrame = new QFrame();
    readoutFrame->setFrameStyle(QFrame::Box | QFrame::Sunken);
    QFormLayout *readoutLayout = new QFormLayout(readoutFrame);

    imuAccelLabel = new QLabel("---,  ---,  ---  g");
    imuGyroLabel  = new QLabel("---,  ---,  ---  dps");
    imuRateLabel  = new QLabel("0 samples/batch");
    QFont mono;
    mono.setFamily("monospace");
    mono.setPointSize(11);
    imuAccelLabel->setFont(mono);
    imuGyroLabel->setFont(mono);
    imuRateLabel->setStyleSheet("color: #666;");

    readoutLayout->addRow("Accel:", imuAccelLabel);
    readoutLayout->addRow("Gyro:",  imuGyroLabel);
    readoutLayout->addRow("Rate:",  imuRateLabel);
    mainLayout->addWidget(readoutFrame);

    // -- Event log ----------------------------------------------------------
    QGroupBox *eventLogGroup = new QGroupBox("Event log");
    QVBoxLayout *logLayout = new QVBoxLayout(eventLogGroup);
    imuEventLog = new QPlainTextEdit();
    imuEventLog->setReadOnly(true);
    imuEventLog->setMaximumBlockCount(500);
    imuEventLog->setMaximumHeight(120);
    imuEventLog->setStyleSheet("font-family: monospace; font-size: 11px;");
    logLayout->addWidget(imuEventLog);
    mainLayout->addWidget(eventLogGroup);

    // -- Configure button ---------------------------------------------------
    // The dialog has many settings (ODR, FS, FIFO, tap, free-fall, wake,
    // routing, frame). Keeping them out of the inline group keeps the
    // main panel scannable. Mirrors the "open dialog for advanced
    // settings" pattern used by Settings / Calibration.
    QHBoxLayout *configRow = new QHBoxLayout();
    configRow->addStretch();
    imuConfigureButton = new QPushButton("⚙ Configure...");
    imuConfigureButton->setEnabled(false);
    connect(imuConfigureButton, &QPushButton::clicked,
            this, &MainWindow::onImuConfigureClicked);
    configRow->addWidget(imuConfigureButton);
    mainLayout->addLayout(configRow);

    return group;
}


// ============================================================================
// DISTANCE SENSOR SETTINGS GROUP
// ============================================================================
ExpandableGroupBox *MainWindow::setUpDistanceSensorSettingsGroup(QWidget *parent)
{
    ExpandableGroupBox *group = new ExpandableGroupBox("Sensor Settings", parent);
    group->setExpanded(false);
    QVBoxLayout *mainLayout = new QVBoxLayout(group->getContentWidget());

    // Timing
    ExpandableGroupBox *timingGroup = new ExpandableGroupBox("Timing", group);
    QFormLayout *timingLayout = new QFormLayout(timingGroup->getContentWidget());
    depthTimingBudgetSpinBox = new QSpinBox();
    depthTimingBudgetSpinBox->setRange(10, 200);
    depthTimingBudgetSpinBox->setValue(50);
    depthTimingBudgetSpinBox->setSuffix(" ms");
    timingLayout->addRow("Timing Budget:", depthTimingBudgetSpinBox);
    depthInterMeasurementPeriodSpinBox = new QSpinBox();
    depthInterMeasurementPeriodSpinBox->setRange(10, 500);
    depthInterMeasurementPeriodSpinBox->setValue(100);
    depthInterMeasurementPeriodSpinBox->setSuffix(" ms");
    timingLayout->addRow("Inter-Measurement Period:", depthInterMeasurementPeriodSpinBox);
    mainLayout->addWidget(timingGroup);

    // Detection Thresholds
    ExpandableGroupBox *threshGroup = new ExpandableGroupBox("Detection Thresholds", group);
    QFormLayout *threshLayout = new QFormLayout(threshGroup->getContentWidget());
    depthSigmaThresholdSpinBox = new QSpinBox();
    depthSigmaThresholdSpinBox->setRange(1, 50);
    depthSigmaThresholdSpinBox->setValue(5);
    depthSigmaThresholdSpinBox->setSuffix(" mm");
    threshLayout->addRow("Sigma Threshold:", depthSigmaThresholdSpinBox);
    depthSignalThresholdSpinBox = new QSpinBox();
    depthSignalThresholdSpinBox->setRange(0, 65535);
    depthSignalThresholdSpinBox->setValue(4096);
    depthSignalThresholdSpinBox->setSuffix(" kcps");
    threshLayout->addRow("Signal Threshold:", depthSignalThresholdSpinBox);
    mainLayout->addWidget(threshGroup);

    // Connect signals
    connect(depthTimingBudgetSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v)
            { onCameraParamChanged(CameraParam::DEPTH_TIMING_BUDGET, v); });
    connect(depthInterMeasurementPeriodSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v)
            { onCameraParamChanged(CameraParam::DEPTH_INTER_MEASUREMENT, v); });
    connect(depthSigmaThresholdSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v)
            { onCameraParamChanged(CameraParam::DEPTH_SIGMA_THRESHOLD, v); });
    connect(depthSignalThresholdSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v)
            { onCameraParamChanged(CameraParam::DEPTH_SIGNAL_THRESHOLD, v); });

    return group;
}

ExpandableGroupBox *MainWindow::setupSensorTimingGroup()
{
    sensorTimingGroupBox = new ExpandableGroupBox("⏱ Sensor Timing", this);
    sensorTimingGroupBox->setExpanded(false);

    QVBoxLayout *mainLayout = new QVBoxLayout(sensorTimingGroupBox->getContentWidget());

    // RGB Camera timing
    QGroupBox *rgbGroup = new QGroupBox("IMX708 Camera");
    QVBoxLayout *rgbLayout = new QVBoxLayout(rgbGroup);

    imx708TimingLabel = new QLabel("Not available");
    imx708TimingLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    rgbLayout->addWidget(imx708TimingLabel);

    imx708TimingDetailLabel = new QLabel("");
    imx708TimingDetailLabel->setStyleSheet("color: #666; font-family: monospace;");
    imx708TimingDetailLabel->setWordWrap(true);
    rgbLayout->addWidget(imx708TimingDetailLabel);

    // --- temperature row ---
    QHBoxLayout *tempRow = new QHBoxLayout();
    tempRow->addWidget(new QLabel("SoC Temperature:"));
    imx708TempLabel = new QLabel("—");
    imx708TempLabel->setStyleSheet("font-weight: bold;");
    tempRow->addWidget(imx708TempLabel);
    imx708TempAgeLabel = new QLabel("");
    imx708TempAgeLabel->setStyleSheet("color: #888; font-size: 11px;");
    tempRow->addWidget(imx708TempAgeLabel);
    tempRow->addStretch();
    rgbLayout->addLayout(tempRow);
    // --- end temperature row ---

    mainLayout->addWidget(rgbGroup);

    // Arducam timing
    QGroupBox *arducamGroup = new QGroupBox("IMX219 Camera (Arducam)");
    QVBoxLayout *arducamLayout = new QVBoxLayout(arducamGroup);

    imx219TimingLabel = new QLabel("Not available");
    imx219TimingLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    arducamLayout->addWidget(imx219TimingLabel);

    imx219TimingDetailLabel = new QLabel("");
    imx219TimingDetailLabel->setStyleSheet("color: #666; font-family: monospace;");
    imx219TimingDetailLabel->setWordWrap(true);
    arducamLayout->addWidget(imx219TimingDetailLabel);

    mainLayout->addWidget(arducamGroup);

    // Refresh button
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *refreshButton = new QPushButton("🔄 Refresh Timing");
    connect(refreshButton, &QPushButton::clicked, this, [this]()
            {
        QJsonObject cmd;
        cmd[Param::COMMAND] = Command::GET_STATUS;
        sendCommand(cmd);
        addLog("Requesting sensor timing...", "info"); 
        requestCameraTemperature(); });
    buttonLayout->addWidget(refreshButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    cameraTemperatureTimer = new QTimer(this);
    cameraTemperatureTimer->setInterval(30000);
    connect(cameraTemperatureTimer, &QTimer::timeout,
            this, &MainWindow::requestCameraTemperature);

    return sensorTimingGroupBox;
}

// ============================================================================
// SETUP UI
// ============================================================================
void MainWindow::setupUI()
{
    setWindowTitle("Sanuwave Medical Imaging Client");
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);

    mainLayout->addWidget(setupConnectionGroup());

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget *scrollContent = new QWidget();
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->setSpacing(5);
    scrollLayout->setAlignment(Qt::AlignTop);

    scrollLayout->addWidget(setupSensorTimingGroup());
    scrollLayout->addWidget(setupStreamingGroup());
    scrollLayout->addWidget(setUpSingleCaptureGroup());
    scrollLayout->addWidget(setupLedControlGroup());

    // Distance Sensor
    ExpandableGroupBox *distanceGroup = new ExpandableGroupBox("📏 VL53L4CD Distance Sensor", this);
    distanceGroup->setExpanded(false);
    QVBoxLayout *distLayout = new QVBoxLayout(distanceGroup->getContentWidget());
    QHBoxLayout *distCtrlLayout = new QHBoxLayout();

    distanceStartButton = new QPushButton("▶ Start Stream");
    distanceStartButton->setEnabled(false);
    connect(distanceStartButton, &QPushButton::clicked, this, &MainWindow::onDistanceStartStreaming);
    distCtrlLayout->addWidget(distanceStartButton);
    distanceStopButton = new QPushButton("⏹ Stop Stream");
    distanceStopButton->setEnabled(false);
    connect(distanceStopButton, &QPushButton::clicked, this, &MainWindow::onDistanceStopStreaming);
    distCtrlLayout->addWidget(distanceStopButton);
    distCtrlLayout->addStretch();
    distanceStatusLabel = new QLabel("Not initialized");
    distCtrlLayout->addWidget(distanceStatusLabel);
    distLayout->addLayout(distCtrlLayout);
    QFrame *distFrame = new QFrame();
    distFrame->setFrameStyle(QFrame::Box | QFrame::Sunken);
    QVBoxLayout *distDisplayLayout = new QVBoxLayout(distFrame);
    distanceDisplayLabel = new QLabel("--- mm");
    distanceDisplayLabel->setAlignment(Qt::AlignCenter);
    QFont distFont;
    distFont.setPointSize(48);
    distFont.setBold(true);
    distanceDisplayLabel->setFont(distFont);
    distDisplayLayout->addWidget(distanceDisplayLabel);
    distanceSignalLabel = new QLabel("Signal: ---");
    distanceSignalLabel->setAlignment(Qt::AlignCenter);
    distDisplayLayout->addWidget(distanceSignalLabel);
    distLayout->addWidget(distFrame);
    distLayout->addWidget(setUpDistanceSensorSettingsGroup(distanceGroup));
    scrollLayout->addWidget(distanceGroup);

    // UV Sensor
    ExpandableGroupBox *uvGroup = new ExpandableGroupBox("☀️ AS7331 UV Sensor", this);
    uvGroup->setExpanded(false);
    QVBoxLayout *uvLayout = new QVBoxLayout(uvGroup->getContentWidget());
    QHBoxLayout *uvCtrlLayout = new QHBoxLayout();
    uvInitButton = new QPushButton("Initialize");
    connect(uvInitButton, &QPushButton::clicked, this, &MainWindow::onUVInit);
    uvCtrlLayout->addWidget(uvInitButton);
    uvReadButton = new QPushButton("📊 Single Read");
    uvReadButton->setEnabled(false);
    connect(uvReadButton, &QPushButton::clicked, this, &MainWindow::onUVRead);
    uvCtrlLayout->addWidget(uvReadButton);
    uvShutdownButton = new QPushButton("Shutdown");
    uvShutdownButton->setEnabled(false);
    connect(uvShutdownButton, &QPushButton::clicked, this, &MainWindow::onUVShutdown);
    uvCtrlLayout->addWidget(uvShutdownButton);
    uvCtrlLayout->addStretch();
    uvStatusLabel = new QLabel("Not initialized");
    uvCtrlLayout->addWidget(uvStatusLabel);
    uvLayout->addLayout(uvCtrlLayout);

    QGroupBox *uvConfig = new QGroupBox("Configuration");
    QGridLayout *uvConfigLayout = new QGridLayout(uvConfig);
    uvGainCombo = new QComboBox();
    uvGainCombo->addItem("64x (Default)", "64");
    uvGainCombo->setEnabled(false);
    connect(uvGainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onUVGainChanged);
    uvConfigLayout->addWidget(new QLabel("Gain:"), 0, 0);
    uvConfigLayout->addWidget(uvGainCombo, 0, 1);
    uvIntegrationTimeCombo = new QComboBox();
    uvIntegrationTimeCombo->addItem("64 ms (Default)", "64ms");
    uvIntegrationTimeCombo->setEnabled(false);
    connect(uvIntegrationTimeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onUVIntegrationTimeChanged);
    uvConfigLayout->addWidget(new QLabel("Integration:"), 0, 2);
    uvConfigLayout->addWidget(uvIntegrationTimeCombo, 0, 3);
    uvModeCombo = new QComboBox();
    uvModeCombo->addItem("Command", "cmd");
    uvModeCombo->setEnabled(false);
    connect(uvModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onUVModeChanged);
    uvConfigLayout->addWidget(new QLabel("Mode:"), 1, 0);
    uvConfigLayout->addWidget(uvModeCombo, 1, 1);
    uvLayout->addWidget(uvConfig);

    QFrame *uvFrame = new QFrame();
    uvFrame->setFrameStyle(QFrame::Box | QFrame::Sunken);
    QGridLayout *uvDisplayLayout = new QGridLayout(uvFrame);
    QFont uvFont;
    uvFont.setPointSize(24);
    uvFont.setBold(true);
    uvADisplayLabel = new QLabel("--- µW/cm²");
    uvADisplayLabel->setFont(uvFont);
    uvADisplayLabel->setAlignment(Qt::AlignCenter);
    uvDisplayLayout->addWidget(uvADisplayLabel, 0, 0);
    uvBDisplayLabel = new QLabel("--- µW/cm²");
    uvBDisplayLabel->setFont(uvFont);
    uvBDisplayLabel->setAlignment(Qt::AlignCenter);
    uvDisplayLayout->addWidget(uvBDisplayLabel, 0, 1);
    uvCDisplayLabel = new QLabel("--- µW/cm²");
    uvCDisplayLabel->setFont(uvFont);
    uvCDisplayLabel->setAlignment(Qt::AlignCenter);
    uvDisplayLayout->addWidget(uvCDisplayLabel, 0, 2);
    uvTempDisplayLabel = new QLabel("--- °C");
    uvTempDisplayLabel->setFont(uvFont);
    uvTempDisplayLabel->setAlignment(Qt::AlignCenter);
    uvDisplayLayout->addWidget(uvTempDisplayLabel, 0, 3);
    uvTimestampLabel = new QLabel("Last update: --");
    uvTimestampLabel->setAlignment(Qt::AlignCenter);
    uvDisplayLayout->addWidget(uvTimestampLabel, 1, 0, 1, 4);
    uvLayout->addWidget(uvFrame);
    scrollLayout->addWidget(uvGroup);

    scrollLayout->addWidget(setUpImuGroup(scrollContent));


    ExpandableGroupBox *alsGroup = new ExpandableGroupBox("💡 VD6283 Ambient Light Sensor", this);
    alsGroup->setExpanded(false);
    QVBoxLayout *alsLayout = new QVBoxLayout(alsGroup->getContentWidget());

    QHBoxLayout *alsCtrlLayout = new QHBoxLayout();
    alsInitButton = new QPushButton("Initialize");
    connect(alsInitButton, &QPushButton::clicked, this, &MainWindow::onALSInit);
    alsCtrlLayout->addWidget(alsInitButton);
    alsReadButton = new QPushButton("📊 Single Read");
    alsReadButton->setEnabled(false);
    connect(alsReadButton, &QPushButton::clicked, this, &MainWindow::onALSRead);
    alsCtrlLayout->addWidget(alsReadButton);
    alsShutdownButton = new QPushButton("Shutdown");
    alsShutdownButton->setEnabled(false);
    connect(alsShutdownButton, &QPushButton::clicked, this, &MainWindow::onALSShutdown);
    alsCtrlLayout->addWidget(alsShutdownButton);
    alsCtrlLayout->addStretch();
    alsStatusLabel = new QLabel("Not initialized");
    alsCtrlLayout->addWidget(alsStatusLabel);
    alsLayout->addLayout(alsCtrlLayout);

    QGroupBox *alsConfig = new QGroupBox("Configuration");
    QGridLayout *alsConfigLayout = new QGridLayout(alsConfig);
    alsConfigLayout->addWidget(new QLabel("Gain:"), 0, 0);
    alsGainCombo = new QComboBox();
    // Items carry the VD6283TX::Gain enum code (0x01–0x0F) as userData.
    // Listed low gain → high gain for user clarity; the enum codes themselves
    // run inverse (lower code = higher gain). See VD6283TX::Gain.
    alsGainCombo->addItem("0.71x", sanuwave::protocol::Param::AlsGain::X0_71);
    alsGainCombo->addItem("0.83x", sanuwave::protocol::Param::AlsGain::X0_83);
    alsGainCombo->addItem("1x",    sanuwave::protocol::Param::AlsGain::X1);
    alsGainCombo->addItem("1.25x", sanuwave::protocol::Param::AlsGain::X1_25);
    alsGainCombo->addItem("1.67x", sanuwave::protocol::Param::AlsGain::X1_67);
    alsGainCombo->addItem("2.5x",  sanuwave::protocol::Param::AlsGain::X2_5);
    alsGainCombo->addItem("3.33x", sanuwave::protocol::Param::AlsGain::X3_33);
    alsGainCombo->addItem("5x",    sanuwave::protocol::Param::AlsGain::X5);
    alsGainCombo->addItem("7.1x",  sanuwave::protocol::Param::AlsGain::X7_1);
    alsGainCombo->addItem("10x",   sanuwave::protocol::Param::AlsGain::X10);
    alsGainCombo->addItem("16x",   sanuwave::protocol::Param::AlsGain::X16);
    alsGainCombo->addItem("25x",   sanuwave::protocol::Param::AlsGain::X25);
    alsGainCombo->addItem("33x",   sanuwave::protocol::Param::AlsGain::X33);
    alsGainCombo->addItem("50x",   sanuwave::protocol::Param::AlsGain::X50);
    alsGainCombo->addItem("66.6x", sanuwave::protocol::Param::AlsGain::X66_6);
    alsGainCombo->setCurrentIndex(2);  // "1x"    // Default to 1x (third entry).
    alsGainCombo->setEnabled(false);
    connect(alsGainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onALSGainChanged);
    alsConfigLayout->addWidget(alsGainCombo, 0, 1);
    alsConfigLayout->addWidget(new QLabel("Exposure:"), 0, 2);
    alsExposureSpinBox = new QSpinBox();
    alsExposureSpinBox->setRange(1, 1600);
    alsExposureSpinBox->setValue(100);
    alsExposureSpinBox->setSuffix(" ms");
    alsExposureSpinBox->setEnabled(false);
    connect(alsExposureSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onALSExposureChanged);
    alsConfigLayout->addWidget(alsExposureSpinBox, 0, 3);
    alsLayout->addWidget(alsConfig);

    QFrame *alsFrame = new QFrame();
    alsFrame->setFrameStyle(QFrame::Box | QFrame::Sunken);
    QGridLayout *alsDisplayLayout = new QGridLayout(alsFrame);
    QFont alsFont;
    alsFont.setPointSize(16);
    alsFont.setBold(true);
    alsDisplayLayout->addWidget(new QLabel("Red:"), 0, 0);
    alsRedLabel = new QLabel("---");
    alsRedLabel->setFont(alsFont);
    alsRedLabel->setStyleSheet("color: #e74c3c;");
    alsDisplayLayout->addWidget(alsRedLabel, 0, 1);
    alsDisplayLayout->addWidget(new QLabel("Green:"), 0, 2);
    alsGreenLabel = new QLabel("---");
    alsGreenLabel->setFont(alsFont);
    alsGreenLabel->setStyleSheet("color: #27ae60;");
    alsDisplayLayout->addWidget(alsGreenLabel, 0, 3);
    alsDisplayLayout->addWidget(new QLabel("Blue:"), 0, 4);
    alsBlueLabel = new QLabel("---");
    alsBlueLabel->setFont(alsFont);
    alsBlueLabel->setStyleSheet("color: #3498db;");
    alsDisplayLayout->addWidget(alsBlueLabel, 0, 5);
    alsDisplayLayout->addWidget(new QLabel("Clear:"), 1, 0);
    alsClearLabel = new QLabel("---");
    alsClearLabel->setFont(alsFont);
    alsDisplayLayout->addWidget(alsClearLabel, 1, 1);
    alsDisplayLayout->addWidget(new QLabel("IR:"), 1, 2);
    alsIRLabel = new QLabel("---");
    alsIRLabel->setFont(alsFont);
    alsDisplayLayout->addWidget(alsIRLabel, 1, 3);
    alsDisplayLayout->addWidget(new QLabel("Visible:"), 1, 4);
    alsVisibleLabel = new QLabel("---");
    alsVisibleLabel->setFont(alsFont);
    alsDisplayLayout->addWidget(alsVisibleLabel, 1, 5);
    QFont largeFont;
    largeFont.setPointSize(24);
    largeFont.setBold(true);
    alsDisplayLayout->addWidget(new QLabel("Lux:"), 2, 0);
    alsLuxLabel = new QLabel("--- lx");
    alsLuxLabel->setFont(largeFont);
    alsDisplayLayout->addWidget(alsLuxLabel, 2, 1, 1, 2);
    alsDisplayLayout->addWidget(new QLabel("CCT:"), 2, 3);
    alsCCTLabel = new QLabel("--- K");
    alsCCTLabel->setFont(largeFont);
    alsDisplayLayout->addWidget(alsCCTLabel, 2, 4, 1, 2);
    alsTimestampLabel = new QLabel("Last update: --");
    alsTimestampLabel->setAlignment(Qt::AlignCenter);
    alsDisplayLayout->addWidget(alsTimestampLabel, 3, 0, 1, 6);
    alsLayout->addWidget(alsFrame);
    scrollLayout->addWidget(alsGroup);


    // ── UVBF VBlank Timing ────────────────────────────────────────────────────
    ExpandableGroupBox *uvbfGroup = new ExpandableGroupBox("⏱ UV Fluorescence VBlank Timing", this);
    uvbfGroup->setExpanded(true);
        QHBoxLayout *uvbfLayout = new QHBoxLayout(uvbfGroup->getContentWidget());

    uvbfVBlankButton = new QPushButton("⏱ VBlank Timing…");
    uvbfVBlankButton->setMinimumHeight(50);
    uvbfVBlankButton->setEnabled(false);    // enabled in updateConnectionStatus()
    uvbfVBlankButton->setToolTip(
        "Captures a 7-frame VBlank timing experiment.\n"
        "Evaluates whether requestCompleted fires inside the VBlank\n"
        "interval, which would allow LED toggling without a prime frame.");
    connect(uvbfVBlankButton, &QPushButton::clicked,
            this, &MainWindow::onUVBFVBlankTriggered);
    uvbfLayout->addWidget(uvbfVBlankButton);
 
    uvbfLayout->addStretch();
    scrollLayout->addWidget(uvbfGroup);
    // ── end UVBF ──────────────────────────────────────────────────────────────

    // Image Viewer
    ExpandableGroupBox *viewerGroup = new ExpandableGroupBox("Image Display", this);
    QHBoxLayout *viewerLayout = new QHBoxLayout(viewerGroup->getContentWidget());
    showImageViewerButton = new QPushButton("🖺 Open Image Viewer Window");
    showImageViewerButton->setMinimumHeight(50);
    connect(showImageViewerButton, &QPushButton::clicked, this, &MainWindow::onShowImageViewer);
    viewerLayout->addWidget(showImageViewerButton);
    viewerLayout->addStretch();
    scrollLayout->addWidget(viewerGroup);

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);

    logDock = new QDockWidget("System Logs", this);
    logDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    logTextEdit = new QTextEdit();
    logTextEdit->setReadOnly(true);
    logTextEdit->setMinimumHeight(150);
    logDock->setWidget(logTextEdit);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);

    ledGpioStatusLabel = new QLabel("LED: Off");
    ledGpioStatusLabel->setStyleSheet("QLabel { color: gray; }");
    statusBar()->addPermanentWidget(ledGpioStatusLabel);

    registerCameraWidgets();
}

void MainWindow::createMenuBar()
{
    QMenuBar *menuBar = new QMenuBar(this);
    setMenuBar(menuBar);
    QMenu *fileMenu = menuBar->addMenu("&File");
    QAction *settingsAction = fileMenu->addAction("&Settings...");
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);
    QAction *saveSettingsAction = fileMenu->addAction("Save All Camera Settings...");
    connect(saveSettingsAction, &QAction::triggered, this, &MainWindow::onSaveAllSettings);
    QAction *loadSettingsAction = fileMenu->addAction("Load All Camera Settings...");
    connect(loadSettingsAction, &QAction::triggered, this, &MainWindow::onLoadAllSettings);
    fileMenu->addSeparator();
    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
    QMenu *viewMenu = menuBar->addMenu("&View");
    QAction *showLogsAction = viewMenu->addAction("System &Logs");
    showLogsAction->setCheckable(true);
    showLogsAction->setChecked(true);
    viewMenu->addSeparator();
    QAction *showCalibrationAction = viewMenu->addAction("Show RPi &Calibration Data...");
    connect(showCalibrationAction, &QAction::triggered,
            this, &MainWindow::onShowCalibrationViewer);
    connect(showLogsAction, &QAction::toggled, logDock, &QDockWidget::setVisible);
    QAction *showRawDiagAction = viewMenu->addAction("Raw Image &Diagnostic...");
    connect(showRawDiagAction, &QAction::triggered,
            this, &MainWindow::onOpenRawDiagnostic);
    lensCalibAction = new QAction(tr("Lens Calibration…"), this);
    // Add to whatever menu makes sense, e.g. toolsMenu:
    viewMenu->addAction(lensCalibAction);
    connect(lensCalibAction, &QAction::triggered,
            this, &MainWindow::onOpenLensCalibration);

    QMenu *helpMenu = menuBar->addMenu("&Help");
    QAction *aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, [this]()
            {
        AboutDialog dialog(this);
        dialog.exec(); });
}

void MainWindow::loadSettings()
{
    QString ip = settings->value("server/ip", "192.168.1.100").toString();
    int port = settings->value("server/port", 8080).toInt();
    serverInfoLabel->setText(QString("Server: %1:%2").arg(ip).arg(port));
    cameraRotation[Camera::IMX708] = settings->value("rotation/imx708", false).toBool();
    cameraRotation[Camera::IMX219] = settings->value("rotation/imx219", false).toBool();
    cameraRotation[Camera::THERMAL] = settings->value("rotation/thermal", false).toBool();

   sessionOutputFolder = settings->value("session/output_folder", "").toString();
    if (!sessionOutputFolder.isEmpty())
    {
        if (QDir(sessionOutputFolder).exists())
        {
            sessionManager->setOutputFolder(sessionOutputFolder);
            QString display = sessionOutputFolder.length() > 50
                                  ? "…" + sessionOutputFolder.right(47)
                                  : sessionOutputFolder;
            sessionFolderLabel->setText(display);
            sessionFolderLabel->setStyleSheet("color: #27ae60; font-style: normal;");
            sessionFolderLabel->setToolTip(sessionOutputFolder);
        }
        else
        {
            LOG_WARNING << "Session output folder no longer exists, clearing: "
                        << sessionOutputFolder.toStdString() << std::endl;
            sessionOutputFolder.clear();
            sessionFolderLabel->setText("No folder selected (previous folder missing)");
            sessionFolderLabel->setStyleSheet("color: #e67e22; font-style: italic;");
            sessionFolderLabel->setToolTip("");
        }
    }

     ledExternalControlCheckBox->setChecked(settings->value("led/external_control", false).toBool());
     applyLedExternalControlState(ledExternalControlCheckBox->isChecked());

}

void MainWindow::onOpenSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
    {
        loadSettings();
        addLog("Settings updated", "success");
    }
}

void MainWindow::onOpenRawDiagnostic()
{
    if (!serverConnection->isConnected())
    {
        QMessageBox::warning(this, "Not Connected",
                             "Connect to the server before opening diagnostics.");
        return;
    }

    if (streamingActive)
    {
        QMessageBox::warning(this, "Diagnostic Unavailable",
                             "Stop camera streaming before opening diagnostics.");
        return;
    }
    RawDiagnosticWindow dlg(*serverConnection, this);
    dlg.exec();
}

void MainWindow::onOpenLensCalibration()
{
    // Collect all unmet prerequisites and report them together
    QStringList missing;

    if (!serverConnection || !serverConnection->isConnected())
        missing << tr("• Connect to the camera server");

    if (!streamingActive)
    {
        missing << tr("• Start an IMX708 or IMX219 camera stream");
    }
    else
    {
        // Stream is active — check it's a supported camera
        QString cam = streamCameraSelector->currentData().toString();
        if (cam != QString(sanuwave::protocol::Camera::IMX708) &&
            cam != QString(sanuwave::protocol::Camera::IMX219))
        {
            missing << tr("• Switch to an IMX708 or IMX219 stream "
                          "(lens position is not supported for %1)")
                           .arg(cam);
        }
    }

    if (!distanceInitialized)
        missing << tr("• Initialize the ToF distance sensor");
    else if (!distanceStreaming)
        missing << tr("• Start ToF distance ranging");

    if (!missing.isEmpty())
    {
        QMessageBox::information(this, tr("Lens Calibration — Setup Required"),
                                 tr("Before opening Lens Calibration, please:\n\n") + missing.join("\n"));
        return;
    }

    // All prerequisites met — determine active camera
    QString cam = streamCameraSelector->currentData().toString();

    // Create dialog if not already open
    if (!lensCalibDialog)
    {
        lensCalibDialog = new LensCalibrationDialog(serverConnection, cam, this);

        // Forward lens position changes to server
        connect(lensCalibDialog, &LensCalibrationDialog::lensPositionChangeRequested,
                this, [this](float position, const QString &camera)
                {
                    using namespace sanuwave::protocol;
                    QJsonObject cmd;
                    cmd[Param::COMMAND]      = Command::SET_LENS_POSITION;
                    cmd[Param::CAMERA]       = camera;
                    cmd[Param::LENS_POSITION] = static_cast<double>(position);
                    sendCommand(cmd); });

        // Clean up pointer when dialog is closed
        connect(lensCalibDialog, &QDialog::finished,
                this, [this]()
                { lensCalibDialog = nullptr; });
    }

    lensCalibDialog->show();
    lensCalibDialog->raise();
    lensCalibDialog->activateWindow();
}

void MainWindow::onShowCalibrationViewer()
{
    if (!calibrationViewerDialog)
    {
        calibrationViewerDialog = new CalibrationViewerDialog(this);
    }

    calibrationViewerDialog->refreshData();
    calibrationViewerDialog->show();
    calibrationViewerDialog->raise();
    calibrationViewerDialog->activateWindow();
}

// ============================================================================
// SESSION SLOTS
// ============================================================================

void MainWindow::onSelectSessionOutputFolder()
{
    QFileDialog dialog(this, "Select Session Output Folder");
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setDirectory(sessionOutputFolder.isEmpty()
                            ? QDir::homePath()
                            : sessionOutputFolder);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString folder = dialog.selectedFiles().first();
    if (folder.isEmpty())
        return;

    sessionOutputFolder = folder;
    sessionManager->setOutputFolder(folder);

    QString display = folder.length() > 50
                          ? "…" + folder.right(47)
                          : folder;
    sessionFolderLabel->setText(display);
    sessionFolderLabel->setStyleSheet("color: #27ae60; font-style: normal;");
    sessionFolderLabel->setToolTip(folder);

    addLog("Session output folder: " + folder, "success");
}

void MainWindow::onSessionStarted(const QString &sessionId, const QString &sessionDir)
{
    sessionIdLabel->setText(sessionId);
    sessionFrameCountLabel->setText("0 frames");
    addLog(QString("Session started: %1").arg(sessionId), "success");
    addLog(QString("Session directory: %1").arg(sessionDir), "info");
}

void MainWindow::onFrameSaved(const QString &filePath, const QString & /*sidecarPath*/)
{
    // Update frame count label from the session manager's counters
    // by counting saved files — simplest approach without exposing internals
    QFileInfo fi(filePath);
    QDir dir(fi.absolutePath());
    int jpegCount = static_cast<int>(
        dir.entryList({"*.jpg"}, QDir::Files).count());
    sessionFrameCountLabel->setText(QString("%1 frame%2 saved")
                                        .arg(jpegCount)
                                        .arg(jpegCount == 1 ? "" : "s"));
}

// ============================================================================
// CONNECTION HANDLERS
// ============================================================================
void MainWindow::onConnectClicked()
{
    QString ip = settings->value("server/ip", "192.168.1.100").toString();
    quint16 port = settings->value("server/port", 8080).toInt();
    addLog(QString("Connecting to %1:%2...").arg(ip).arg(port), "info");
    serverConnection->connectToServer(ip, port);
}

void MainWindow::onDisconnectClicked()
{
    if (streamingActive)
        onStreamStop();

    if (distanceStreaming)
    {
        // Stop the local polling timer immediately so no further DISTANCE_READ
        // commands are queued after the DISTANCE_STOP.
        distanceStreamTimer->stop();
        distanceStreaming = false;

        // Send stop command, then give the socket one event-loop cycle to flush
        // before we close it.  A single-shot timer of 0 ms defers the disconnect
        // until the current call stack unwinds and the socket write buffer drains.
        QJsonObject cmd;
        cmd[Param::COMMAND] = Command::DISTANCE_STOP;
        serverConnection->sendCommand(cmd);

        QTimer::singleShot(50, this, [this]()
        {
            serverConnection->disconnectFromServer();
        });
        return;
    }

    serverConnection->disconnectFromServer();
}

void MainWindow::onServerConnected()
{
    addLog("✔ Connected to server", "success");
    updateConnectionStatus(true);

    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::GET_STATUS;
    sendCommand(cmd);

    // Start periodic temperature polling
    if (cameraTemperatureTimer)
        cameraTemperatureTimer->start();

    requestCameraTemperature();
}

void MainWindow::onServerDisconnected()
{
    addLog("✗ Disconnected from server", "error");
    updateConnectionStatus(false);

    // Close VBlank timing dialog if open
    if (vblankDialog)
        vblankDialog->reject();

    // Reset stream UI state
    streamingActive = false;
    isDualStreaming = false;
    streamStartButton->setEnabled(true);
    streamDualButton->setEnabled(true);
    streamStopButton->setEnabled(false);
    streamStatusLabel->setText("Idle");
    streamFpsLabel->setText("FPS: 0");
    streamFrameCount = 0;
    imx219Timing = SensorTimingInfo();
    imx708Timing = SensorTimingInfo();
    if (imx708TimingLabel)
        imx708TimingLabel->setText("Not available");
    if (imx708TimingDetailLabel)
        imx708TimingDetailLabel->setText("");
    if (imx219TimingLabel)
        imx219TimingLabel->setText("Not available");
    if (imx219TimingDetailLabel)
        imx219TimingDetailLabel->setText("");

    if (cameraTemperatureTimer)
        cameraTemperatureTimer->stop();
    if (imx708TempLabel)
        imx708TempLabel->setText("—");
    if (imx708TempAgeLabel)
        imx708TempAgeLabel->setText("");

    // Distance sensor (VL53L4CD)
    distanceInitialized = false;
    distanceStreaming = false;
    distanceStreamTimer->stop();
    distanceStartButton->setEnabled(false);
    distanceStopButton->setEnabled(false);
    distanceStatusLabel->setText("Uknown");
    distanceDisplayLabel->setText("--- mm");
    distanceSignalLabel->setText("Signal: ---");

     // IMU (LSM6DS3TR-C)
    imuInitialized = false;
    imuStreaming = false;
    if (imuStartButton)     
        imuStartButton->setEnabled(false);
    if (imuStopButton)      
        imuStopButton->setEnabled(false);
    if (imuConfigureButton) 
        imuConfigureButton->setEnabled(false);
    if (imuStatusLabel)     
        imuStatusLabel->setText("Not initialized");
    if (imuAccelLabel)      
        imuAccelLabel->setText("---,  ---,  ---  g");
    if (imuGyroLabel)       
        imuGyroLabel->setText("---,  ---,  ---  dps");
    if (imuRateLabel)       
        imuRateLabel->setText("0 samples/batch");
    if (imuEventLog) 
        imuEventLog->clear();

    // ALS (VD6283TX)
    alsInitialized = false;
    if (alsInitButton)
        alsInitButton->setEnabled(true);     // user may want to re-init after reconnect
    if (alsReadButton)
        alsReadButton->setEnabled(false);
    if (alsShutdownButton)
        alsShutdownButton->setEnabled(false);
    if (alsGainCombo)
        alsGainCombo->setEnabled(false);
    if (alsExposureSpinBox)
        alsExposureSpinBox->setEnabled(false);
    if (alsStatusLabel)
        alsStatusLabel->setText("Not initialized");
    if (alsRedLabel)       alsRedLabel->setText("---");
    if (alsGreenLabel)     alsGreenLabel->setText("---");
    if (alsBlueLabel)      alsBlueLabel->setText("---");
    if (alsClearLabel)     alsClearLabel->setText("---");
    if (alsIRLabel)        alsIRLabel->setText("---");
    if (alsVisibleLabel)   alsVisibleLabel->setText("---");
    if (alsLuxLabel)       alsLuxLabel->setText("--- lx");
    if (alsCCTLabel)       alsCCTLabel->setText("--- K");
    if (alsTimestampLabel) alsTimestampLabel->setText("Last update: --");

    // LED controls
    ledInitialized = false;
    ledAllOffButton->setEnabled(false);
    ledIdSpinBox->setEnabled(false);
    ledBrightnessSlider->setEnabled(false);
    ledTorchButton->setEnabled(false);
    ledFlashButton->setEnabled(false);
    ledFlashDurationSpinBox->setEnabled(false);
    ledOffButton->setEnabled(false);
    ledStatusLabel->setText("Not available");
    versionChecked = false;

    rgbDecoder->stop();
    thermalDecoder->stop();
}

void MainWindow::onServerError(const QString &message)
{
    addLog(QString("Socket error: %1").arg(message), "error");
}

void MainWindow::tryFinalizeFrame()
{
    if (!pendingCaptureInfo.has_value() || pendingImageData.isEmpty())
        return;

    LOG_INFO << "tryFinalizeFrame: finalizing session "
             << pendingCaptureInfo->sessionId.toStdString() << std::endl;

    sessionManager->finalizeFrame(*pendingCaptureInfo, pendingImageData);
    sessionManager->endSession();

    pendingCaptureInfo.reset();
    pendingImageData.clear();
    pendingImageModality.clear();
}



void MainWindow::snapshotCaptureContext(CaptureResultInfo& info) const
{
    // ToF snapshot
    info.tofValid       = lastDistanceValid;
    info.tofDistance_mm = lastDistanceMm;
    info.tofSignal_kcps = lastDistanceSignal;
    info.tofStatus      = lastDistanceStatus;

    // LED GPIO mode snapshot — one entry covering the whole GPIO trigger state
    // (per-LED LM3643 state comes from the server in the capture_complete response)
    QString gpioMode = ledGpioModeCombo->currentData().toString();
    if (gpioMode != "off") {
        // Check if there's already an entry from the server; if not, create a
        // synthetic one for the GPIO trigger settings.
        LedStateInfo gpio;
        gpio.ledId            = "gpio_trigger";
        gpio.active           = true;
        gpio.ledGpioMode      = gpioMode;
        gpio.preFrameDelay_ms = ledPreFrameDelaySpinBox->value();
        gpio.postCaptureOff   = ledPostCaptureOffCheckBox->isChecked();
        // drive_ma comes from per-LED LM3643 data in info.leds (server-supplied)
        info.leds.append(gpio);
    } else {
        // Still record that LED GPIO was off, so the sidecar is unambiguous
        LedStateInfo gpio;
        gpio.ledId       = "gpio_trigger";
        gpio.active      = false;
        gpio.ledGpioMode = "off";
        info.leds.append(gpio);
    }
}



void MainWindow::onCaptureComplete(const CaptureResultInfo &info)
{
    // Update session ID label if it changed (e.g. first capture)
    if (!info.sessionId.isEmpty() && sessionIdLabel)
        sessionIdLabel->setText(info.sessionId);


    CaptureResultInfo augmented = info;
    snapshotCaptureContext(augmented); 


    QString timingInfo;
    if (info.timing.valid)
    {
        timingInfo = QString(" | Line: %1µs, Rolling: %2µs")
                         .arg(info.timing.lineTime_us, 0, 'f', 1)
                         .arg(info.timing.rollingShutter_us, 0, 'f', 0);
    }
    QString ledInfo;
    if (!info.leds.isEmpty())
        ledInfo = QString(" | LEDs: %1 active").arg(info.leds.size());

    addLog(QString("Capture complete: %1 %2×%3, exp=%4µs, gain=%5%6%7")
               .arg(info.modality)
               .arg(info.width)
               .arg(info.height)
               .arg(info.actualExposure_us)
               .arg(info.actualGain, 0, 'f', 2)
               .arg(timingInfo)
               .arg(ledInfo),
           "success");

    pendingCaptureInfo = augmented;

    addLog(QString("RAW capture_complete leds count: %1").arg(info.leds.size()), "info");
    addLog(QString("GPIO mode combo: %1").arg(ledGpioModeCombo->currentData().toString()), "info");
    tryFinalizeFrame();
}

void MainWindow::onSensorTimingReceived(const QString &camera, const SensorTimingInfo &timing)
{
    if (camera == Camera::IMX708)
    {
        imx708Timing = timing;
        updateTimingDisplay(Camera::IMX708, timing);
    }
    else if (camera == Camera::IMX219)
    {
        imx219Timing = timing;
        updateTimingDisplay(Camera::IMX219, timing);
    }

    if (timing.valid)
    {
        addLog(QString("%1 timing updated: line=%2µs, rolling=%3ms")
                   .arg(camera.toUpper())
                   .arg(timing.lineTime_us, 0, 'f', 2)
                   .arg(timing.rollingShutter_us / 1000.0, 0, 'f', 1),
               "info");
    }
    updateFrameDurationTimingHints();
}

void MainWindow::onImageReceived(const QByteArray &data, const QString &modality)
{
    pendingImageData = data;
    pendingImageModality = modality;
    tryFinalizeFrame();

    displayImage(data, modality);
}

void MainWindow::onStreamFrameReceived(const QByteArray &data, const StreamFrameInfo &info)
{
    // Drive focus sweep from stream frames (timers don't fire reliably
    // when the event loop is saturated by stream frame processing)
    if (sweepActiveButton != nullptr)
    {
        sweepFrameCounter++;

        if (pendingSweepSave && sweepFrameCounter >= sweepSettleFrames)
        {
            // Save mode: save this frame, advance
            LOG_INFO << "Saving focus sweep image for lens position " << pendingSweepSaveLensPos << std::endl;
            pendingSweepSave = false;
            saveFocusSweepImage(data, pendingSweepSaveLensPos, sweepActiveCamera);
            focusSweepPos += focusSweepStep;
            sweepFrameCounter = 0;
            advanceFocusSweep();
        }
        else if (!sweepSaveImages && !pendingSweepSave && sweepFrameCounter >= sweepSettleFrames)
        {
            // No-save mode: just advance after settle frames
            focusSweepPos += focusSweepStep;
            sweepFrameCounter = 0;
            advanceFocusSweep();
        }
    }

    displayStreamFrame(data, info);

    // Motion indicator: only updated from rgb-stream frames. Thermal frames
    // (single thermal stream, and dual stream's thermal leg) carry
    // motion.valid==false and would push the indicator into Unknown on every
    // other dual-stream frame if we did not gate here.
    const QString& mod = info.modality;
    const bool isRgbStream =
        (mod == Camera::IMX708    ||   // "imx708"
         mod == Camera::IMX219    ||   // "imx219"
         mod == Modality::RGB);        // "rgb"   (dual-stream rgb leg)
    if (isRgbStream)
        updateMotionIndicator(info);
}

void MainWindow::onIntervalFrameReceived(const QByteArray &data, const StreamFrameInfo &info)
{
    displayIntervalFrame(data, info);
}

// ---------------------------------------------------------------------------
// Motion indicator
//
// Drives three surfaces from a single source of truth (StreamFrameInfo):
//   1. Numeric label in the FPS strip ("Motion: 0.42 px")
//   2. Overlay badge on the image viewer (STILL / MOVING / hidden)
//   3. Scrolling history chart inside the streaming panel
//
// Hysteresis between Still and Moving prevents the badge from flickering
// when motion is right at threshold. Caller (onStreamFrameReceived) gates
// to rgb-only streams so we do not get pulled into Unknown on every other
// dual-stream frame.
// ---------------------------------------------------------------------------
void MainWindow::updateMotionIndicator(const StreamFrameInfo& info)
{
    const StreamFrameInfo::Motion& m = info.motion;

    // Reset the decay timer on every measurement (valid or not) - the
    // stream is alive even if motion was filtered out by the confidence
    // floor.
    motionDecayTimer_->start(motionDecayMs_);

    if (!m.valid || m.confidence < motionConfFloor_)
    {
        motionLabel_->setText("Motion: -");
        if (motionUiState_ != MotionUiState::Unknown)
        {
            motionUiState_ = MotionUiState::Unknown;
            if (imageViewerWindow)
                imageViewerWindow->setMotionState(
                    ImageViewerWindow::MotionBadge::Unknown);
        }
        // No sample fed to the chart on filtered frames - a gap in the
        // trace correctly indicates "no measurement available".
        return;
    }

    motionLabel_->setText(QString("Motion: %1 px").arg(m.trans_px, 0, 'f', 2));

    // Feed the history chart with the server-side timestamp so the x axis
    // remains honest under network jitter.
    if (motionChart_)
        motionChart_->addSample(static_cast<qint64>(info.timestamp), m.trans_px);

    // Hysteresis: enter Moving at motionEnterMoving_, exit at
    // motionExitMoving_. Unknown promotes to Still or Moving depending on
    // the first valid reading.
    MotionUiState next = motionUiState_;
    if (motionUiState_ == MotionUiState::Moving)
    {
        if (m.trans_px <= motionExitMoving_)
            next = MotionUiState::Still;
    }
    else // Still or Unknown
    {
        if (m.trans_px >= motionEnterMoving_)
            next = MotionUiState::Moving;
        else
            next = MotionUiState::Still;
    }

    if (next != motionUiState_)
    {
        motionUiState_ = next;
        if (imageViewerWindow)
        {
            ImageViewerWindow::MotionBadge badge =
                (next == MotionUiState::Moving)
                    ? ImageViewerWindow::MotionBadge::Moving
                    : ImageViewerWindow::MotionBadge::Still;
            imageViewerWindow->setMotionState(badge);
        }
    }
}

void MainWindow::onMotionDecayTimeout()
{
    // No rgb-stream frame in motionDecayMs_ - clear the indicator so a
    // stale reading does not linger after stream stop.
    motionLabel_->setText("Motion: -");
    motionUiState_ = MotionUiState::Unknown;
    if (imageViewerWindow)
        imageViewerWindow->setMotionState(
            ImageViewerWindow::MotionBadge::Unknown);
    // The chart trims its own samples and will naturally drain to empty
    // over the next windowMs_; we do not clear() it explicitly so the
    // operator can still see the last 30 s of history after a stop.
}

// ---------------------------------------------------------------------------
// Motion settings dialog
//
// Lazily constructed on first click so the cost is only paid if the
// operator actually opens it. Non-modal, parented to MainWindow, kept
// alive across show/hide cycles. Updates to thresholds apply immediately
// to the in-memory state and the chart's reference lines; ROI/reference
// changes only persist (they are read by onStreamStart/Dual on the next
// command).
// ---------------------------------------------------------------------------
void MainWindow::onMotionSettingsClicked()
{
    if (!motionSettingsDialog_)
    {
        motionSettingsDialog_ = new MotionSettingsDialog(settings, this);

        connect(motionSettingsDialog_, &MotionSettingsDialog::enterMovingChanged,
                this, &MainWindow::onMotionEnterChanged);
        connect(motionSettingsDialog_, &MotionSettingsDialog::exitMovingChanged,
                this, &MainWindow::onMotionExitChanged);
        connect(motionSettingsDialog_, &MotionSettingsDialog::confFloorChanged,
                this, &MainWindow::onMotionConfChanged);
        connect(motionSettingsDialog_, &MotionSettingsDialog::decayMsChanged,
                this, &MainWindow::onMotionDecayChanged);
        // ROI and reference signals do not need MainWindow slots - the
        // dialog persists them via QSettings, and onStreamStart/Dual read
        // them directly on the next command build.
    }
    motionSettingsDialog_->show();
    motionSettingsDialog_->raise();
    motionSettingsDialog_->activateWindow();
}

void MainWindow::onMotionEnterChanged(double v)
{
    motionEnterMoving_ = v;
    if (motionChart_)
        motionChart_->setThresholds(motionEnterMoving_, motionExitMoving_);
}

void MainWindow::onMotionExitChanged(double v)
{
    motionExitMoving_ = v;
    if (motionChart_)
        motionChart_->setThresholds(motionEnterMoving_, motionExitMoving_);
}

void MainWindow::onMotionConfChanged(double v)
{
    motionConfFloor_ = v;
}

void MainWindow::onMotionDecayChanged(int v)
{
    motionDecayMs_ = v;
    // No need to restart the timer here - the next motion frame will
    // call motionDecayTimer_->start(motionDecayMs_) with the new value.
}

void MainWindow::onImuDataReceived(const QJsonObject &data)
{
    if (!data[ImuField::VALID].toBool(false)) return;
    const int count = data[ImuField::COUNT].toInt(0);
    if (count <= 0) return;

    const QJsonArray ax = data[ImuField::ACCEL_X].toArray();
    const QJsonArray ay = data[ImuField::ACCEL_Y].toArray();
    const QJsonArray az = data[ImuField::ACCEL_Z].toArray();
    const QJsonArray gx = data[ImuField::GYRO_X].toArray();
    const QJsonArray gy = data[ImuField::GYRO_Y].toArray();
    const QJsonArray gz = data[ImuField::GYRO_Z].toArray();
    const QJsonArray ts = data[ImuField::TIMESTAMPS_NS].toArray();

    imuAccelLsbToG  = data[ImuField::ACCEL_LSB_TO_G].toDouble(imuAccelLsbToG);
    imuGyroLsbToDps = data[ImuField::GYRO_LSB_TO_DPS].toDouble(imuGyroLsbToDps);

    if (ax.size() != count) return;  // malformed batch

    // Live readout — most recent sample.
    const int16_t lastAx = static_cast<int16_t>(ax.last().toInt());
    const int16_t lastAy = static_cast<int16_t>(ay.last().toInt());
    const int16_t lastAz = static_cast<int16_t>(az.last().toInt());
    const int16_t lastGx = static_cast<int16_t>(gx.last().toInt());
    const int16_t lastGy = static_cast<int16_t>(gy.last().toInt());
    const int16_t lastGz = static_cast<int16_t>(gz.last().toInt());

    imuAccelLabel->setText(QString::asprintf(
        "%+6.3f, %+6.3f, %+6.3f  g",
        lastAx * imuAccelLsbToG,
        lastAy * imuAccelLsbToG,
        lastAz * imuAccelLsbToG));
    imuGyroLabel->setText(QString::asprintf(
        "%+7.2f, %+7.2f, %+7.2f  dps",
        lastGx * imuGyroLsbToDps,
        lastGy * imuGyroLsbToDps,
        lastGz * imuGyroLsbToDps));


    // Rate label — span of this batch.
    if (!ts.isEmpty()) {
        const qint64 firstNs = static_cast<qint64>(ts.first().toDouble());
        const qint64 lastNs  = static_cast<qint64>(ts.last().toDouble());
        const double spanMs  = (lastNs - firstNs) / 1.0e6;
        imuRateLabel->setText(QString("%1 samples/batch · span %2 ms")
            .arg(count).arg(spanMs, 0, 'f', 1));
    }
}

void MainWindow::onImuEventReceived(const QJsonObject &evt)
{
    const QString kind = evt[ImuField::EVENT_KIND].toString();
    const qint64  tNs  = static_cast<qint64>(
        evt[ImuField::EVENT_TIMESTAMP].toDouble());

    QStringList axes;
    if (evt[ImuField::EVENT_AXIS_X].toBool()) axes << "X";
    if (evt[ImuField::EVENT_AXIS_Y].toBool()) axes << "Y";
    if (evt[ImuField::EVENT_AXIS_Z].toBool()) axes << "Z";

    const QString line = QString("[%1ms]  %2  %3%4")
        .arg(tNs / 1000000)
        .arg(kind)
        .arg(axes.join(""))
        .arg(evt[ImuField::EVENT_SIGN].toBool() ? " (+)" : " (-)");

    if (imuEventLog) imuEventLog->appendPlainText(line);
    addLog(QString("IMU event: %1 %2").arg(kind).arg(axes.join("")), "info");
}

void MainWindow::onDistanceDataReceived(const QJsonObject &data)
{

    // Snapshot for sidecar metadata
    if (data["valid"].toBool()) {
        lastDistanceMm     = data[sanuwave::protocol::DistanceField::DISTANCE_MM].toInt();
        lastDistanceSignal = data[sanuwave::protocol::DistanceField::SIGNAL_PER_SPAD].toInt();
        lastDistanceStatus = data[sanuwave::protocol::DistanceField::RANGE_STATUS].toInt();
        lastDistanceValid   = true;
    }

    displayDistanceData(data);
    if (lensCalibDialog)
    {
        float mm = static_cast<float>(data[sanuwave::protocol::DistanceField::DISTANCE_MM].toDouble());
        lensCalibDialog->onDistanceData(mm);
    }
}

void MainWindow::onUVDataReceived(const QJsonObject &data)
{
    displayUVData(data);
}

void MainWindow::onALSDataReceived(const QJsonObject &data)
{
    displayALSData(data);
}

void MainWindow::onLedTorch()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::LED_TORCH;
    cmd["led_id"] = QString::number(ledIdSpinBox->value());
    cmd["brightness"] = QString::number(ledBrightnessSlider->value());
    sendCommand(cmd);
    addLog(QString("LED %1 torch at brightness %2")
               .arg(ledIdSpinBox->value())
               .arg(ledBrightnessSlider->value()),
           "info");
}

void MainWindow::onLedFlash()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::LED_FLASH;
    cmd["led_id"] = QString::number(ledIdSpinBox->value());
    cmd["brightness"] = QString::number(ledBrightnessSlider->value());
    cmd["duration_ms"] = QString::number(ledFlashDurationSpinBox->value());
    sendCommand(cmd);
    addLog(QString("LED %1 flash: brightness %2, duration %3ms")
               .arg(ledIdSpinBox->value())
               .arg(ledBrightnessSlider->value())
               .arg(ledFlashDurationSpinBox->value()),
           "info");
}

void MainWindow::onLedGpioFlash()
{
    // Arm the selected LED first
    QJsonObject selectCmd;
    selectCmd[Param::COMMAND] = Command::LED_SELECT;
    selectCmd["led_ids"] = QJsonArray{ledIdSpinBox->value()};
    selectCmd["led_brightnesses"] = QJsonArray{ledBrightnessSlider->value()};
    sendCommand(selectCmd);

    QJsonObject cmd;
    cmd[Param::COMMAND]  = Command::LED_GPIO_FLASH;
    cmd["brightness"]    = ledBrightnessSlider->value();
    cmd["duration_us"]   = ledFlashDurationSpinBox->value();
    sendCommand(cmd);

    addLog(QString("GPIO flash: brightness=%1, duration=%2 µs (%3 ms)")
               .arg(ledBrightnessSlider->value())
               .arg(ledFlashDurationSpinBox->value())
               .arg(ledFlashDurationSpinBox->value() / 1000.0, 0, 'f', 3),
           "info");
}

void MainWindow::onLedAllOff()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::LED_ALL_OFF;
    sendCommand(cmd);
    addLog("All LEDs off (disarmed)", "info");
}

void MainWindow::onLedOff()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::LED_OFF;
    cmd["led_id"] = QString::number(ledIdSpinBox->value());
    sendCommand(cmd);
    addLog(QString("LED %1 off").arg(ledIdSpinBox->value()), "info");
}




void MainWindow::onLedGpioModeChanged()
{
    QString mode = ledGpioModeCombo->currentData().toString();
    bool enabled = (mode != "off");

    // Enable/disable the delay and post-capture controls
    ledPreFrameDelaySpinBox->setEnabled(enabled);
    ledPostCaptureOffCheckBox->setEnabled(mode == "torch");

    // If strobe, post-capture off doesn't apply (hardware handles it)
    if (mode == "strobe")
        ledPostCaptureOffCheckBox->setChecked(false);

    if (!serverConnection->isConnected())
    {
        return;
    }

    // Send to server
    QJsonObject cmd;
    cmd["command"] = Command::LED_SET_GPIO_MODE;
    cmd["led_gpio_mode"] = mode;
    cmd["led_pre_frame_delay_ms"] = QString::number(ledPreFrameDelaySpinBox->value());
    cmd["led_post_capture_off"] = ledPostCaptureOffCheckBox->isChecked() ? "true" : "false";

    sendCommand(cmd);

    // Log and update status indicator
    if (mode == "off")
    {
        addLog("LED GPIO trigger: Off", "info");
        ledGpioStatusLabel->setText("LED: Off");
        ledGpioStatusLabel->setStyleSheet("QLabel { color: gray; }");
    }
    else
    {
        QString desc = QString("LED GPIO trigger: %1, pre-frame delay %2 ms%3")
                           .arg(mode)
                           .arg(ledPreFrameDelaySpinBox->value())
                           .arg(mode == "torch" && ledPostCaptureOffCheckBox->isChecked()
                                    ? ", off after capture"
                                    : "");
        addLog(desc, "success");

        if (mode == "torch")
        {
            ledGpioStatusLabel->setText(QString("LED: Torch (%1ms)")
                                            .arg(ledPreFrameDelaySpinBox->value()));
            ledGpioStatusLabel->setStyleSheet("QLabel { color: #e67e22; font-weight: bold; }");
        }
        else
        {
            ledGpioStatusLabel->setText(QString("LED: Strobe (%1ms)")
                                            .arg(ledPreFrameDelaySpinBox->value()));
            ledGpioStatusLabel->setStyleSheet("QLabel { color: #9b59b6; font-weight: bold; }");
        }
    }
}

void MainWindow::applyLedExternalControlState(bool external)
{
    // The manual LM3643 controls are only interactive when:
    //   (a) external control is OFF, AND
    //   (b) the LED manager is initialized/available
    bool enableManual = !external && ledInitialized;

    ledIdSpinBox->setEnabled(enableManual);
    ledBrightnessSlider->setEnabled(enableManual);
    ledAllOffButton->setEnabled(enableManual);
    ledTorchButton->setEnabled(enableManual);
    ledOffButton->setEnabled(enableManual);
    ledFlashButton->setEnabled(enableManual);
    ledFlashDurationSpinBox->setEnabled(enableManual);

    // Status label reflects which mode is active
    if (!ledInitialized)
        ledStatusLabel->setText("Not available");
    else if (external)
        ledStatusLabel->setText("External control active");
    else
        ledStatusLabel->setText("Ready (32 LEDs)");
}


void MainWindow::onSelectIntervalStillFolder()
{
    QFileDialog dialog(this, "Select Auto-Save Folder");
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontResolveSymlinks, false);

    // This enables the "New Folder" button in the dialog
    dialog.setOption(QFileDialog::ReadOnly, false);

    // Start from the last used folder, or home
    QString startDir = intervalStillSaveFolder.isEmpty()
                           ? QDir::homePath()
                           : intervalStillSaveFolder;
    dialog.setDirectory(startDir);

    if (dialog.exec() == QDialog::Accepted)
    {
        QString folder = dialog.selectedFiles().first();
        if (!folder.isEmpty())
        {
            intervalStillSaveFolder = folder;

            // Show a truncated path if too long
            QString displayPath = folder;
            if (displayPath.length() > 60)
                displayPath = "…" + displayPath.right(57);

            intervalStillFolderLabel->setText(displayPath);
            intervalStillFolderLabel->setStyleSheet("color: #27ae60; font-style: normal;");
            intervalStillFolderLabel->setToolTip(folder); // full path on hover

            addLog("Auto-save folder: " + folder, "success");
        }
    }
}

void MainWindow::autoSaveIntervalFrame(const QByteArray &frameData,
                                       const QString &modality)
{
    if (intervalStillSaveFolder.isEmpty())
        return;

    QDir dir(intervalStillSaveFolder);
    if (!dir.exists())
    {
        addLog("Auto-save folder missing: " + intervalStillSaveFolder, "error");
        return;
    }

    // Timestamp filename: imx708_still_20260305_143022_456.jpg
    QString timestamp = QDateTime::currentDateTime()
                            .toString("yyyyMMdd_HHmmss_zzz");
    QString filename = QString("%1_%2.jpg").arg(modality).arg(timestamp);
    QString fullPath = dir.filePath(filename);

    // frameData is already JPEG — write directly without re-encoding
    QFile file(fullPath);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(frameData);
        file.close();
        LOG_DEBUG << "Auto-saved: " << fullPath.toStdString() << std::endl;
    }
    else
    {
        addLog("Failed to save: " + fullPath, "error");
    }
}

void MainWindow::unlockFrameDuration()
{
    if (!frameDurationLockCheckBox || !frameDurationLockCheckBox->isChecked())
        return;

    // Disable strobe sync first — it must not fire after the stream stops
    if (strobeVBlankCheckBox && strobeVBlankCheckBox->isChecked())
    {
        strobeVBlankCheckBox->blockSignals(true);
        strobeVBlankCheckBox->setChecked(false);
        strobeLeadTimeSpinBox->setEnabled(false);
        strobeVBlankCheckBox->blockSignals(false);

        QJsonObject strobeCmd;
        strobeCmd[Param::COMMAND] = Command::LED_STROBE_SYNC_ENABLE;
        strobeCmd["enabled"] = false;
        sendCommand(strobeCmd);
    }

    // Send the disable command to the server while the stream is still running
    QString camera = streamCameraSelector->currentData().toString();
    CameraParam enabledParam = (camera == Camera::IMX219)
        ? CameraParam::ARDUCAM_STREAMING_FRAME_DURATION_ENABLED
        : CameraParam::RGB_STREAMING_FRAME_DURATION_ENABLED;
    onCameraParamChanged(enabledParam, false);

    // Update UI without triggering another server send
    frameDurationLockCheckBox->blockSignals(true);
    frameDurationLockCheckBox->setChecked(false);
    frameDurationSpinBox->setEnabled(false);
    strobeVBlankCheckBox->setEnabled(false);
    frameDurationLockCheckBox->blockSignals(false);
}


void MainWindow::updateLedAutoExposureWarning()
{
    if (!ledGpioModeCombo || !ledAutoExposureWarningLabel)
        return;

    QString mode = ledGpioModeCombo->currentData().toString();
    bool ledActive = (mode != "off");

    // Check if auto exposure is enabled for the current capture camera
    bool autoExposure = false;
    // Adapt this to however your capture settings are exposed — e.g.:
    // For the RGB camera single capture controls:
    if (rgbCaptureAutoExposureCheckBox)
        autoExposure = rgbCaptureAutoExposureCheckBox->isChecked();

    ledAutoExposureWarningLabel->setVisible(ledActive && autoExposure);
}

void MainWindow::updateImx708StreamingExposureHint()
{
    if (!rgbStreamingExposureHintLabel)
        return;

    bool manualExposure = !rgbStreamingAutoExposureCheckBox->isChecked();
    bool autoGain = rgbStreamingAutoAnalogGainCheckBox->isChecked();

    rgbStreamingExposureHintLabel->setVisible(manualExposure && autoGain);
}

void MainWindow::updateImx219StreamingExposureHint()
{
    if (!arducamStreamingExposureHintLabel)
        return;

    // IMX219 doesn't have a separate auto gain checkbox in streaming,
    // so show hint whenever manual exposure is enabled
    bool manualExposure = !arducamStreamingAutoExposureCheckBox->isChecked();
    arducamStreamingExposureHintLabel->setVisible(manualExposure);
}


void MainWindow::updateFrameDurationTimingHints()
{
    const double rollingShutter_ms = activeStreamingRollingShutter_ms();
    const double dur_ms  = frameDurationSpinBox  ? frameDurationSpinBox->value()         : 0.0;
    const bool   locked  = frameDurationLockCheckBox && frameDurationLockCheckBox->isChecked();
    const bool   strobe  = strobeVBlankCheckBox  && strobeVBlankCheckBox->isChecked();
    const double flash_ms = strobeLeadTimeSpinBox ? strobeLeadTimeSpinBox->value() : 0.0;

    const double vblank_ms = (locked && rollingShutter_ms > 0.0)
                             ? std::max(0.0, dur_ms - rollingShutter_ms)
                             : 0.0;

    // ── VBlank gap label (shown when locked and timing is available) ─────────
    if (frameDurationVBlankLabel)
    {
        if (locked && rollingShutter_ms > 0.0)
        {
            QString cam = streamCameraSelector
                          ? streamCameraSelector->currentData().toString()
                          : QString();
            frameDurationVBlankLabel->setText(
                QString("Rolling shutter: %1 ms  ·  VBlank gap: %2 ms  (%3)")
                    .arg(rollingShutter_ms, 0, 'f', 1)
                    .arg(vblank_ms,         0, 'f', 1)
                    .arg(cam == Camera::IMX219 ? "IMX219" : "IMX708"));
            frameDurationVBlankLabel->setVisible(true);
        }
        else
        {
            frameDurationVBlankLabel->setVisible(false);
        }
    }

    // ── Flash fit hint (shown when strobe is on) ─────────────────────────────
   if (frameDurationFlashHintLabel)
    {
        if (strobe && locked && rollingShutter_ms > 0.0)
        {
            // lead_time must be less than vblank_ms so the strobe turns on
            // inside the VBlank interval before readout starts.
            const double lead_ms   = flash_ms; // spinbox reused for lead time
            const double margin_ms = vblank_ms - lead_ms;
            QString hint;
            if (lead_ms >= vblank_ms)
            {
                hint = QString("⚠  Lead time (%1 ms) exceeds VBlank gap (%2 ms) — strobe will fire during readout")
                           .arg(lead_ms,   0, 'f', 1)
                           .arg(vblank_ms, 0, 'f', 1);
                frameDurationFlashHintLabel->setStyleSheet(
                    "color: orange; font-size: 11px; font-style: italic;");
            }
            else if (margin_ms < 1.0)
            {
                hint = QString("⚠  %1 ms lead time, only %2 ms margin inside VBlank — very tight")
                           .arg(lead_ms,   0, 'f', 1)
                           .arg(margin_ms, 0, 'f', 1);
                frameDurationFlashHintLabel->setStyleSheet(
                    "color: orange; font-size: 11px; font-style: italic;");
            }
            else
            {
                hint = QString("✓  %1 ms lead time, %2 ms VBlank gap (%3 ms margin) — LED on for full readout (%4 ms)")
                           .arg(lead_ms,           0, 'f', 1)
                           .arg(vblank_ms,          0, 'f', 1)
                           .arg(margin_ms,          0, 'f', 1)
                           .arg(rollingShutter_ms,  0, 'f', 1);
                frameDurationFlashHintLabel->setStyleSheet(
                    "color: green; font-size: 11px; font-style: italic;");
            }
            frameDurationFlashHintLabel->setText(hint);
            frameDurationFlashHintLabel->setVisible(true);
        }
        else
        {
            frameDurationFlashHintLabel->setVisible(false);
        }
    }
 
    {
        if (strobe && locked && rollingShutter_ms > 0.0)
        {
            const double margin = vblank_ms - flash_ms;
            QString hint;
            if (flash_ms >= vblank_ms)
            {
                hint = QString("⚠  Flash (%1 ms) exceeds VBlank gap (%2 ms) — banding likely")
                           .arg(flash_ms,  0, 'f', 1)
                           .arg(vblank_ms, 0, 'f', 1);
                frameDurationFlashHintLabel->setStyleSheet(
                    "color: orange; font-size: 11px; font-style: italic;");
            }
            else if (margin < 1.5)
            {
                hint = QString("⚠  %1 ms flash, only %2 ms margin — very tight")
                           .arg(flash_ms, 0, 'f', 1)
                           .arg(margin,   0, 'f', 1);
                frameDurationFlashHintLabel->setStyleSheet(
                    "color: orange; font-size: 11px; font-style: italic;");
            }
            else
            {
                hint = QString("✓  %1 ms flash fits inside %2 ms VBlank (%3 ms margin)")
                           .arg(flash_ms,  0, 'f', 1)
                           .arg(vblank_ms, 0, 'f', 1)
                           .arg(margin,    0, 'f', 1);
                frameDurationFlashHintLabel->setStyleSheet(
                    "color: green; font-size: 11px; font-style: italic;");
            }
            frameDurationFlashHintLabel->setText(hint);
            frameDurationFlashHintLabel->setVisible(true);
        }
        else
        {
            frameDurationFlashHintLabel->setVisible(false);
        }
    }
}


double MainWindow::activeStreamingRollingShutter_ms() const
{
    QString cam = streamCameraSelector->currentData().toString();
    const SensorTimingInfo &t = (cam == Camera::IMX708) ? imx708Timing : imx219Timing;
    if (!t.valid)
        return 0.0;
    return t.rollingShutter_us / 1000.0;
}

void MainWindow::onServerErrorMessage(const QString &message)
{
    addLog(QString("✗ Error: %1").arg(message), "error");
    if (message.contains("Stream") || message.contains("stream"))
    {
        streamingActive = false;
        streamStartButton->setEnabled(true);
        streamDualButton->setEnabled(true);
        streamStopButton->setEnabled(false);
        streamStatusLabel->setText("Idle");
    }
}

void MainWindow::onCameraTemperatureReceived(const QJsonObject &response)
{
    if (!imx708TempLabel || !imx708TempAgeLabel)
        return;

    // Error responses — sensor not supported or no reading yet
    if (response.contains("error"))
    {
        imx708TempLabel->setText("N/A");
        imx708TempAgeLabel->setText("");
        return;
    }

    int celsius = response["celsius"].toInt();
    bool reliable = response["reliable"].toBool();
    double age_s = response["age_seconds"].toDouble();

    QString tempStr = QString("%1 °C").arg(celsius);
    if (!reliable)
        tempStr += " ⚠"; // flag stale / unreliable readings

    imx708TempLabel->setText(tempStr);
    imx708TempLabel->setStyleSheet(
        celsius >= 80 ? "font-weight: bold; color: #e74c3c;" : // hot  — red
            celsius >= 65 ? "font-weight: bold; color: #e67e22;"
                          : // warm — orange
            "font-weight: bold;");

    if (age_s < 5.0)
        imx708TempAgeLabel->setText("");
    else
        imx708TempAgeLabel->setText(QString("(%1s ago)").arg(static_cast<int>(age_s)));
}



void MainWindow::onStatusReceived(const QString &message, const QJsonObject &response)
{
    namespace StatusMsg = sanuwave::protocol::StatusMessage;

    // ── Device list (initial GET_STATUS response) ────────────────────────────
    if (message.isEmpty() && response.contains(sanuwave::protocol::Device::IMX708_CAMERA))
    {
        QStringList devices;
        if (response[sanuwave::protocol::Device::IMX708_CAMERA].toBool())      devices << "RGB (IMX708)";
        if (response[sanuwave::protocol::Device::IMX219_CAMERA].toBool())      devices << "Arducam (IMX219)";
        if (response[sanuwave::protocol::Device::THERMAL_CAMERA].toBool())     devices << "Thermal";
        if (response[sanuwave::protocol::Device::DISTANCE_SENSOR].toBool())    devices << "Distance";
        if (response[sanuwave::protocol::Device::UV_SENSOR].toBool())          devices << "UV";
        if (response[sanuwave::protocol::Device::ALS_SENSOR].toBool())         devices << "ALS";
        if (response[sanuwave::protocol::Device::LED_MANAGER].toBool())        devices << "LEDs";
        addLog(QString("✔ Server status: %1").arg(devices.join(", ")), "success");
    }
    else if (!message.isEmpty())
    {
        addLog(QString("✔ Status: %1").arg(message), "success");
    }

    // ── Version check (runs once per connection) ─────────────────────────────
    if (!versionChecked && response.contains(sanuwave::protocol::VersionField::KEY_GIT_HASH))
    {
        QString serverHash    = response[sanuwave::protocol::VersionField::KEY_GIT_HASH].toString();
        QString serverBranch  = response[sanuwave::protocol::VersionField::KEY_GIT_BRANCH].toString();
        QString serverBuild   = response[sanuwave::protocol::VersionField::KEY_BUILD_TIME].toString();
        QString serverVersion = response[sanuwave::protocol::VersionField::KEY_VERSION_STR].toString();
        QString clientHash    = Version::gitHash();

        addLog(QString("DEBUG version fields: hash=%1 branch=%2 build=%3 version=%4")
                   .arg(serverHash).arg(serverBranch).arg(serverBuild).arg(serverVersion), "info");
        addLog(QString("Server version: %1  branch: %2  built: %3")
                   .arg(serverVersion).arg(serverBranch).arg(serverBuild), "info");
        addLog(QString("Client version: %1  branch: %2")
                   .arg(Version::fullVersion()).arg(Version::branch()), "info");

        if (serverHash != clientHash)
        {
            addLog(QString("⚠ Version mismatch — client: %1, server: %2")
                       .arg(clientHash).arg(serverHash), "error");

            QMessageBox::warning(this, "Version Mismatch",
                QString("Version mismatch between client and server.\n\n"
                        "Client:  %1  (%2)\n"
                        "Server:  %3  (%4  built %5)\n\n"
                        "Behaviour may be unpredictable. Proceed with caution.")
                    .arg(Version::fullVersion(), Version::branch(),
                         serverVersion, serverBranch, serverBuild));
        }
        else
        {
            addLog(QString("✔ Version match: %1").arg(clientHash), "success");
        }

        versionChecked = true;
    }

    namespace LRF = sanuwave::protocol::LensRangeField;

    if (response.contains(LRF::IMX708_MAX))
    {
        double max = response[LRF::IMX708_MAX].toDouble();
        double min = response.value(LRF::IMX708_MIN).toDouble(0.0);
        rgbStreamingLensPositionSpinBox->setRange(min, max);
        rgbCaptureLensPositionSpinBox->setRange(min, max);
        focusSweepMax = max;
    }

    if (response.contains(LRF::IMX219_MAX))
    {
        double max = response[LRF::IMX219_MAX].toDouble();
        double min = response.value(LRF::IMX219_MIN).toDouble(0.0);
        arducamStreamingLensPositionSpinBox->setRange(min, max);
        arducamCaptureLensPositionSpinBox->setRange(min, max);
    }

    namespace FDField = sanuwave::protocol::FrameDurationField;

    if (response.contains(FDField::MIN_US) && response.contains(FDField::MAX_US))
    {
        naturalFrameDurationMin_us = static_cast<int64_t>(response[FDField::MIN_US].toDouble());
        naturalFrameDurationMax_us = static_cast<int64_t>(response[FDField::MAX_US].toDouble());

        double maxFps = 1e6 / static_cast<double>(naturalFrameDurationMin_us);
        double minFps = 1e6 / static_cast<double>(naturalFrameDurationMax_us);

        addLog(QString("Frame duration limits: min=%1 µs (%2 fps max), max=%3 µs (%4 fps min)")
                   .arg(naturalFrameDurationMin_us)
                   .arg(maxFps, 0, 'f', 1)
                   .arg(naturalFrameDurationMax_us)
                   .arg(minFps, 0, 'f', 1), "info");

        if (frameDurationSpinBox && naturalFrameDurationMin_us > 0 && naturalFrameDurationMax_us > 0)
        {
            double minMs = naturalFrameDurationMin_us / 1000.0;
            double maxMs = qMin(naturalFrameDurationMax_us / 1000.0, 5000.0);
            frameDurationSpinBox->setRange(minMs, maxMs);
            frameDurationSpinBox->setValue(minMs);
            if (minMs > 0.0)
                frameDurationFpsLabel->setText(QString("%1 fps").arg(1000.0 / minMs, 0, 'f', 1));
        }
    }

    // ── Stream state ─────────────────────────────────────────────────────────
    if (message.contains(StatusMsg::STREAM_STARTED))
    {
        streamingActive = true;
        streamStartButton->setEnabled(false);
        streamDualButton->setEnabled(false);
        streamStopButton->setEnabled(true);
        streamStatusLabel->setText(QString("🔴 Live: %1")
                                       .arg(response[Param::MODALITY].toString()));
    }
    else if (message.contains(StatusMsg::STREAM_STOPPED))
    {
        streamStatusLabel->setText("Idle");
    }

    // ── Distance sensor ───────────────────────────────────────────────────────
    else if (message.contains(StatusMsg::DISTANCE_INITIALIZED))
    {
        distanceInitialized = true;
        distanceStartButton->setEnabled(true);
        distanceStatusLabel->setText("Initialized");
    }
    else if (message.contains(StatusMsg::DISTANCE_STARTED))
    {
        distanceStartButton->setEnabled(false);
        distanceStopButton->setEnabled(true);
        distanceStatusLabel->setText("Ranging");
    }
    else if (message.contains(StatusMsg::DISTANCE_STOPPED))
    {
        distanceStartButton->setEnabled(true);
        distanceStopButton->setEnabled(false);
        distanceStatusLabel->setText("Stopped");
    }

    // ── UV sensor ─────────────────────────────────────────────────────────────
    else if (message.contains(StatusMsg::UV_INITIALIZED))
    {
        uvInitialized = true;
        uvInitButton->setEnabled(false);
        uvReadButton->setEnabled(true);
        uvShutdownButton->setEnabled(true);
        uvGainCombo->setEnabled(true);
        uvIntegrationTimeCombo->setEnabled(true);
        uvModeCombo->setEnabled(true);
        uvStatusLabel->setText("Initialized");
    }
    else if (message.contains(StatusMsg::UV_SHUTDOWN))
    {
        uvInitialized = false;
        uvInitButton->setEnabled(true);
        uvReadButton->setEnabled(false);
        uvShutdownButton->setEnabled(false);
        uvGainCombo->setEnabled(false);
        uvIntegrationTimeCombo->setEnabled(false);
        uvModeCombo->setEnabled(false);
        uvStatusLabel->setText("Not initialized");
    }
    // ── IMU sensor ────────────────────────────────────────────────────────────
    else if (message.contains(StatusMsg::IMU_INITIALIZED))
    {
        imuInitialized = true;
        imuStartButton->setEnabled(true);
        imuConfigureButton->setEnabled(true);
        imuStatusLabel->setText("Initialized");
    }
    else if (message.contains(StatusMsg::IMU_STARTED))
    {
        imuStreaming = true;
        imuStartButton->setEnabled(false);
        imuStopButton->setEnabled(true);
        imuConfigureButton->setEnabled(false);
        imuStatusLabel->setText("Streaming");
    }
    else if (message.contains(StatusMsg::IMU_STOPPED))
    {
        imuStreaming = false;
        imuStartButton->setEnabled(imuInitialized);
        imuStopButton->setEnabled(false);
        imuConfigureButton->setEnabled(imuInitialized);
        imuStatusLabel->setText(imuInitialized ? "Initialized" : "Not initialized");
    }
    else if (message.contains(StatusMsg::IMU_CONFIGURED))
    {
        addLog("IMU reconfigured", "info");
    }
    // ── ALS sensor ────────────────────────────────────────────────────────────
    else if (message.contains(StatusMsg::ALS_INITIALIZED))
    {

        alsInitialized = true;
        alsInitButton->setEnabled(false);
        alsReadButton->setEnabled(true);
        alsShutdownButton->setEnabled(true);
        alsGainCombo->setEnabled(true);
        alsExposureSpinBox->setEnabled(true);
        alsStatusLabel->setText("Initialized");

    }
    else if (message.contains(StatusMsg::ALS_SHUTDOWN))
    {

        alsInitialized = false;
        alsInitButton->setEnabled(true);
        alsReadButton->setEnabled(false);
        alsShutdownButton->setEnabled(false);
        alsGainCombo->setEnabled(false);
        alsExposureSpinBox->setEnabled(false);
        alsStatusLabel->setText("Not initialized");

    }

    // ── LED manager ───────────────────────────────────────────────────────────
    else if (message.contains(StatusMsg::LED_INITIALIZED))
    {
        ledInitialized = true;
        ledAllOffButton->setEnabled(true);
        ledIdSpinBox->setEnabled(true);
        ledBrightnessSlider->setEnabled(true);
        ledTorchButton->setEnabled(true);
        ledFlashButton->setEnabled(true);
        ledFlashDurationSpinBox->setEnabled(true);
        ledOffButton->setEnabled(true);
        ledStatusLabel->setText("Initialized (32 LEDs)");
    }
    else if (message.contains(StatusMsg::LED_SHUTDOWN))
    {
        ledInitialized = false;
        ledAllOffButton->setEnabled(false);
        ledIdSpinBox->setEnabled(false);
        ledBrightnessSlider->setEnabled(false);
        ledTorchButton->setEnabled(false);
        ledFlashButton->setEnabled(false);
        ledFlashDurationSpinBox->setEnabled(false);
        ledOffButton->setEnabled(false);
        ledStatusLabel->setText("Not initialized");
    }

    // ── Auto-enable from GET_STATUS device flags ──────────────────────────────
    if (response.contains(sanuwave::protocol::Device::LED_MANAGER))
    {
        bool available = response[sanuwave::protocol::Device::LED_MANAGER].toBool();
        ledInitialized = available;
        ledAllOffButton->setEnabled(available);
        ledIdSpinBox->setEnabled(available);
        ledBrightnessSlider->setEnabled(available);
        ledTorchButton->setEnabled(available);
        ledFlashButton->setEnabled(available);
        ledFlashDurationSpinBox->setEnabled(available);
        ledOffButton->setEnabled(available);
        ledStatusLabel->setText(available ? "Ready (32 LEDs)" : "Not available");
    }

    if (response.contains(sanuwave::protocol::Device::DISTANCE_SENSOR))
    {
        bool available = response[sanuwave::protocol::Device::DISTANCE_SENSOR].toBool();
        distanceInitialized = available;
        distanceStartButton->setEnabled(available);
        distanceStatusLabel->setText(available ? "Ready" : "Not available");
    }
}


void MainWindow::requestCameraTemperature()
{
    if (!serverConnection->isConnected())
        return;

    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::GET_SENSOR_TEMPERATURE;
    cmd[Param::CAMERA] = Camera::IMX708;
    sendCommand(cmd);
}

void MainWindow::displayALSData(const QJsonObject &data)
{
    if (data["valid"].toBool())
    {
        uint32_t red     = static_cast<uint32_t>(data["red"].toInt());
        uint32_t green   = static_cast<uint32_t>(data["green"].toInt());
        uint32_t blue    = static_cast<uint32_t>(data["blue"].toInt());
        uint32_t clear   = static_cast<uint32_t>(data["clear"].toInt());
        uint32_t ir      = static_cast<uint32_t>(data["ir"].toInt());
        uint32_t visible = static_cast<uint32_t>(data["visible"].toInt());

        alsRedLabel    ->setText(QString::number(red));
        alsGreenLabel  ->setText(QString::number(green));
        alsBlueLabel   ->setText(QString::number(blue));
        alsClearLabel  ->setText(QString::number(clear));
        alsIRLabel     ->setText(QString::number(ir));
        alsVisibleLabel->setText(QString::number(visible));

        // ── Estimated lux from clear channel ─────────────────────────────
        // Scale factor assumes default gain X1 and ~50ms exposure.
        // Not calibrated — label makes this clear to the user.
        constexpr double LUX_SCALE = 2500.0;
        double lux = (LUX_SCALE > 0.0 && clear > 0)
                     ? static_cast<double>(clear) / LUX_SCALE
                     : 0.0;
        alsLuxLabel->setText(QString("~%1 lx (est.)").arg(lux, 0, 'f', 1));

        // ── Estimated CCT from red/blue ratio ─────────────────────────────
        // Rough approximation: warm sources skew red, cool/daylight skew blue.
        // Valid only when both channels have meaningful counts.
        if (red > 100 && blue > 100)
        {
            double ratio = static_cast<double>(blue) / static_cast<double>(red);
            double cct   = 2000.0 + ratio * 3000.0;
            cct = qBound(1800.0, cct, 8000.0);
            alsCCTLabel->setText(QString("~%1 K (est.)").arg(static_cast<int>(cct)));
        }
        else
        {
            alsCCTLabel->setText("— K");
        }

        alsTimestampLabel->setText(QString("Last update: %1")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        alsStatusLabel->setText("OK");
    }
    else
    {
        alsStatusLabel->setText("Read error");
    }
}

void MainWindow::displayDistanceData(const QJsonObject &data)
{
    if (data["valid"].toBool())
    {
        distanceDisplayLabel->setText(QString("%1 mm").arg(data[sanuwave::protocol::DistanceField::DISTANCE_MM].toInt()));
        distanceSignalLabel->setText(
        QString("Signal: %1 cts/SPAD").arg(data[sanuwave::protocol::DistanceField::SIGNAL_PER_SPAD].toInt()));
    }
}

void MainWindow::displayUVData(const QJsonObject &data)
{
    if (data["valid"].toBool())
    {
        uvADisplayLabel->setText(QString("%1 µW/cm²").arg(data["uva"].toDouble(), 0, 'f', 2));
        uvBDisplayLabel->setText(QString("%1 µW/cm²").arg(data["uvb"].toDouble(), 0, 'f', 2));
        uvCDisplayLabel->setText(QString("%1 µW/cm²").arg(data["uvc"].toDouble(), 0, 'f', 2));
        uvTempDisplayLabel->setText(QString("%1 °C").arg(data["temp_c"].toDouble(), 0, 'f', 1));
    }
}

// Replace the existing displayImage() in mainwindow.cpp with this:

void MainWindow::displayImage(const QByteArray &imageData, const QString &modality)
{
    // Check if this is RAW Bayer data — extract raw pixels for DNG export
    // BEFORE debayering to preview
    sanuwave::RawImageData rawData; // Will remain invalid if not RAW

    if (imageData.startsWith("RAW|"))
    {
        sanuwave::RawImageInfo info;
        size_t headerLen = 0;
        const uint8_t *rawPtr = reinterpret_cast<const uint8_t *>(imageData.constData());
        size_t rawSize = static_cast<size_t>(imageData.size());

        if (sanuwave::RawBayerDecoder::parseHeader(rawPtr, rawSize, info, headerLen))
        {
            // Build RawImageData for DNG export from the raw bytes after the header
            rawData = sanuwave::DngExporter::buildFromCapture(
                rawPtr + headerLen,
                rawSize - headerLen,
                info);

            LOG_INFO << "RAW capture stored for DNG export: "
                     << info.width << "x" << info.height
                     << " " << info.bitDepth << "-bit "
                     << sanuwave::RawBayerDecoder::patternToString(info.pattern)
                     << std::endl;
        }
    }

    // Decode image (debayer if RAW, or decode JPEG/PNG)
    QImage image = sanuwave::ImageDecoding::decodeToImage(imageData);

    if (!image.isNull())
    {
        lastImage = image;

        if (imageViewerWindow)
        {
            if (!imageViewerWindow->isVisible())
                imageViewerWindow->show();

            // Set or clear RAW data on the viewer
            if (rawData.isValid())
            {
                imageViewerWindow->setRawData(rawData);
            }
            else
            {
                imageViewerWindow->clearRawData();
            }

            imageViewerWindow->setImage(QPixmap::fromImage(image), modality);
            imageViewerWindow->raise();
        }
    }
}

void MainWindow::displayStreamFrame(const QByteArray &frameData,
                                    const StreamFrameInfo &info)
{
    if (info.modality == Modality::THERMAL)
        thermalDecoder->submitFrame(frameData, info);
    else
        rgbDecoder->submitFrame(frameData, info);

    streamFrameCount++;
}

void MainWindow::displayIntervalFrame(const QByteArray &frameData,
                                      const StreamFrameInfo &info)
{
    // Always save and update status, viewer doesn't need to be open
    autoSaveIntervalFrame(frameData, info.modality);
    intervalStillFrameCount++;
    intervalStillStatusLabel->setText(
        QString("● Shooting — %1 frames").arg(intervalStillFrameCount));

    // Only display if viewer is open
    if (imageViewerWindow && imageViewerWindow->isVisible())
    {
        QImage image = sanuwave::ImageDecoding::decodeJpegToImageWithTurboJpeg(frameData);
        if (!image.isNull())
            displayImage(frameData, info.modality);
    }
}

void MainWindow::requestSensorTiming(const QString &camera, int width, int height)
{
    if (!serverConnection->isConnected())
        return;

    if (camera != Camera::IMX708 && camera != Camera::IMX219)
        return; // thermal doesn't have timing info

    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::GET_SENSOR_TIMING;
    cmd[Param::CAMERA] = camera;
    cmd[Param::WIDTH] = QString::number(width);
    cmd[Param::HEIGHT] = QString::number(height);
    sendCommand(cmd);
}

void MainWindow::updateTimingDisplay(const QString &camera, const SensorTimingInfo &timing)
{
    QLabel *mainLabel = nullptr;
    QLabel *detailLabel = nullptr;

    if (camera == Camera::IMX708)
    {
        mainLabel = imx708TimingLabel;
        detailLabel = imx708TimingDetailLabel;
    }
    else if (camera == Camera::IMX219)
    {
        mainLabel = imx219TimingLabel;
        detailLabel = imx219TimingDetailLabel;
    }

    if (!mainLabel || !detailLabel)
        return;

    if (!timing.valid)
    {
        mainLabel->setText("Not available");
        detailLabel->setText("");
        return;
    }

    // Main display: rolling shutter window (most important for LED sync)
    QString mainText = QString("Rolling Shutter: %1 ms  |  Line Time: %2 µs")
                           .arg(timing.rollingShutter_us / 1000.0, 0, 'f', 2)
                           .arg(timing.lineTime_us, 0, 'f', 2);
    mainLabel->setText(mainText);

    // Detail display
    QString detailText = QString(
                             "Resolution: %1×%2\n"
                             "HBlank: %3 px  |  VBlank: %4 lines\n"
                             "Frame Time: %5 ms  |  Pixel Rate: %6 MHz")
                             .arg(timing.activeWidth)
                             .arg(timing.activeHeight)
                             .arg(timing.hblank)
                             .arg(timing.vblank)
                             .arg(timing.frameTime_us / 1000.0, 0, 'f', 2)
                             .arg(timing.pixelRate / 1000000.0, 0, 'f', 1);
    detailLabel->setText(detailText);
}

void MainWindow::addLog(const QString &message, const QString &type)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString color = (type == "error") ? "#e74c3c" : (type == "success") ? "#2ecc71"
                                                                        : "#3498db";
    QString html = QString("<span style='color: %1;'>[%2] %3</span>").arg(color).arg(timestamp).arg(message);
    logTextEdit->append(html);
}

void MainWindow::updateConnectionStatus(bool connected)
{
        if (connected)
    {
        statusLabel->setText("🟢 Connected");
        connectButton->setEnabled(false);
        disconnectButton->setEnabled(true);
        if (uvbfVBlankButton) uvbfVBlankButton->setEnabled(true);
    }
    else
    {
        statusLabel->setText("⚫ Disconnected");
        connectButton->setEnabled(true);
        disconnectButton->setEnabled(false);
        if (uvbfVBlankButton) uvbfVBlankButton->setEnabled(false);
    }
}


void MainWindow::sendCommand(const QJsonObject &command)
{
    if (!serverConnection->isConnected())
    {
        if (!shuttingDown)
        {
            QMessageBox::warning(this, "Not Connected", "Please connect to server first");
        }
        return;
    }
    serverConnection->sendCommand(command);
}

void MainWindow::sendCameraParameter(const QString &camera, const QString &parameter,
                                     const QVariant &value, const QString &mode)
{
    serverConnection->sendCameraParameter(camera, parameter, value, mode);
}

// ============================================================================
// CAMERA PARAMETER CHANGED HANDLER
// ============================================================================
void MainWindow::onCameraParamChanged(CameraParam param, QVariant value)
{
    // 1. Look up the routing info
    auto info = CameraParamRouter::lookup(param);
    if (!info)
    {
        LOG_WARNING << "Unknown camera parameter: " << static_cast<int>(param) << std::endl;
        return;
    }

    // 2. Send to server
    sendCameraParameter(info->camera, info->parameter, value, info->mode);

    // 3. Handle UI enable/disable for auto toggles
    if (CameraParamRouter::isAutoToggle(param))
    {
        uiController->handleAutoToggle(param, value.toBool());
    }
}

// ============================================================================
// STREAM HANDLERS
// ============================================================================
void MainWindow::onStreamStart()
{
    rgbDecoder->start();
    thermalDecoder->start();
    lastThermalDecoded = QImage();
    if (motionChart_)
        motionChart_->clear();   // fresh chart for new session
    QString camera = streamCameraSelector->currentData().toString();
    QVariantList dims = streamResolutionCombo->currentData().toList();

    if (dims.size() < 2)
    {
        addLog("Invalid resolution selected", "error");
        return;
    }
    if (imageViewerWindow)
    {
        imageViewerWindow->setRotation180(isRotated(camera));
    }

    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::STREAM_START;
    cmd[Param::CAMERA] = camera; // "imx708", "imx219", or "thermal" - matches server
    cmd[Param::WIDTH] = QString::number(dims[0].toInt());
    cmd[Param::HEIGHT] = QString::number(dims[1].toInt());

    if (camera != Camera::THERMAL)
    {
        cmd[Param::QUALITY] = QString::number(streamQualitySpinBox->value());
    }

    // Motion measurement (rgb only). The server ignores these params on
    // thermal streams, but we omit them anyway to keep the command tidy.
    if (camera != Camera::THERMAL && streamMotionEnabledCheckBox &&
        streamMotionEnabledCheckBox->isChecked())
    {
        cmd[Param::MOTION_ENABLED]   = "true";
        cmd[Param::MOTION_ROI_SIZE]  =
            QString::number(settings->value("motion/roiSize", 512).toInt());
        cmd[Param::MOTION_REFERENCE] =
            settings->value("motion/reference",
                            sanuwave::protocol::MotionReference::PREVIOUS)
                .toString();
    }

    sendCommand(cmd);
}

void MainWindow::onStreamStartDual()
{
    isDualStreaming = true;
    rgbDecoder->start();
    thermalDecoder->start();
    lastThermalDecoded = QImage();
    if (motionChart_)
        motionChart_->clear();   // fresh chart for new session
    QVariantList dims = streamResolutionCombo->currentData().toList();
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::STREAM_START;
    cmd[Param::CAMERA] = Camera::DUAL;
    cmd[Param::WIDTH] = QString::number(dims[0].toInt());
    cmd[Param::HEIGHT] = QString::number(dims[1].toInt());
    cmd[Param::QUALITY] = QString::number(streamQualitySpinBox->value());

    // Motion measurement applies to dual stream's rgb leg only (server-
    // side gate). Thermal frames ignore these.
    if (streamMotionEnabledCheckBox && streamMotionEnabledCheckBox->isChecked())
    {
        cmd[Param::MOTION_ENABLED]   = "true";
        cmd[Param::MOTION_ROI_SIZE]  =
            QString::number(settings->value("motion/roiSize", 512).toInt());
        cmd[Param::MOTION_REFERENCE] =
            settings->value("motion/reference",
                            sanuwave::protocol::MotionReference::PREVIOUS)
                .toString();
    }

    if (imageViewerWindow)
    {
        imageViewerWindow->setRotation180(isRotated(Camera::IMX708));
    }

    sendCommand(cmd);
}

void MainWindow::onStreamFrameDecoded(const QImage &image,
                                      const StreamFrameInfo &info)
{
    // Forward to calibration dialog regardless of viewer state
    if (lensCalibDialog && !image.isNull())
        lensCalibDialog->onStreamFrame(image);

    // Viewer is optional — only update if it's open
    if (!imageViewerWindow || !imageViewerWindow->isVisible())
        return;

    if (isDualStreaming)
    {
        if (info.modality == Modality::THERMAL)
        {
            lastThermalDecoded = image;
        }
        else
        {
            if (!lastThermalDecoded.isNull())
                imageViewerWindow->setDualFrame(image, lastThermalDecoded);
            else
                imageViewerWindow->updateStreamFrame(image, info.modality);
        }
    }
    else
    {
        imageViewerWindow->updateStreamFrame(image, info.modality);
    }
}

void MainWindow::onStreamStop()
{
    LOG_INFO << "onStreamStop called" << std::endl;

    streamingActive = false;
    isDualStreaming = false;

    // Clear dual stream buffers
    rgbDecoder->stop();
    thermalDecoder->stop();
    lastThermalDecoded = QImage();
    streamStartButton->setEnabled(true);
    streamDualButton->setEnabled(true);
    streamStopButton->setEnabled(false);
    streamStatusLabel->setText("Stopping...");


    unlockFrameDuration();

    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::STREAM_STOP;
    sendCommand(cmd);

    addLog("Stream stop requested", "info");
    if (lensCalibDialog)
    {
        lensCalibDialog->onStreamStopped();
    }
}

void MainWindow::onStreamResolutionChanged(int)
{
    if (streamingActive)
    {
        onStreamStop();
    }
    // Request updated timing for new resolution
    QString camera = streamCameraSelector->currentData().toString();
    if (camera != Camera::THERMAL)
    {
        QVariantList dims = streamResolutionCombo->currentData().toList();
        if (dims.size() >= 2)
            requestSensorTiming(camera, dims[0].toInt(), dims[1].toInt());
    }
}

void MainWindow::onStreamQualityChanged([[maybe_unused]] int index)
{
    if (streamingActive)
    {
        onStreamStop();
    }

    // Request updated timing for new resolution
    QString camera = streamCameraSelector->currentData().toString();
    if (camera != Camera::THERMAL)
    {
        QVariantList dims = streamResolutionCombo->currentData().toList();
        if (dims.size() >= 2)
            requestSensorTiming(camera, dims[0].toInt(), dims[1].toInt());
    }
}

bool MainWindow::isRotated(const QString &cameraId) const
{
    return cameraRotation.value(cameraId, false);
}

void MainWindow::updateRotationCheckBox(QCheckBox *checkBox, const QString &cameraId)
{
    if (!checkBox)
        return;
    checkBox->blockSignals(true);
    checkBox->setChecked(cameraRotation.value(cameraId, false));
    checkBox->blockSignals(false);
}

void MainWindow::onRotationChanged(bool checked, const QString &context)
{
    QString cameraId;
    if (context == "stream")
        cameraId = streamCameraSelector->currentData().toString();
    else
        cameraId = singleCaptureCameraSelector->currentData().toString();

    cameraRotation[cameraId] = checked;

    // Sync the other checkbox if it's showing the same camera
    if (context == "stream")
    {
        if (singleCaptureCameraSelector->currentData().toString() == cameraId)
            updateRotationCheckBox(captureRotate180CheckBox, cameraId);
    }
    else
    {
        if (streamCameraSelector->currentData().toString() == cameraId)
            updateRotationCheckBox(streamRotate180CheckBox, cameraId);
    }

    if (imageViewerWindow)
        imageViewerWindow->setRotation180(checked);
}

// ============================================================================
// onStreamCameraChanged()
// ============================================================================
void MainWindow::onStreamCameraChanged(int index)
{
    if (streamingActive)
        onStreamStop();
    QString camera = streamCameraSelector->itemData(index).toString();
    streamResolutionCombo->clear();

    if (camera == Camera::IMX708)
    {
        streamResolutionCombo->addItem("🏆 Maximum (4608×2592) - 12MP", QVariantList() << 4608 << 2592);
        streamResolutionCombo->addItem("High (2304×1296) - 3MP", QVariantList() << 2304 << 1296);
        streamResolutionCombo->addItem("HD 1080p (1920×1080)", QVariantList() << 1920 << 1080);
        streamResolutionCombo->addItem("HD 720p (1280×720)", QVariantList() << 1280 << 720);
        streamResolutionCombo->addItem("VGA (640×480)", QVariantList() << 640 << 480);
        streamingRGBCameraControlsGroupBox->setVisible(true);
        streamingRGBCameraControlsGroupBox->setExpanded(true);
        streamingArducamControlsGroupBox->setVisible(false);
        streamingThermalControlsGroupBox->setVisible(false);
        streamResolutionCombo->setCurrentIndex(0);
        streamingFrameDurationGroupBox->setVisible(true);
    }
    else if (camera == Camera::IMX219)
    {
        streamResolutionCombo->addItem("🏆 Maximum (3280×2464) - 8MP", QVariantList() << 3280 << 2464);
        streamResolutionCombo->addItem("High (1920×1080) - 1080p", QVariantList() << 1920 << 1080);
        streamResolutionCombo->addItem("Medium (1640×1232) - 2MP", QVariantList() << 1640 << 1232);
        streamResolutionCombo->addItem("HD 720p (1280×720)", QVariantList() << 1280 << 720);
        streamResolutionCombo->addItem("VGA (640×480)", QVariantList() << 640 << 480);
        streamingRGBCameraControlsGroupBox->setVisible(false);
        streamingArducamControlsGroupBox->setVisible(true);
        streamingArducamControlsGroupBox->setExpanded(true);
        streamingThermalControlsGroupBox->setVisible(false);
        streamResolutionCombo->setCurrentIndex(0);
        streamingFrameDurationGroupBox->setVisible(true);
    }
    else if (camera == Camera::THERMAL)
    {
        streamResolutionCombo->addItem("Native (160×120)", QVariantList() << 160 << 120);
        streamResolutionCombo->addItem("Scaled 2x (320×240)", QVariantList() << 320 << 240);
        streamResolutionCombo->addItem("Scaled 4x (640×480)", QVariantList() << 640 << 480);
        streamResolutionCombo->addItem("Scaled 8x (1280×960)", QVariantList() << 1280 << 960);
        streamResolutionCombo->setCurrentIndex(2); // Default to 4x scaled
        streamingRGBCameraControlsGroupBox->setVisible(false);
        streamingArducamControlsGroupBox->setVisible(false);
        streamingThermalControlsGroupBox->setVisible(true);
        streamingThermalControlsGroupBox->setExpanded(true);
        streamingFrameDurationGroupBox->setVisible(false);
    }

    if (camera != Camera::THERMAL)
    {
        QVariantList dims = streamResolutionCombo->currentData().toList();
        if (dims.size() >= 2)
        {
            QString timingCamera = (camera == Camera::IMX708) ? Camera::IMX708 : Camera::IMX219;
            requestSensorTiming(timingCamera, dims[0].toInt(), dims[1].toInt());
        }
    }
    updateRotationCheckBox(streamRotate180CheckBox, camera);
    updateFrameDurationTimingHints();
}

void MainWindow::onSingleCaptureResolutionChanged(int index)
{
    Q_UNUSED(index);
    QVariantList dims = singleCaptureResolutionCombo->currentData().toList();
    if (dims.size() < 2)
        return;
    int width = dims[0].toInt();
    int height = dims[1].toInt();
    int megapixels = (width * height) / 1000000;
    if (megapixels >= 8)
        singleCaptureQualitySpinBox->setValue(90);
    else if (megapixels >= 2)
        singleCaptureQualitySpinBox->setValue(92);
    else
        singleCaptureQualitySpinBox->setValue(95);
    addLog(QString("Resolution changed to %1×%2 (%3MP)").arg(width).arg(height).arg(megapixels), "info");
    QString camera = singleCaptureCameraSelector->currentData().toString();
    if (camera != Camera::THERMAL)
    {
        requestSensorTiming(camera, width, height);
    }
}

// ============================================================================
// UPDATE onSingleCaptureCameraChanged() - Show/hide thermal controls
// ============================================================================
void MainWindow::onSingleCaptureCameraChanged(int index)
{
    QString camera = singleCaptureCameraSelector->itemData(index).toString();
    singleCaptureResolutionCombo->clear();

    if (camera == Camera::IMX708)
    {
        singleCaptureResolutionCombo->addItem("🏆 Maximum (4608×2592) - 12MP", QVariantList() << 4608 << 2592);
        singleCaptureResolutionCombo->addItem("High (2304×1296) - 3MP", QVariantList() << 2304 << 1296);
        singleCaptureResolutionCombo->addItem("HD 1080p (1920×1080)", QVariantList() << 1920 << 1080);
        singleCaptureResolutionCombo->addItem("HD 720p (1280×720)", QVariantList() << 1280 << 720);
        singleCaptureResolutionCombo->addItem("VGA (640×480)", QVariantList() << 640 << 480);
        singleCaptureRGBControlsGroupBox->setVisible(true);
        singleCaptureRGBControlsGroupBox->setExpanded(true);
        singleCaptureArducamControlsGroupBox->setVisible(false);
        singleCaptureThermalControlsGroupBox->setVisible(false);
        singleCaptureQualitySpinBox->setEnabled(!rgbCaptureRawModeCheckBox->isChecked());
        singleCaptureResolutionCombo->setCurrentIndex(0);
    }
    else if (camera == Camera::IMX219)
    {
        singleCaptureResolutionCombo->addItem("🏆 Maximum (3280×2464) - 8MP", QVariantList() << 3280 << 2464);
        singleCaptureResolutionCombo->addItem("High (1920×1080) - 1080p", QVariantList() << 1920 << 1080);
        singleCaptureResolutionCombo->addItem("Medium (1640×1232) - 2MP", QVariantList() << 1640 << 1232);
        singleCaptureResolutionCombo->addItem("HD 720p (1280×720)", QVariantList() << 1280 << 720);
        singleCaptureResolutionCombo->addItem("VGA (640×480)", QVariantList() << 640 << 480);
        singleCaptureRGBControlsGroupBox->setVisible(false);
        singleCaptureArducamControlsGroupBox->setVisible(true);
        singleCaptureArducamControlsGroupBox->setExpanded(true);
        singleCaptureThermalControlsGroupBox->setVisible(false);
        singleCaptureQualitySpinBox->setEnabled(!arducamCaptureRawModeCheckBox->isChecked());
        singleCaptureResolutionCombo->setCurrentIndex(0);
    }
    else if (camera == Camera::THERMAL)
    {
        singleCaptureResolutionCombo->addItem("Native (160×120)", QVariantList() << 160 << 120);
        singleCaptureResolutionCombo->addItem("Scaled 2x (320×240)", QVariantList() << 320 << 240);
        singleCaptureResolutionCombo->addItem("Scaled 4x (640×480)", QVariantList() << 640 << 480);
        singleCaptureResolutionCombo->addItem("Scaled 8x (1280×960)", QVariantList() << 1280 << 960);
        singleCaptureResolutionCombo->setCurrentIndex(2); // Default to 4x scaled
        singleCaptureRGBControlsGroupBox->setVisible(false);
        singleCaptureArducamControlsGroupBox->setVisible(false);
        singleCaptureThermalControlsGroupBox->setVisible(true);
        singleCaptureThermalControlsGroupBox->setExpanded(true);
        singleCaptureQualitySpinBox->setEnabled(true);
    }

    // Request timing for the selected camera/resolution
    if (camera != Camera::THERMAL)
    {
        QVariantList dims = singleCaptureResolutionCombo->currentData().toList();
        if (dims.size() >= 2)
            requestSensorTiming(camera, dims[0].toInt(), dims[1].toInt());
    }
    updateRotationCheckBox(captureRotate180CheckBox, camera);
}
// ============================================================================
// CAPTURE HANDLERS
// ============================================================================

QJsonObject MainWindow::buildRgbCaptureSettings() const
{
    QJsonObject settings;

    // Exposure
    settings[Param::AUTO_EXPOSURE] = rgbCaptureAutoExposureCheckBox->isChecked();
    settings[Param::EXPOSURE_TIME_US] = QString::number(rgbCaptureExposureSpinBox->value());
    settings[Param::EV_COMPENSATION] = QString::number(rgbCaptureEvCompensationSpinBox->value());

    // Gain
    settings[Param::AUTO_ANALOG_GAIN] = rgbCaptureAutoAnalogGainCheckBox->isChecked();
    settings[Param::ANALOG_GAIN] = QString::number(rgbCaptureAnalogGainSpinBox->value());
    settings[Param::DIGITAL_GAIN] = QString::number(rgbCaptureDigitalGainSpinBox->value());

    // White balance
    settings[Param::AUTO_WHITE_BALANCE] = rgbCaptureAutoWhiteBalanceCheckBox->isChecked();
    settings[Param::RED_GAIN] = QString::number(rgbCaptureRedGainSpinBox->value());
    settings[Param::BLUE_GAIN] = QString::number(rgbCaptureBlueGainSpinBox->value());

    // Focus
    settings[Param::AUTO_FOCUS] = rgbCaptureAutoFocusCheckBox->isChecked();
    settings[Param::LENS_POSITION] = QString::number(rgbCaptureLensPositionSpinBox->value());

    return settings;
}

QJsonObject MainWindow::buildArducamCaptureSettings() const
{
    QJsonObject settings;

    // Exposure
    settings[Param::AUTO_EXPOSURE] = arducamCaptureAutoExposureCheckBox->isChecked();
    settings[Param::EXPOSURE_TIME_US] = QString::number(arducamCaptureExposureSpinBox->value());
    settings[Param::EV_COMPENSATION] = QString::number(arducamCaptureEvCompensationSpinBox->value());

    // Gain
    settings[Param::AUTO_ANALOG_GAIN] = true; // No separate checkbox for Arducam
    settings[Param::ANALOG_GAIN] = QString::number(arducamCaptureAnalogGainSpinBox->value());

    // White balance
    settings[Param::AUTO_WHITE_BALANCE] = arducamCaptureAutoWhiteBalanceCheckBox->isChecked();
    settings[Param::RED_GAIN] = QString::number(arducamCaptureRedGainSpinBox->value());
    settings[Param::BLUE_GAIN] = QString::number(arducamCaptureBlueGainSpinBox->value());

    settings[Param::AUTO_FOCUS] = arducamCaptureAutoFocusCheckBox->isChecked();

    return settings;
}

QJsonObject MainWindow::buildThermalCaptureSettings() const
{
    QJsonObject settings;

    settings[Param::EMISSIVITY] = QString::number(thermalCaptureEmissivitySpinBox->value());
    settings[Param::MIN_TEMP] = QString::number(thermalCaptureMinTempSpinBox->value());
    settings[Param::MAX_TEMP] = QString::number(thermalCaptureMaxTempSpinBox->value());
    settings[Param::COLORMAP] = thermalCaptureColormapCombo->currentData().toString();
    settings[Param::NUC_ENABLED] = thermalCaptureNucEnabledCheckBox->isChecked();
    settings[Param::ALARM_ENABLED] = thermalCaptureAlarmEnabledCheckBox->isChecked();
    settings[Param::ALARM_TEMP] = QString::number(thermalCaptureAlarmTempSpinBox->value());

    return settings;
}

void MainWindow::onCaptureSingle()
{
    QString camera = singleCaptureCameraSelector->currentData().toString();

    // Set rotation — not applied to RAW captures
    bool isRawCapture = false;
    if (camera == Camera::IMX708)
    {
        isRawCapture = rgbCaptureRawModeCheckBox && rgbCaptureRawModeCheckBox->isChecked();
    }
    else if (camera == Camera::IMX219)
    {
        isRawCapture = arducamCaptureRawModeCheckBox && arducamCaptureRawModeCheckBox->isChecked();
    }
    if (imageViewerWindow)
    {
        imageViewerWindow->setRotation180(!isRawCapture && isRotated(camera));
    }

    QVariantList dims = singleCaptureResolutionCombo->currentData().toList();
    if (dims.size() < 2)
    {
        addLog("Invalid resolution selected", "error");
        return;
    }

    int width = dims[0].toInt();
    int height = dims[1].toInt();
    int quality = singleCaptureQualitySpinBox->value();

    QJsonObject cmd;
    cmd[Param::WIDTH] = QString::number(width);
    cmd[Param::HEIGHT] = QString::number(height);
    cmd[Param::CAMERA] = camera;

    if (camera == Camera::IMX708)
    {
        bool rawMode = rgbCaptureRawModeCheckBox && rgbCaptureRawModeCheckBox->isChecked();
        cmd[Param::COMMAND] = rawMode ? Command::CAPTURE_RAW : Command::CAPTURE_RGB;
        if (!rawMode)
            cmd[Param::QUALITY] = QString::number(quality);

        // Merge in all RGB settings
        QJsonObject rgbSettings = buildRgbCaptureSettings();
        for (auto it = rgbSettings.begin(); it != rgbSettings.end(); ++it)
            cmd[it.key()] = it.value();

        addLog(QString("Capturing RGB %1 at %2×%3")
                   .arg(rawMode ? "RAW" : "JPEG")
                   .arg(width)
                   .arg(height),
               "info");
    }
    else if (camera == Camera::IMX219)
    {
        bool rawMode = arducamCaptureRawModeCheckBox && arducamCaptureRawModeCheckBox->isChecked();
        cmd[Param::COMMAND] = rawMode ? Command::CAPTURE_RAW : Command::CAPTURE_ARDUCAM_CUSTOM;
        if (!rawMode)
            cmd[Param::QUALITY] = QString::number(quality);

        // Merge in all Arducam settings
        QJsonObject arducamSettings = buildArducamCaptureSettings();
        for (auto it = arducamSettings.begin(); it != arducamSettings.end(); ++it)
            cmd[it.key()] = it.value();

        addLog(QString("Capturing Arducam %1 at %2×%3")
                   .arg(rawMode ? "RAW" : "JPEG")
                   .arg(width)
                   .arg(height),
               "info");
    }
    else if (camera == Camera::THERMAL)
    {
        cmd[Param::COMMAND] = Command::CAPTURE_THERMAL;
        cmd[Param::SCALE_WIDTH] = cmd.take(Param::WIDTH); // Thermal uses different key names
        cmd[Param::SCALE_HEIGHT] = cmd.take(Param::HEIGHT);
        cmd[Param::QUALITY] = QString::number(quality);

        // Merge in thermal settings
        QJsonObject thermalSettings = buildThermalCaptureSettings();
        for (auto it = thermalSettings.begin(); it != thermalSettings.end(); ++it)
            cmd[it.key()] = it.value();

        addLog(QString("Capturing thermal at %1×%2").arg(width).arg(height), "info");
    }
    QString sid = sessionManager->beginSession({camera});
    cmd["session_id"] = sid;

    sendCommand(cmd);
}

void MainWindow::onIntervalStillStart()
{
    // Reuse the existing single-capture selectors — no duplication
    QString camera = singleCaptureCameraSelector->currentData().toString();
    QVariantList dims = singleCaptureResolutionCombo->currentData().toList();
    int w = dims.size() >= 2 ? dims[0].toInt() : 4608;
    int h = dims.size() >= 2 ? dims[1].toInt() : 2592;
    int quality = singleCaptureQualitySpinBox->value();
    int intervalMs = static_cast<int>(intervalStillIntervalSpinBox->value() * 1000.0);

    QJsonObject cmd;
    cmd[Param::COMMAND] = sanuwave::protocol::Command::INTERVAL_STILL_START;
    cmd[Param::CAMERA] = camera;
    cmd[Param::WIDTH] = w;
    cmd[Param::HEIGHT] = h;
    cmd[Param::QUALITY] = quality;
    cmd[Param::INTERVAL_MS] = intervalMs;
    sendCommand(cmd);

    intervalStillFrameCount = 0;
    intervalStillStartButton->setEnabled(false);
    intervalStillStopButton->setEnabled(true);
    intervalStillIntervalSpinBox->setEnabled(false);
    intervalStillStatusLabel->setText("● Shooting — 0 frames");
    intervalStillStatusLabel->setStyleSheet("color: #27ae60; font-style: normal;");

    addLog(QString("Interval still started: %1 every %2s")
               .arg(camera)
               .arg(intervalStillIntervalSpinBox->value()),
           "success");
}

void MainWindow::onIntervalStillStop()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = sanuwave::protocol::Command::INTERVAL_STILL_STOP;
    sendCommand(cmd);

    intervalStillStartButton->setEnabled(true);
    intervalStillStopButton->setEnabled(false);
    intervalStillIntervalSpinBox->setEnabled(true);
    intervalStillStatusLabel->setText(
        QString("Idle — last session: %1 frames").arg(intervalStillFrameCount));
    intervalStillStatusLabel->setStyleSheet("color: grey; font-style: italic;");

    addLog(QString("Interval still stopped (%1 frames)").arg(intervalStillFrameCount),
           "success");
}


// ============================================================================
// DISTANCE SENSOR HANDLERS
// ============================================================================
void MainWindow::onDistanceInit()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::DISTANCE_INIT;
    sendCommand(cmd);
}

void MainWindow::onDistanceStartStreaming()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::DISTANCE_START;
    sendCommand(cmd);
    distanceStreamTimer->start(100);
    distanceStreaming = true;
}

void MainWindow::onDistanceStopStreaming()
{
    distanceStreamTimer->stop();
    distanceStreaming = false;
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::DISTANCE_STOP;
    sendCommand(cmd);
}

void MainWindow::onDistanceStreamTick()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::DISTANCE_READ;
    sendCommand(cmd);
}

// ============================================================================
// UV SENSOR HANDLERS
// ============================================================================
void MainWindow::onUVInit()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::UV_INIT;
    sendCommand(cmd);
}

void MainWindow::onUVShutdown()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::UV_SHUTDOWN;
    sendCommand(cmd);
}

void MainWindow::onUVRead()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::UV_READ;
    sendCommand(cmd);
}

void MainWindow::onUVGainChanged(int) {}
void MainWindow::onUVIntegrationTimeChanged(int) {}
void MainWindow::onUVModeChanged(int) {}

// ============================================================================
// IMU SENSOR HANDLERS
// ============================================================================
void MainWindow::onImuInit()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::IMU_INIT;
    sendCommand(cmd);
}

void MainWindow::onImuStartStreaming()
{
    // Push cached config (if any) so the session uses last-saved settings.
    // First-run users (empty cache) get the server's compiled defaults.
    if (!imuConfigCache.isEmpty()) {
        QJsonObject cfg = imuConfigCache;
        cfg[Param::COMMAND] = Command::IMU_CONFIGURE;
        sendCommand(cfg);
    }

    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::IMU_START;
    sendCommand(cmd);

    imuStreaming = true;
    imuStartButton->setEnabled(false);
    imuStopButton->setEnabled(true);
    imuConfigureButton->setEnabled(false);  // lock config while streaming
    imuStatusLabel->setText("Streaming");
}

void MainWindow::onImuStopStreaming()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::IMU_STOP;
    sendCommand(cmd);

    imuStreaming = false;
    imuStartButton->setEnabled(imuInitialized);
    imuStopButton->setEnabled(false);
    imuConfigureButton->setEnabled(imuInitialized);
    imuStatusLabel->setText(imuInitialized ? "Initialized" : "Not initialized");
}

void MainWindow::onImuConfigureClicked()
{
    if (imuConfigDialog) {
        imuConfigDialog->raise();
        imuConfigDialog->activateWindow();
        return;
    }

    imuConfigDialog = new ImuConfigDialog(imuConfigCache, serverConnection, this);
    connect(imuConfigDialog, &QDialog::accepted,
            this, &MainWindow::onImuConfigDialogAccepted);
    connect(imuConfigDialog, &QDialog::finished,
            this, [this](int) {
                if (imuConfigDialog) {
                    imuConfigDialog->deleteLater();
                    imuConfigDialog = nullptr;
                }
            });
    imuConfigDialog->show();
}

void MainWindow::onImuConfigDialogAccepted()
{
    if (!imuConfigDialog) return;
    imuConfigCache = imuConfigDialog->resultConfig();

    // Push to the server immediately so a probe can happen with the new
    // settings without requiring a Start.
    QJsonObject cfg = imuConfigCache;
    cfg[Param::COMMAND] = Command::IMU_CONFIGURE;
    sendCommand(cfg);
    addLog("IMU configuration applied", "info");
}



// ============================================================================
// ALS SENSOR HANDLERS
// ============================================================================
void MainWindow::onALSInit()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::ALS_INIT;
    sendCommand(cmd);
}

void MainWindow::onALSShutdown()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::ALS_SHUTDOWN;
    sendCommand(cmd);
}

void MainWindow::onALSRead()
{
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::ALS_READ;
    sendCommand(cmd);
}

void MainWindow::onALSGainChanged(int /*index*/)
{
    if (!alsInitialized)
        return;

    // Combo's userData carries the VD6283TX::Gain enum code (0x01–0x0F).
    // Send as an integer JSON value; the server interprets it as the same enum.
    bool ok = false;
    int gainCode = alsGainCombo->currentData().toInt(&ok);
    if (!ok || gainCode < sanuwave::protocol::Param::AlsGain::MIN_CODE || gainCode > sanuwave::protocol::Param::AlsGain::MAX_CODE)
    {
        return;
    }   

    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::ALS_SET_GAIN;
    cmd[Param::GAIN] = gainCode;
    sendCommand(cmd);
}

void MainWindow::onALSExposureChanged(int value)
{
#
    if (!alsInitialized)
        return;

    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::ALS_SET_EXPOSURE;
    cmd[Param::EXPOSURE_MS] = QString::number(value);
    sendCommand(cmd);

}

void MainWindow::onShowImageViewer()
{
    if (imageViewerWindow)
    {
        imageViewerWindow->show();
        imageViewerWindow->raise();
    }
}

void MainWindow::updateimx708ExposureHint()
{
    if (!rgbCaptureExposureHintLabel)
        return;

    bool manualExposure = !rgbCaptureAutoExposureCheckBox->isChecked();
    bool autoGain = rgbCaptureAutoAnalogGainCheckBox->isChecked();

    // Show hint when exposure is manual but gain is still auto
    rgbCaptureExposureHintLabel->setVisible(manualExposure && autoGain);
}

void MainWindow::updateimx219ExposureHint()
{
    if (!arducamCaptureExposureHintLabel)
        return;

    bool manualExposure = !arducamCaptureAutoExposureCheckBox->isChecked();
    // Arducam doesn't have auto gain checkbox, so just show when manual exposure
    arducamCaptureExposureHintLabel->setVisible(manualExposure);
}

void MainWindow::saveWindowGeometry()
{
    settings->setValue("window/stateVersion", kWindowStateVersion);
    settings->setValue("window/geometry", saveGeometry());
    settings->setValue("window/state", saveState());

    // Save per-camera rotation
    settings->setValue("rotation/imx708", cameraRotation.value(Camera::IMX708, false));
    settings->setValue("rotation/imx219", cameraRotation.value(Camera::IMX219, false));
    settings->setValue("rotation/thermal", cameraRotation.value(Camera::THERMAL, false));
    settings->setValue("session/output_folder", sessionOutputFolder);
}

void MainWindow::restoreWindowGeometry()
{
    // Version check — drop state from older builds rather than risk
    // restoring something the current binary can't parse safely.
    const int storedVersion = settings->value("window/stateVersion", 0).toInt();
    if (storedVersion != kWindowStateVersion)
    {
        LOG_INFO << "Window state version mismatch (stored=" << storedVersion
                 << ", current=" << kWindowStateVersion
                 << "); discarding saved geometry" << std::endl;
        settings->remove("window/geometry");
        settings->remove("window/state");
        return;
    }

    if (!settings->contains("window/geometry"))
        return;

    const QByteArray geom = settings->value("window/geometry").toByteArray();
    const QByteArray state = settings->value("window/state").toByteArray();

    // Sanity check before handing bytes to Qt. Empty arrays are fine
    // (we just skip); absurdly small or large ones get rejected outright.
    auto looksReasonable = [](const QByteArray& ba) -> bool {
        if (ba.isEmpty()) return false;
        // QByteArray for geometry is typically 50-200 bytes; for state a
        // few KB. Anything multi-megabyte is corruption.
        if (ba.size() > 1024 * 1024) return false;
        return true;
    };

    bool geometryRestored = false;
    if (looksReasonable(geom))
    {
        geometryRestored = restoreGeometry(geom);
        if (!geometryRestored)
        {
            LOG_WARNING << "restoreGeometry() rejected saved bytes; clearing" << std::endl;
            settings->remove("window/geometry");
        }
    }

    if (looksReasonable(state))
    {
        if (!restoreState(state))
        {
            LOG_WARNING << "restoreState() rejected saved bytes; clearing" << std::endl;
            settings->remove("window/state");
        }
    }

    if (!geometryRestored)
        return;

    // Existing deferred screen-clamp logic — unchanged behavior.
    QTimer::singleShot(150, this, [this]()
                       {
        QScreen *screen = QGuiApplication::screenAt(geometry().center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;

        QRect available = screen->availableGeometry();
        QRect frame = frameGeometry();
        int titleBarHeight = frame.height() - height();

        LOG_INFO << "Geometry check: available=" << available.width() << "x" << available.height()
                 << " frame=" << frame.width() << "x" << frame.height()
                 << " titleBar=" << titleBarHeight << std::endl;

        // Clamp content size so frame fits in available area
        int maxW = available.width();
        int maxH = available.height() - titleBarHeight;
        bool resized = false;

        if (width() > maxW || height() > maxH)
        {
            resize(qMin(width(), maxW), qMin(height(), maxH));
            resized = true;
        }

        // Force top-left to be within available area, accounting for title bar
        int targetX = x();
        int targetY = y();

        if (frame.top() < available.top())
            targetY = available.top() + titleBarHeight;

        if (targetY + height() > available.bottom())
            targetY = available.bottom() - height();

        if (frame.left() < available.left())
            targetX = available.left();
        if (targetX + width() > available.right())
            targetX = available.right() - width();

        // Final safety: if the resulting rect still doesn't intersect ANY
        // screen, fall back to centered defaults. Handles the multi-monitor-
        // disconnect case where the saved geometry was on a monitor that's
        // gone entirely.
        QRect finalFrame(targetX, targetY, width(), height());
        bool onAnyScreen = false;
        for (QScreen* s : QGuiApplication::screens())
        {
            if (s->availableGeometry().intersects(finalFrame))
            {
                onAnyScreen = true;
                break;
            }
        }
        if (!onAnyScreen)
        {
            LOG_WARNING << "Restored geometry is off-screen on all displays; "
                     << "resetting to centered default" << std::endl;
            resize(1280, 800);
            QRect primary = QGuiApplication::primaryScreen()->availableGeometry();
            move(primary.center().x() - 640, primary.center().y() - 400);
            return;
        }

        if (targetX != x() || targetY != y())
        {
            move(targetX, targetY);
            LOG_INFO << "Window moved to (" << targetX << "," << targetY << ")" << std::endl;
        }

        if (resized)
            LOG_INFO << "Window resized to " << width() << "x" << height() << std::endl;
    });
}

void MainWindow::onUVBFVBlankTriggered()
{
    if (vblankDialog) {
        vblankDialog->raise();
        vblankDialog->activateWindow();
        return;
    }
 
    if (streamingActive)
        onStreamStop();
 
    // Re-use the Arducam capture analog gain as the default for the experiment,
    // since the VBlank burst always uses the IMX219.
    const QString camera    = Camera::IMX219;
    const double  analogGain = arducamCaptureAnalogGainSpinBox->value();
    const QString sessionId  = QString("vblank_%1")
                                   .arg(QDateTime::currentDateTime()
                                            .toString("yyyyMMdd_HHmmsszzz"));
 
    vblankDialog = new UVBFVBlankDialog(serverConnection, camera, analogGain,
                                        sessionId, this);
 
    // ── Wire ServerConnection signals → dialog ────────────────────────────
    connect(serverConnection, &ServerConnection::vblankStarted,
            vblankDialog, [this](int frameCount, const QStringList& roles) {
                if (vblankDialog) vblankDialog->onVBlankStarted(frameCount, roles);
            }, Qt::QueuedConnection);
 
    connect(serverConnection, &ServerConnection::vblankFrameCaptured,
            vblankDialog, [this](const QString& role) {
                if (vblankDialog) vblankDialog->onVBlankFrameCaptured(role);
            }, Qt::QueuedConnection);
 
    connect(serverConnection, &ServerConnection::vblankComplete,
            vblankDialog, [this](const QJsonObject& summary) {
                if (vblankDialog) vblankDialog->onVBlankComplete(summary);
            }, Qt::QueuedConnection);
 
    // Binary frames: reuse uvbfFrameTransferComplete.
    // UVBFVBlankDialog::onFrameTransferComplete guards on sessionId so it
    // ignores any frames that belong to the regular UVBF path.
    connect(serverConnection, &ServerConnection::uvbfFrameTransferComplete,
            vblankDialog, [this](const UVBFFrameInfo& frameInfo,
                                  const QByteArray& dngData) {
                if (vblankDialog)
                    vblankDialog->onFrameTransferComplete(frameInfo, dngData);
            }, Qt::QueuedConnection);
 
    connect(serverConnection, &ServerConnection::uvbfError,
            vblankDialog, [this](const QString& /*stage*/, const QString& reason) {
                if (vblankDialog) vblankDialog->onVBlankError(reason);
            }, Qt::QueuedConnection);
 
    // Clean up when dialog closes
    connect(vblankDialog, &QDialog::finished, this, [this]() {
        if (vblankDialog) {
            serverConnection->disconnect(vblankDialog);
            vblankDialog->deleteLater();
        }
        vblankDialog = nullptr;
    });
 
    vblankDialog->show();
}

void MainWindow::onStrobeVBlankToggled(bool enabled)
{
    strobeLeadTimeSpinBox->setEnabled(enabled);

    if (enabled && !frameDurationLockCheckBox->isChecked())
        frameDurationLockCheckBox->setChecked(true);

    if (!streamingActive)
    {
        if (enabled)
            addLog("Start a stream before enabling VBlank strobe sync", "error");
        strobeVBlankCheckBox->blockSignals(true);
        strobeVBlankCheckBox->setChecked(false);
        strobeLeadTimeSpinBox->setEnabled(false);
        strobeVBlankCheckBox->blockSignals(false);
        return;
    }

    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::LED_STROBE_SYNC_ENABLE;
    cmd["enabled"] = enabled;
    cmd[Param::LED_STROBE_LEAD_TIME_MS] = strobeLeadTimeSpinBox->value();
    sendCommand(cmd);

    addLog(QString("VBlank strobe sync %1").arg(enabled ? "enabled" : "disabled"),
           enabled ? "success" : "info");
    updateFrameDurationTimingHints();
}


void MainWindow::setupSettingsManager()
{
    settingsManager = new CameraSettingsManager(this);

    // RGB Streaming
    settingsManager->registerSpinBox("rgb_streaming_exposure", rgbStreamingExposureSpinBox);
    settingsManager->registerCheckBox("rgb_streaming_auto_exposure", rgbStreamingAutoExposureCheckBox);
    settingsManager->registerDoubleSpinBox("rgb_streaming_ev_comp", rgbStreamingEvCompensationSpinBox);
    settingsManager->registerCheckBox("rgb_streaming_auto_focus", rgbStreamingAutoFocusCheckBox);
    settingsManager->registerDoubleSpinBox("rgb_streaming_lens_pos", rgbStreamingLensPositionSpinBox);
    settingsManager->registerCheckBox("rgb_streaming_auto_gain", rgbStreamingAutoAnalogGainCheckBox);
    settingsManager->registerDoubleSpinBox("rgb_streaming_analog_gain", rgbStreamingAnalogGainSpinBox);
    settingsManager->registerDoubleSpinBox("rgb_streaming_digital_gain", rgbStreamingDigitalGainSpinBox);
    settingsManager->registerCheckBox("rgb_streaming_auto_wb", rgbStreamingAutoWhiteBalanceCheckBox);
    settingsManager->registerDoubleSpinBox("rgb_streaming_red_gain", rgbStreamingRedGainSpinBox);
    settingsManager->registerDoubleSpinBox("rgb_streaming_blue_gain", rgbStreamingBlueGainSpinBox);

    // RGB Capture
    settingsManager->registerSpinBox("rgb_capture_exposure", rgbCaptureExposureSpinBox);
    settingsManager->registerCheckBox("rgb_capture_auto_exposure", rgbCaptureAutoExposureCheckBox);
    settingsManager->registerDoubleSpinBox("rgb_capture_ev_comp", rgbCaptureEvCompensationSpinBox);
    settingsManager->registerCheckBox("rgb_capture_auto_focus", rgbCaptureAutoFocusCheckBox);
    settingsManager->registerDoubleSpinBox("rgb_capture_lens_pos", rgbCaptureLensPositionSpinBox);
    settingsManager->registerCheckBox("rgb_capture_auto_gain", rgbCaptureAutoAnalogGainCheckBox);
    settingsManager->registerDoubleSpinBox("rgb_capture_analog_gain", rgbCaptureAnalogGainSpinBox);
    settingsManager->registerDoubleSpinBox("rgb_capture_digital_gain", rgbCaptureDigitalGainSpinBox);
    settingsManager->registerCheckBox("rgb_capture_auto_wb", rgbCaptureAutoWhiteBalanceCheckBox);
    settingsManager->registerDoubleSpinBox("rgb_capture_red_gain", rgbCaptureRedGainSpinBox);
    settingsManager->registerDoubleSpinBox("rgb_capture_blue_gain", rgbCaptureBlueGainSpinBox);
    settingsManager->registerCheckBox("rgb_capture_raw_mode", rgbCaptureRawModeCheckBox);
    // Arducam Streaming
    settingsManager->registerSpinBox("arducam_streaming_exposure", arducamStreamingExposureSpinBox);
    settingsManager->registerCheckBox("arducam_streaming_auto_exposure", arducamStreamingAutoExposureCheckBox);
    settingsManager->registerDoubleSpinBox("arducam_streaming_ev_comp", arducamStreamingEvCompensationSpinBox);
    settingsManager->registerDoubleSpinBox("arducam_streaming_analog_gain", arducamStreamingAnalogGainSpinBox);
    settingsManager->registerCheckBox("arducam_streaming_auto_wb", arducamStreamingAutoWhiteBalanceCheckBox);
    settingsManager->registerDoubleSpinBox("arducam_streaming_red_gain", arducamStreamingRedGainSpinBox);
    settingsManager->registerDoubleSpinBox("arducam_streaming_blue_gain", arducamStreamingBlueGainSpinBox);

    // Arducam Capture
    settingsManager->registerSpinBox("arducam_capture_exposure", arducamCaptureExposureSpinBox);
    settingsManager->registerCheckBox("arducam_capture_auto_exposure", arducamCaptureAutoExposureCheckBox);
    settingsManager->registerDoubleSpinBox("arducam_capture_ev_comp", arducamCaptureEvCompensationSpinBox);
    settingsManager->registerDoubleSpinBox("arducam_capture_analog_gain", arducamCaptureAnalogGainSpinBox);
    settingsManager->registerCheckBox("arducam_capture_auto_wb", arducamCaptureAutoWhiteBalanceCheckBox);
    settingsManager->registerDoubleSpinBox("arducam_capture_red_gain", arducamCaptureRedGainSpinBox);
    settingsManager->registerDoubleSpinBox("arducam_capture_blue_gain", arducamCaptureBlueGainSpinBox);
    settingsManager->registerCheckBox("arducam_capture_raw_mode", arducamCaptureRawModeCheckBox);

    // Thermal Streaming
    settingsManager->registerDoubleSpinBox("thermal_streaming_emissivity", thermalStreamingEmissivitySpinBox);
    settingsManager->registerDoubleSpinBox("thermal_streaming_min_temp", thermalStreamingMinTempSpinBox);
    settingsManager->registerDoubleSpinBox("thermal_streaming_max_temp", thermalStreamingMaxTempSpinBox);
    settingsManager->registerComboBox("thermal_streaming_colormap", thermalStreamingColormapCombo);
    settingsManager->registerCheckBox("thermal_streaming_nuc", thermalStreamingNucEnabledCheckBox);
    settingsManager->registerCheckBox("thermal_streaming_alarm", thermalStreamingAlarmEnabledCheckBox);
    settingsManager->registerDoubleSpinBox("thermal_streaming_alarm_temp", thermalStreamingAlarmTempSpinBox);

    // Thermal Capture
    settingsManager->registerDoubleSpinBox("thermal_capture_emissivity", thermalCaptureEmissivitySpinBox);
    settingsManager->registerDoubleSpinBox("thermal_capture_min_temp", thermalCaptureMinTempSpinBox);
    settingsManager->registerDoubleSpinBox("thermal_capture_max_temp", thermalCaptureMaxTempSpinBox);
    settingsManager->registerComboBox("thermal_capture_colormap", thermalCaptureColormapCombo);
    settingsManager->registerCheckBox("thermal_capture_nuc", thermalCaptureNucEnabledCheckBox);
    settingsManager->registerCheckBox("thermal_capture_alarm", thermalCaptureAlarmEnabledCheckBox);
    settingsManager->registerDoubleSpinBox("thermal_capture_alarm_temp", thermalCaptureAlarmTempSpinBox);

    // Distance sensor
    settingsManager->registerSpinBox("depth_timing_budget", depthTimingBudgetSpinBox);
    settingsManager->registerSpinBox("depth_inter_measurement", depthInterMeasurementPeriodSpinBox);
    settingsManager->registerSpinBox("depth_sigma_threshold", depthSigmaThresholdSpinBox);
    settingsManager->registerSpinBox("depth_signal_threshold", depthSignalThresholdSpinBox);

    // Quality settings
    settingsManager->registerSpinBox("stream_quality", streamQualitySpinBox);
    settingsManager->registerSpinBox("capture_quality", singleCaptureQualitySpinBox);

    // Camera and resolution selectors
    settingsManager->registerComboBox("stream_camera", streamCameraSelector);
    settingsManager->registerComboBox("capture_camera", singleCaptureCameraSelector);
    settingsManager->registerComboBox("stream_resolution", streamResolutionCombo);
    settingsManager->registerComboBox("capture_resolution", singleCaptureResolutionCombo);

    // Rotation checkboxes (these hold the currently-displayed camera's state)
    settingsManager->registerCheckBox("rotation_stream", streamRotate180CheckBox);
    settingsManager->registerCheckBox("rotation_capture", captureRotate180CheckBox);

    settingsManager->registerComboBox("led_gpio_mode", ledGpioModeCombo);
    settingsManager->registerSpinBox("led_pre_frame_delay_ms", ledPreFrameDelaySpinBox);
    settingsManager->registerCheckBox("led_post_capture_off", ledPostCaptureOffCheckBox);

    connect(settingsManager, &CameraSettingsManager::settingsLoaded,
            this, &MainWindow::onSettingsLoaded);
}

void MainWindow::onSaveAllSettings()
{
    QString filePath = CameraSettingsManager::getSaveFilePath(this);
    if (filePath.isEmpty())
        return;

    QString errorMsg;
    if (settingsManager->saveToFile(filePath, &errorMsg))
    {
        addLog(QString("Settings saved to: %1").arg(filePath), "success");
    }
    else
    {
        QMessageBox::warning(this, "Save Failed", errorMsg);
        addLog(QString("Failed to save settings: %1").arg(errorMsg), "error");
    }
}

void MainWindow::onLoadAllSettings()
{
    if (streamingActive || isDualStreaming || distanceStreaming)
    {
        QMessageBox::warning(this, "Cannot Load Settings",
                             "Please stop all streaming before loading settings.");
        return;
    }

    QString filePath = CameraSettingsManager::getLoadFilePath(this);
    if (filePath.isEmpty())
        return;

    QString errorMsg;
    if (settingsManager->loadFromFile(filePath, &errorMsg))
    {
        addLog(QString("Settings loaded from: %1").arg(filePath), "success");
    }
    else
    {
        QMessageBox::warning(this, "Load Failed", errorMsg);
        addLog(QString("Failed to load settings: %1").arg(errorMsg), "error");
    }
}

void MainWindow::applyCaptureCameraVisibility()
{
    QString camera = singleCaptureCameraSelector->currentData().toString();

    singleCaptureRGBControlsGroupBox->setVisible(camera == Camera::IMX708);
    singleCaptureArducamControlsGroupBox->setVisible(camera == Camera::IMX219);
    singleCaptureThermalControlsGroupBox->setVisible(camera == Camera::THERMAL);

    // Update quality spinbox based on raw mode
    if (camera == Camera::IMX708)
        singleCaptureQualitySpinBox->setEnabled(!rgbCaptureRawModeCheckBox->isChecked());
    else if (camera == Camera::IMX219)
        singleCaptureQualitySpinBox->setEnabled(!arducamCaptureRawModeCheckBox->isChecked());
    else
        singleCaptureQualitySpinBox->setEnabled(true);
}

void MainWindow::applyStreamCameraVisibility()
{
    QString camera = streamCameraSelector->currentData().toString();

    streamingRGBCameraControlsGroupBox->setVisible(camera == Camera::IMX708);
    streamingArducamControlsGroupBox->setVisible(camera == Camera::IMX219);
    streamingThermalControlsGroupBox->setVisible(camera == Camera::THERMAL);
}

void MainWindow::onSettingsLoaded()
{
    // Update panel visibility without repopulating resolution combos
    applyStreamCameraVisibility();
    applyCaptureCameraVisibility();

    // Refresh enable/disable state for dependent widgets
    rgbStreamingExposureSpinBox->setEnabled(!rgbStreamingAutoExposureCheckBox->isChecked());
    rgbStreamingEvCompensationSpinBox->setEnabled(rgbStreamingAutoExposureCheckBox->isChecked());
    rgbStreamingLensPositionSpinBox->setEnabled(!rgbStreamingAutoFocusCheckBox->isChecked());
    rgbStreamingAnalogGainSpinBox->setEnabled(!rgbStreamingAutoAnalogGainCheckBox->isChecked());
    rgbStreamingDigitalGainSpinBox->setEnabled(!rgbStreamingAutoAnalogGainCheckBox->isChecked());

    rgbCaptureExposureSpinBox->setEnabled(!rgbCaptureAutoExposureCheckBox->isChecked());
    rgbCaptureEvCompensationSpinBox->setEnabled(rgbCaptureAutoExposureCheckBox->isChecked());
    rgbCaptureLensPositionSpinBox->setEnabled(!rgbCaptureAutoFocusCheckBox->isChecked());
    rgbCaptureAnalogGainSpinBox->setEnabled(!rgbCaptureAutoAnalogGainCheckBox->isChecked());
    rgbCaptureDigitalGainSpinBox->setEnabled(!rgbCaptureAutoAnalogGainCheckBox->isChecked());

    arducamStreamingExposureSpinBox->setEnabled(!arducamStreamingAutoExposureCheckBox->isChecked());
    arducamStreamingEvCompensationSpinBox->setEnabled(arducamStreamingAutoExposureCheckBox->isChecked());
    arducamCaptureExposureSpinBox->setEnabled(!arducamCaptureAutoExposureCheckBox->isChecked());
    arducamCaptureEvCompensationSpinBox->setEnabled(arducamCaptureAutoExposureCheckBox->isChecked());

    thermalStreamingAlarmTempSpinBox->setEnabled(thermalStreamingAlarmEnabledCheckBox->isChecked());
    thermalCaptureAlarmTempSpinBox->setEnabled(thermalCaptureAlarmEnabledCheckBox->isChecked());

    updateimx708ExposureHint();
    updateImx708StreamingExposureHint();
    updateimx219ExposureHint();
    updateImx219StreamingExposureHint();
    // Sync rotation checkboxes to currently selected cameras
    if (streamRotate180CheckBox && streamCameraSelector)
        updateRotationCheckBox(streamRotate180CheckBox,
                               streamCameraSelector->currentData().toString());
    if (captureRotate180CheckBox && singleCaptureCameraSelector)
        updateRotationCheckBox(captureRotate180CheckBox,
                               singleCaptureCameraSelector->currentData().toString());

    QString ledMode = ledGpioModeCombo->currentData().toString();
    bool ledEnabled = (ledMode != "off");
    ledPreFrameDelaySpinBox->setEnabled(ledEnabled);
    ledPostCaptureOffCheckBox->setEnabled(ledMode == "torch");
    if (ledMode == "strobe")
        ledPostCaptureOffCheckBox->setChecked(false);
    onLedGpioModeChanged();
}
