#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// Copyright 2026 Sanuwave Medical LLC.
// 
// Portions of this code were generated with Claude.ai and
// reviewed and edited.

#include <QMainWindow>
#include <QTcpSocket>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QSettings>
#include <QPixmap>
#include <QImage>
#include <QTimer>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QDockWidget>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QDir>
#include <QProgressBar>
#include <QFormLayout>
#include <QPointer>
#include "settingsdialog.h"
#include "imageviewerwindow.h"
#include "expandablegroupbox.h"
#include "server_connection.h"
#include "camera_param_router.h"
#include "camera_ui_controller.h"
#include "server_connection.h"
#include "calibration_viewer_dialog.h"

class MotionChartWidget;
class MotionSettingsDialog;
#include "lens_calibration_dialog.h"
#include "camera_settings_manager.h"
#include "raw_diag_window.h"
#include "stream_frame_decoder.h"
#include "uvbf_capture_dialog.h"          
#include <optional>
#include "session_manager.h"
class UVBFVBlankDialog;
class ImuConfigDialog;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onServerConnected();
    void onServerDisconnected();
    void onOpenSettings();
    void onCaptureSingle();
    void onStreamStart();

    void onStreamStartDual();
    void onStreamStop();
    void onIntervalStillStart();
    void onIntervalStillStop();
    void onSelectIntervalStillFolder();
    void onStreamCameraChanged(int index);
    void onStreamResolutionChanged(int);
    void onStreamQualityChanged(int);
    void onSingleCaptureCameraChanged(int index);
    QJsonObject buildRgbCaptureSettings() const;
    QJsonObject buildArducamCaptureSettings() const;
    QJsonObject buildThermalCaptureSettings() const;
    void onSingleCaptureResolutionChanged(int index);
    void onCameraParamChanged(CameraParam param, QVariant value);
    void onDistanceInit();
    void onDistanceStartStreaming();
    void onDistanceStopStreaming();
    void onDistanceStreamTick();
    void onUVInit();
    void onUVShutdown();
    void onUVRead();
    void onUVGainChanged(int index);
    void onUVIntegrationTimeChanged(int index);
    void onUVModeChanged(int index);
    void onALSInit();
    void onALSShutdown();
    void onALSRead();
    void onALSGainChanged(double value);
    void onALSExposureChanged(int value);
    void onStrobeVBlankToggled(bool enabled);
    
    void onLedTorch();
    void onLedFlash();
    void onLedOff();
    void onLedAllOff();
    void onLedGpioFlash();  
    void onLedGpioModeChanged();
    void applyLedExternalControlState(bool external);
    void unlockFrameDuration();
    void updateLedAutoExposureWarning();
    void onSessionStarted(const QString& sessionId, const QString& sessionDir);
    void onFrameSaved(const QString& filePath, const QString& sidecarPath);
    void onSelectSessionOutputFolder();
    void updateImx708StreamingExposureHint();
    void updateImx219StreamingExposureHint();
    void onShowImageViewer();
    void onServerError(const QString &message);
    void onImageReceived(const QByteArray &data, const QString &modality);
    void onStreamFrameReceived(const QByteArray &data, const StreamFrameInfo &info);
    void onIntervalFrameReceived(const QByteArray &data, const StreamFrameInfo &info);
    void onStreamFrameDecoded(const QImage &image, const StreamFrameInfo &info);
    void onMotionDecayTimeout();
    void onMotionSettingsClicked();
    void onMotionEnterChanged(double v);
    void onMotionExitChanged(double v);
    void onMotionConfChanged(double v);
    void onMotionDecayChanged(int v);
    void onDistanceDataReceived(const QJsonObject &data);
    void onUVDataReceived(const QJsonObject &data);
    void onImuDataReceived  (const QJsonObject& data);
    void onImuEventReceived (const QJsonObject& evt);
    void onImuInit();
    void onImuStartStreaming();
    void onImuStopStreaming();
    void onImuConfigureClicked();
    void onImuConfigDialogAccepted();
    void onALSDataReceived(const QJsonObject &data);
    void onStatusReceived(const QString &message, const QJsonObject &response);
    void onServerErrorMessage(const QString &message);
    void snapshotCaptureContext(CaptureResultInfo &info) const;
    void onCaptureComplete(const CaptureResultInfo &info);
    void onSensorTimingReceived(const QString &camera, const SensorTimingInfo &timing);
    void onShowCalibrationViewer();


    void onSelectFocusSweepFolder();

    void onOpenRawDiagnostic();
    void onOpenLensCalibration();
    void onSaveAllSettings();
    void onLoadAllSettings();
  
    void onSettingsLoaded();

    // ---- UVBF ----                                               
    void onUVBFCaptureTriggered();                                  
    void onUVBFCaptureModeAck(const QJsonObject& response);        
    void onUVBFFrameTransferProgress(const QJsonObject& envelope,  
                                      int bytesReceived);           
    void onUVBFFrameTransferComplete(const QJsonObject& envelope,   
                                      const QByteArray& dngData);   
    void onUVBFCaptureAccepted(const UVBFSession& session);

    void onUVBFVBlankTriggered();

 
private:
    void setupUI();
    void registerCameraWidgets();
    void setupSettingsManager();
    void applyCaptureCameraVisibility();
    void applyStreamCameraVisibility();
    ExpandableGroupBox* setupConnectionGroup();
    ExpandableGroupBox* setupStreamingGroup();
    ExpandableGroupBox* setUpStreamingSettingsGroup(QWidget* parent);
    ExpandableGroupBox* setUpStreamingRGBControlsGroup(QWidget* parent);
    ExpandableGroupBox* setUpStreamingArducamControlsGroup(QWidget* parent);
    ExpandableGroupBox* setUpStreamingThermalControlsGroup(QWidget* parent);
    ExpandableGroupBox* setUpSingleCaptureGroup();
    ExpandableGroupBox* setupLedControlGroup();
    ExpandableGroupBox* setUpSingleCaptureSettingsGroup(QWidget* parent);
    ExpandableGroupBox* setUpSingleCaptureRGBControlsGroup(QWidget* parent);
    ExpandableGroupBox* setUpSingleCaptureArducamControlsGroup(QWidget* parent);
    ExpandableGroupBox *setUpIntervalCaptureGroup(QWidget *parent);
    ExpandableGroupBox *setUpSingleCaptureThermalControlsGroup(QWidget *parent);
    ExpandableGroupBox *setUpStreamingFrameDurationGroup(QWidget *parent);
    ExpandableGroupBox *setUpDistanceSensorSettingsGroup(QWidget *parent);
    ExpandableGroupBox* setUpImuGroup(QWidget* parent);
    ExpandableGroupBox* setupSensorTimingGroup();
    ExpandableGroupBox* setUpSessionGroup(QWidget* parent);
    void createMenuBar();
    void loadSettings();
    void saveWindowGeometry();
    void restoreWindowGeometry();
    void updateConnectionStatus(bool connected);
    void sendCommand(const QJsonObject& command);
    void addLog(const QString& message, const QString& type = "info");
    void sendCameraParameter(const QString& camera, const QString& parameter, 
                             const QVariant& value, const QString& mode = "capture");
    void displayImage(const QByteArray& imageData, const QString& modality);
    void displayStreamFrame(const QByteArray& frameData, const StreamFrameInfo& info);
    void displayIntervalFrame(const QByteArray &frameData,
                                       const StreamFrameInfo &info);

    // Motion-indicator driver: updates the FPS-strip numeric label, the
    // viewer overlay badge, and the motion-history chart from an rgb
    // stream frame. Caller is expected to gate on rgb-only modalities;
    // thermal frames carry motion.valid==false but the helper handles the
    // dead-data case anyway.
    void updateMotionIndicator(const StreamFrameInfo& info);
    void displayALSData(const QJsonObject& data);
    void requestSensorTiming(const QString &camera, int width, int height);
    void displayDistanceData(const QJsonObject &data);
    void displayUVData(const QJsonObject& data);
    void updateTimingDisplay(const QString &camera, const SensorTimingInfo &timing);
    void updateimx708ExposureHint();
    void updateimx219ExposureHint();

    bool isRotated(const QString& cameraId) const;
    void updateRotationCheckBox(QCheckBox* checkBox, const QString& cameraId);
    void onRotationChanged(bool checked, const QString& context);

    void addFocusSweepWidgets(QFormLayout* focusLayout,
    QPushButton*& sweepButton, QLabel*& sweepLabel, QProgressBar*& sweepBar,
    CameraParam autoParam, CameraParam lensParam,
    QDoubleSpinBox*& lensSpinBox, const QString& saveFolder);

    void startFocusSweep(CameraParam autoParam, CameraParam lensParam,
    QDoubleSpinBox* lensSpinBox, QPushButton* sweepButton,
    QLabel* sweepLabel, QProgressBar* sweepBar,
    const QString& saveFolder, bool saveImages);

    void saveFocusSweepImage(const QByteArray& jpegData, double lensPos, const QString& saveFolder);
    void stopFocusSweep();
    void advanceFocusSweep();

    void requestCameraTemperature();
    void onCameraTemperatureReceived(const QJsonObject &response);
    void autoSaveIntervalFrame(const QByteArray& frameData, const QString& modality);
    void tryFinalizeFrame();

    // ---- UVBF helpers ----                                        // << ADDED
    void forwardSensorStatusToUVBF(const QJsonObject& statusResponse); // << ADDED


    void updateFrameDurationTimingHints();
    double activeStreamingRollingShutter_ms() const;


    // Connection widgets
    QLabel* serverInfoLabel;
    QLabel* statusLabel;
    QPushButton* connectButton;
    QPushButton* disconnectButton;
    
    // Capture buttons
    QPushButton* threeDButton;
    QPushButton* singleCaptureButton;
    QPushButton* uvbfCaptureButton = nullptr;                       
    QPushButton* uvbfVBlankButton = nullptr;

    // Stream controls
    QComboBox* streamCameraSelector;
    QPushButton *streamStartButton;
    QPushButton* streamStopButton;
    QPushButton* streamDualButton;
    QLabel* streamStatusLabel;
    QLabel* streamFpsLabel;
    QLabel* motionLabel_ = nullptr;   // numeric trans_px display in status strip
    QPushButton* motionSettingsBtn_ = nullptr;
    MotionSettingsDialog* motionSettingsDialog_ = nullptr;
    ExpandableGroupBox* motionHistoryGroup_ = nullptr;
    MotionChartWidget*  motionChart_        = nullptr;
    QComboBox* streamResolutionCombo;
    QSpinBox* streamQualitySpinBox;
    QCheckBox* streamRotate180CheckBox = nullptr;
    QCheckBox* streamMotionEnabledCheckBox = nullptr;   // motion measurement on/off
    // Frame duration lock UI
    ExpandableGroupBox *streamingFrameDurationGroupBox = nullptr;
    QCheckBox         *frameDurationLockCheckBox       = nullptr;
    QDoubleSpinBox    *frameDurationSpinBox             = nullptr;
    QLabel            *frameDurationFpsLabel            = nullptr;
    QCheckBox          *strobeVBlankCheckBox           = nullptr;
    QDoubleSpinBox     *strobeLeadTimeSpinBox     = nullptr;
    // Single capture controls
    QComboBox* singleCaptureCameraSelector;
    QComboBox* singleCaptureResolutionCombo;
    QSpinBox* singleCaptureQualitySpinBox;
    QCheckBox* captureRotate180CheckBox = nullptr;
    // Interval still controls
    QDoubleSpinBox* intervalStillIntervalSpinBox;
    QPushButton*    intervalStillStartButton;
    QPushButton*    intervalStillStopButton;
    QLabel*         intervalStillStatusLabel;
    int             intervalStillFrameCount = 0;
    QString      intervalStillSaveFolder;
    QCheckBox*   intervalStillAutoSaveCheckBox;
    QPushButton* intervalStillFolderButton;
    QLabel*      intervalStillFolderLabel;

    // Distance sensor
    QPushButton* distanceStartButton;
    QPushButton* distanceStopButton;
    QLabel* distanceStatusLabel;
    QLabel* distanceDisplayLabel;
    QLabel* distanceSignalLabel;
    // Camera temperature
    QLabel *imx708TempLabel    = nullptr;
    QLabel *imx708TempAgeLabel = nullptr;
    QTimer *cameraTemperatureTimer = nullptr;

    // ========== LED CONTROLS ==========
    QPushButton* ledTorchButton;
    QPushButton* ledFlashButton;
    QPushButton* ledOffButton;
    QPushButton* ledAllOffButton;
    QSpinBox* ledIdSpinBox;
    QSlider* ledBrightnessSlider;
    QLabel* ledBrightnessValueLabel;
    QSpinBox* ledFlashDurationSpinBox;
    QCheckBox* ledStrobeEnableCheckBox;
    QLabel *frameDurationVBlankLabel    = nullptr;
    QLabel *frameDurationFlashHintLabel = nullptr;
    QWidget *frameDurationTimingWidget  = nullptr; // the bar diagram container
    QLabel* ledStatusLabel;
    QLabel *ledAutoExposureWarningLabel;
    QCheckBox* ledExternalControlCheckBox;
    bool ledInitialized = false;
    double imx708LensMin = 0.0;
    double imx708LensMax = 32.0;
    double imx219LensMin = 0.0;
    double imx219LensMax = 32.0;
    // session management
    SessionManager* sessionManager = nullptr;
    QString         sessionOutputFolder;
    std::optional<CaptureResultInfo> pendingCaptureInfo;
    QByteArray                       pendingImageData;
    QString                          pendingImageModality;
    QLabel* sessionIdLabel         = nullptr;
    QLabel* sessionFolderLabel     = nullptr;
    QLabel* sessionFrameCountLabel = nullptr;
    // Image viewer
    QPushButton* showImageViewerButton;
    QTextEdit* logTextEdit;
    QDockWidget* logDock;
    CameraUIController* uiController;
    
    SensorTimingInfo imx708Timing;
    SensorTimingInfo imx219Timing;

    // ========== STREAMING RGB CONTROLS ==========
    ExpandableGroupBox* streamingRGBCameraControlsGroupBox;
    QSpinBox* rgbStreamingExposureSpinBox;
    QCheckBox* rgbStreamingAutoExposureCheckBox;
    QLabel* rgbStreamingExposureHintLabel = nullptr;
    QLabel* arducamStreamingExposureHintLabel = nullptr;
    QDoubleSpinBox* rgbStreamingEvCompensationSpinBox;
    QCheckBox* rgbStreamingAutoFocusCheckBox;
    QDoubleSpinBox* rgbStreamingLensPositionSpinBox;
    QCheckBox* rgbStreamingAutoAnalogGainCheckBox;
    QDoubleSpinBox* rgbStreamingAnalogGainSpinBox;
    QDoubleSpinBox* rgbStreamingDigitalGainSpinBox;
    QCheckBox* rgbStreamingAutoWhiteBalanceCheckBox;
    QDoubleSpinBox* rgbStreamingRedGainSpinBox;
    QDoubleSpinBox* rgbStreamingBlueGainSpinBox;
    
    // ========== SINGLE CAPTURE RGB CONTROLS ==========
    ExpandableGroupBox* singleCaptureRGBControlsGroupBox;
    QSpinBox* rgbCaptureExposureSpinBox;
    QLabel* rgbCaptureExposureHintLabel;
    QCheckBox* rgbCaptureAutoExposureCheckBox;
    QDoubleSpinBox* rgbCaptureEvCompensationSpinBox;
    QCheckBox* rgbCaptureAutoFocusCheckBox;
    QDoubleSpinBox* rgbCaptureLensPositionSpinBox;
    QCheckBox* rgbCaptureAutoAnalogGainCheckBox;
    QDoubleSpinBox* rgbCaptureAnalogGainSpinBox;
    QDoubleSpinBox* rgbCaptureDigitalGainSpinBox;
    QCheckBox* rgbCaptureAutoWhiteBalanceCheckBox;
    QDoubleSpinBox* rgbCaptureRedGainSpinBox;
    QDoubleSpinBox* rgbCaptureBlueGainSpinBox;
    QCheckBox* rgbCaptureRawModeCheckBox;
    QComboBox *ledGpioModeCombo;
    QSpinBox *ledPreFrameDelaySpinBox;
    QCheckBox *ledPostCaptureOffCheckBox;
    QLabel *ledGpioStatusLabel;
    // ========== STREAMING ARDUCAM CONTROLS ==========
    ExpandableGroupBox* streamingArducamControlsGroupBox;
    QSpinBox* arducamStreamingExposureSpinBox;
    QCheckBox* arducamStreamingAutoExposureCheckBox;
    QCheckBox* arducamStreamingAutoFocusCheckBox;
    QDoubleSpinBox* arducamStreamingLensPositionSpinBox;
    QDoubleSpinBox* arducamStreamingEvCompensationSpinBox;
    QDoubleSpinBox* arducamStreamingAnalogGainSpinBox;
    QCheckBox* arducamStreamingAutoWhiteBalanceCheckBox;
    QDoubleSpinBox* arducamStreamingRedGainSpinBox;
    QDoubleSpinBox* arducamStreamingBlueGainSpinBox;
    QCheckBox* arducamCaptureRawModeCheckBox;
    // ========== SINGLE CAPTURE ARDUCAM CONTROLS ==========
    ExpandableGroupBox* singleCaptureArducamControlsGroupBox;
    QSpinBox* arducamCaptureExposureSpinBox;
    QLabel* arducamCaptureExposureHintLabel;
    QCheckBox* arducamCaptureAutoExposureCheckBox;
    QDoubleSpinBox* arducamCaptureEvCompensationSpinBox;
    QDoubleSpinBox* arducamCaptureAnalogGainSpinBox;
    QCheckBox* arducamCaptureAutoWhiteBalanceCheckBox;
    QDoubleSpinBox* arducamCaptureRedGainSpinBox;
    QDoubleSpinBox* arducamCaptureBlueGainSpinBox;
    QCheckBox*        arducamCaptureAutoFocusCheckBox;
    QDoubleSpinBox*   arducamCaptureLensPositionSpinBox;

    // ========== STREAMING THERMAL CONTROLS ==========
    ExpandableGroupBox* streamingThermalControlsGroupBox;
    QDoubleSpinBox* thermalStreamingEmissivitySpinBox;
    QDoubleSpinBox* thermalStreamingMinTempSpinBox;
    QDoubleSpinBox* thermalStreamingMaxTempSpinBox;
    QComboBox* thermalStreamingColormapCombo;
    QCheckBox* thermalStreamingNucEnabledCheckBox;
    QCheckBox* thermalStreamingAlarmEnabledCheckBox;
    QDoubleSpinBox* thermalStreamingAlarmTempSpinBox;

    // ========== SINGLE CAPTURE THERMAL CONTROLS ==========
    ExpandableGroupBox* singleCaptureThermalControlsGroupBox;
    QDoubleSpinBox* thermalCaptureEmissivitySpinBox;
    QDoubleSpinBox* thermalCaptureMinTempSpinBox;
    QDoubleSpinBox* thermalCaptureMaxTempSpinBox;
    QComboBox* thermalCaptureColormapCombo;
    QCheckBox* thermalCaptureNucEnabledCheckBox;
    QCheckBox* thermalCaptureAlarmEnabledCheckBox;
    QDoubleSpinBox* thermalCaptureAlarmTempSpinBox;

    // Depth controls
    QSpinBox* depthTimingBudgetSpinBox;
    QSpinBox* depthInterMeasurementPeriodSpinBox;
    QSpinBox* depthSigmaThresholdSpinBox;
    QSpinBox* depthSignalThresholdSpinBox;
    QCheckBox* depthMedianFilterEnabledCheckBox;
    QSpinBox* depthMedianFilterSamplesSpinBox;
    
    // UV sensor controls
    QPushButton* uvInitButton;
    QPushButton* uvShutdownButton;
    QPushButton* uvReadButton;
    QLabel* uvStatusLabel;
    QLabel* uvADisplayLabel;
    QLabel* uvBDisplayLabel;
    QLabel* uvCDisplayLabel;
    QLabel* uvTempDisplayLabel;
    QLabel* uvTimestampLabel;
    QComboBox* uvGainCombo;
    QComboBox* uvIntegrationTimeCombo;
    QComboBox* uvModeCombo;
    CalibrationViewerDialog* calibrationViewerDialog = nullptr;
    bool uvInitialized;

    // ALS sensor controls
    QPushButton* alsInitButton;
    QPushButton* alsShutdownButton;
    QPushButton* alsReadButton;
    QLabel* alsStatusLabel;
    QLabel* alsRedLabel;
    QLabel* alsGreenLabel;
    QLabel* alsBlueLabel;
    QLabel* alsClearLabel;
    QLabel* alsIRLabel;
    QLabel* alsVisibleLabel;
    QLabel* alsLuxLabel;
    QLabel* alsCCTLabel;
    QLabel* alsTimestampLabel;
    QComboBox* alsGainCombo;
    QSpinBox* alsExposureSpinBox;
    bool alsInitialized;

    // IMU (LSM6DS3TR-C)
    QPushButton*           imuInitButton       = nullptr;
    QPushButton*           imuStartButton      = nullptr;
    QPushButton*           imuStopButton       = nullptr;
    QPushButton*           imuConfigureButton  = nullptr;
    QLabel*                imuStatusLabel      = nullptr;
    QLabel*                imuAccelLabel       = nullptr;
    QLabel*                imuGyroLabel        = nullptr;
    QLabel*                imuRateLabel        = nullptr;
    QPlainTextEdit*        imuEventLog         = nullptr;
    bool                   imuInitialized      = false;
    bool                   imuStreaming        = false;



    ExpandableGroupBox* sensorTimingGroupBox;
    QLabel* imx708TimingLabel;
    QLabel* imx219TimingLabel;
    QLabel* imx708TimingDetailLabel;
    QLabel* imx219TimingDetailLabel;
    // State
    bool shuttingDown = false;
    ServerConnection* serverConnection;
    QSettings* settings;

    bool streamingActive;
    bool isDualStreaming = false;
    QTimer* streamFpsTimer;
    int streamFrameCount;

    // ---- Motion-indicator state ----
    // Hysteresis between Still and Moving prevents the badge from flickering
    // when motion is right at threshold. Tunables come from QSettings so
    // they can be adjusted without a client release. Defaults: enter Moving
    // at 1.5 px translation, exit at 0.5 px, confidence floor 0.05 to
    // filter flat / occluded scenes.
    enum class MotionUiState { Unknown, Still, Moving };
    QTimer*        motionDecayTimer_  = nullptr;   // clears stale display
    MotionUiState  motionUiState_     = MotionUiState::Unknown;
    double         motionEnterMoving_ = 1.5;
    double         motionExitMoving_  = 0.5;
    double         motionConfFloor_   = 0.05;
    int            motionDecayMs_     = 1000;

    QTimer* distanceStreamTimer;
    bool distanceStreaming;
    bool distanceInitialized;
    int  lastDistanceMm      = 0;
    int  lastDistanceSignal  = 0;
    int  lastDistanceStatus  = 0;
    bool lastDistanceValid   = false;
    QMap<QString, bool> cameraRotation;
    ImageViewerWindow* imageViewerWindow;
    QPixmap lastPixmap;
    QImage lastImage;
    QString lastImageInfo;
    StreamFrameDecoder *rgbDecoder = nullptr;
    StreamFrameDecoder *thermalDecoder = nullptr;
    QImage lastThermalDecoded;
    CameraSettingsManager* settingsManager = nullptr;

    // Focus sweep state
    QTimer*  focusSweepTimer   = nullptr;
    double   focusSweepPos     = 0.0;
    double   focusSweepStep    = 0.5;
    double   focusSweepMax     = 10.0;
    bool     focusSweepWasAuto = false;
    bool sweepSaveImages = false;
    double pendingSweepSaveLensPos = 0.0;
    bool pendingSweepSave = false;
    int  sweepFrameCounter = 0;
    int  sweepSettleFrames = 10;
    QString  focusSweepSaveFolder;

    // Per-panel sweep widgets — RGB streaming
    QPushButton*  rgbStreamingSweepButton  = nullptr;
    QLabel*       rgbStreamingSweepLabel   = nullptr;
    QProgressBar* rgbStreamingSweepBar     = nullptr;

    // Per-panel sweep widgets — RGB capture


    // Per-panel sweep widgets — Arducam streaming
    QPushButton*  arducamStreamingSweepButton = nullptr;
    QLabel*       arducamStreamingSweepLabel  = nullptr;
    QProgressBar* arducamStreamingSweepBar    = nullptr;

    // Per-panel sweep widgets — Arducam capture


    // Shared save-folder widgets (one set, shown in each focus group)
    QCheckBox*   focusSweepSaveCheckBox   = nullptr;
    QPushButton* focusSweepFolderButton   = nullptr;
    QLabel*      focusSweepFolderLabel    = nullptr;

    // Which panel's params to use during the active sweep
    CameraParam     sweepAutoFocusParam    = CameraParam::RGB_STREAMING_AUTO_FOCUS;
    CameraParam     sweepLensPositionParam = CameraParam::RGB_STREAMING_LENS_POSITION;
    QDoubleSpinBox* sweepLensSpinBox       = nullptr;
    QPushButton*    sweepActiveButton      = nullptr;
    QLabel*         sweepActiveLabel       = nullptr;
    QProgressBar*   sweepActiveBar         = nullptr;
    QString         sweepActiveCamera;     // "imx708" or "imx219"

    QAction*               lensCalibAction    = nullptr;
    LensCalibrationDialog* lensCalibDialog    = nullptr;
    int64_t naturalFrameDurationMin_us = 0;
    int64_t naturalFrameDurationMax_us = 0;
    bool versionChecked = false;

    // ---- UVBF ----                                               
    UVBFCaptureDialog* uvbfDialog = nullptr;   
    QPointer<UVBFVBlankDialog> vblankDialog;       
    
    // Last-applied configure-dialog payload (sent on every imu_start so a
    // fresh session uses the user's last-saved settings; persists across
    // reconnects via the dialog's QSettings).
    QJsonObject            imuConfigCache;

        // Scale factors received with each batch — kept around so the live
    // readout can convert raw counts even if a batch arrives without them.
    double                 imuAccelLsbToG  = 0.000122;
    double                 imuGyroLsbToDps = 0.0175;

    // Plot ring-buffer head; reset on disconnect.

    ImuConfigDialog*       imuConfigDialog = nullptr;

};

#endif // MAINWINDOW_H