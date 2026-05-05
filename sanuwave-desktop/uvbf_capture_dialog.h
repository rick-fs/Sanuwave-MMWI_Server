// client/src/uvbf_capture_dialog.h
// UVBF four-frame burst capture dialog.
// Left wizard panel + right image panel (maximised to current screen).
// Copyright 2026 Sanuwave Medical LLC.
#pragma once

#include <QDialog>
#include <QStackedWidget>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QTimer>
#include <QPixmap>
#include <QByteArray>
#include <QWheelEvent>
#include <QTabWidget>
#include <QVBoxLayout>
#include <array>
#include "raw_bayer_decoding.h"
#include "sensor_filter.h"
#include "server_connection.h"
class ServerConnection;

// ============================================================================
// ZoomImageWidget — scroll-area backed zoomable image label
// ============================================================================
class ZoomImageWidget : public QScrollArea
{
    Q_OBJECT
public:
    explicit ZoomImageWidget(QWidget* parent = nullptr);
    void setPixmap(const QPixmap& px);
    void clearImage();

protected:
    void wheelEvent(QWheelEvent* e) override;

private:
    QLabel*  imageLabel = nullptr;
    QPixmap  source;
    double   zoomFactor = 1.0;

    void applyZoom();
};

// ============================================================================
// Readiness check state
// ============================================================================
struct ReadinessCheck {
    bool passing    = false;
    bool overridden = false;
    bool satisfied() const { return passing || overridden; }
};

// ============================================================================
// UVBF capture configuration (persisted client-side)
// ============================================================================
struct UVBFConfig {
    QString camera          = "imx219";
    int     exposureUs      = 100000;
    double  analogGain      = 1.0;
    double  digitalGain     = 1.0;        // IMX708 only
    int     ledBrightness   = 200;        // 0-255
    std::array<bool,32> ledIds = {};
};

// ============================================================================
// Session data accumulated across wizard steps
// ============================================================================
struct UVBFSession {
    QString    sessionId;

    // Sensor snapshot at capture time
    float      distanceMm  = 0.f;
    float      ambientUv   = 0.f;
    float      motionScore = 0.f;

    // Capture config used
    UVBFConfig config;

    // Frame payloads — always empty after the temp-file refactor.
    // Kept for ABI compatibility; consumers should use the tempFile paths instead.
    QByteArray dark1Dng;
    QByteArray illum1Dng;
    QByteArray illum2Dng;
    QByteArray illum3Dng;
    QByteArray dark2Dng;

    // Payload sizes (bytes) — the only thing logged by onUVBFCaptureAccepted.
    qint64 dark1Size  = 0;
    qint64 illum1Size = 0;
    qint64 illum2Size = 0;
    qint64 illum3Size = 0;
    qint64 dark2Size  = 0;

    // Temp file paths — written as each frame arrives, valid until overwritten
    // by the next capture cycle.
    QString dark1TempFile;
    QString illum1TempFile;
    QString illum2TempFile;
    QString illum3TempFile;
    QString dark2TempFile;

    // Preview images (subsampled, for display only)
    QPixmap    dark1Preview;
    QPixmap    illum1Preview;
    QPixmap    illum2Preview;
    QPixmap    illum3Preview;
    QPixmap    dark2Preview;

    // Image info
    sanuwave::RawImageInfo dark1Info;
    sanuwave::RawImageInfo illum1Info;
    sanuwave::RawImageInfo illum2Info;
    sanuwave::RawImageInfo illum3Info;
    sanuwave::RawImageInfo dark2Info;

    // Timestamps
    uint64_t dark1Timestamp_ms  = 0;
    uint64_t illum1Timestamp_ms = 0;
    uint64_t illum2Timestamp_ms = 0;
    uint64_t illum3Timestamp_ms = 0;
    uint64_t dark2Timestamp_ms  = 0;

    // LED on/off timestamps per illuminated frame
    uint64_t ledOnTimestamp1_ms  = 0;
    uint64_t ledOffTimestamp1_ms = 0;
    uint64_t ledOnTimestamp2_ms  = 0;
    uint64_t ledOffTimestamp2_ms = 0;
    uint64_t ledOnTimestamp3_ms  = 0;
    uint64_t ledOffTimestamp3_ms = 0;
};

// ============================================================================
// UVBFCaptureDialog
// ============================================================================
class UVBFCaptureDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UVBFCaptureDialog(ServerConnection*  client,
                               bool               distanceAlreadyActive,
                               QWidget*           parent = nullptr);
    ~UVBFCaptureDialog() override;

    // Called by MainWindow when sensor status arrives
    void onSensorStatus(float distanceMm, float ambientUv, float motionScore);

    // Called by MainWindow when ALS data arrives
    void onALSData(uint32_t clear, float exposureMs);

    // Called when UVBF_STARTED arrives
    void onUVBFStarted();

    // Called when UVBF_FRAME_CAPTURED arrives
    void onUVBFFrameCaptured(const QString& role);

    // Called when UVBF_ERROR arrives
    void onUVBFError(const QString& stage, const QString& reason);
    void onServerError(const QString& message);

    // Called as frame transfer data arrives
    void onFrameTransferProgress(const QString& role, int bytesReceived, int totalBytes);

    // CHANGED: QByteArray by value to match signal signature and support
    // Qt::QueuedConnection (Qt copies signal args into the event queue by value).
    void onFrameTransferComplete(const UVBFFrameInfo& frameInfo,
                                 QByteArray rawPayload);

    void onServerDisconnected();
    void onTransferTimeout();

    // Legacy ack — kept for MainWindow compatibility
    void onCaptureModeAck(bool success, const QString& error = {});

signals:
    void captureAccepted(const UVBFSession& session);

private slots:
    void onNextClicked();
    void onCancelClicked();
    void onRetakeClicked();
    void onAcceptAndCloseClicked();
    void onSaveConfigClicked();

    void onDistanceOverrideToggled(bool checked);
    void onAmbientOverrideToggled(bool checked);
    void onMotionOverrideToggled(bool checked);
    void onMedianFilterToggled(bool checked);

    void onCameraChanged(int index);
    void pollSensorStatus();

private:
    // ── Step indices ─────────────────────────────────────────────────────────
    enum Step {
        StepReadiness  = 0,
        StepConfig     = 1,
        StepCapture    = 2,
        StepTransfer   = 3,
        StepProcessing = 4,
        StepResult     = 5,
    };

    // ── UI construction ──────────────────────────────────────────────────────
    QWidget* buildLeftPanel();
    QWidget* buildRightPanel();

    void     buildStepIndicator(QVBoxLayout* parent);
    QWidget* buildReadinessPage();
    QWidget* buildConfigPage();
    QWidget* buildCapturePage();
    QWidget* buildTransferPage();
    QWidget* buildProcessingPage();
    QWidget* buildResultPage();
    void     buildButtonRow(QVBoxLayout* parent);

    QWidget* buildRightReadiness();
    QWidget* buildRightConfig();
    QWidget* buildRightFrames();
    QWidget* buildRightResult();

    // ── Navigation ───────────────────────────────────────────────────────────
    void goToStep(Step step);
    void updateButtonState();
    void updateStepIndicator(Step step);
    void syncRightPanel(Step step);

    // ── Readiness helpers ────────────────────────────────────────────────────
    void refreshReadinessRow(QLabel* indicator, QLabel* valueLabel,
                             const ReadinessCheck& check,
                             const QString& valueText);
    void evaluateReadiness();

    // ── Config helpers ───────────────────────────────────────────────────────
    void       updateCameraControls(const QString& camera);
    UVBFConfig readConfig() const;
    void       applyConfig(const UVBFConfig& cfg);
    void       loadConfig();
    void       saveConfig();

    // ── Capture helpers ──────────────────────────────────────────────────────
    void beginCapture();
    void setCaptureStatus(const QString& text);

    // ── Transfer helpers ─────────────────────────────────────────────────────
    void setTransferStatus(const QString& text);

    // Returns the fixed temp-file path for a given frame role.
    static QString tempFilePathForRole(const QString& role);

    // Writes payload to the temp file for role, returns true on success.
    static bool writeFrameToTempFile(const QString& path, const QByteArray& payload);

    // Reads the temp file at path into a QByteArray. Returns empty on failure.
    static QByteArray readFrameFromTempFile(const QString& path);

    #ifdef DELETEME
    // ── Processing helpers ───────────────────────────────────────────────────
    void beginProcessing();
    void setProcessingStage(const QString& stage);
    void finishProcessing();
    #endif

    // ── Distance filter ──────────────────────────────────────────────────────
    float applyDistanceFilter(float rawMm);

    // ── Result helpers ───────────────────────────────────────────────────────
    void populateResultTabs();

    // ── Core ─────────────────────────────────────────────────────────────────
    ServerConnection* client            = nullptr;
    bool              distanceWasActive = false;
    UVBFSession       session;

    // ── Step machinery ───────────────────────────────────────────────────────
    QStackedWidget* leftStack     = nullptr;
    QStackedWidget* rightStack    = nullptr;
    QWidget*        stepIndicator = nullptr;
    QList<QLabel*>  stepLabels;
    QPushButton*    btnNext       = nullptr;
    QPushButton*    btnCancel     = nullptr;
    QTimer*         pollTimer     = nullptr;

    // ── Step 1: Readiness ────────────────────────────────────────────────────
    ReadinessCheck checkDistance;
    ReadinessCheck checkAmbient;
    ReadinessCheck checkMotion;

    QLabel*    distanceIndicator = nullptr;
    QLabel*    distanceValue     = nullptr;
    QCheckBox* distanceOverride  = nullptr;
    QLabel*    ambientIndicator  = nullptr;
    QLabel*    ambientValue      = nullptr;
    QCheckBox* ambientOverride   = nullptr;
    QLabel*    motionIndicator   = nullptr;
    QLabel*    motionValue       = nullptr;
    QCheckBox* motionOverride    = nullptr;
    QCheckBox* medianFilterCheck = nullptr;

    // ── Distance filter ──────────────────────────────────────────────────────
    static constexpr int   kDistanceFilterDepth  = 7;
    static constexpr float kDistanceResetDeltaMm = 25.0f;
    Sanuwave::SensorFilter<float> distanceFilter{kDistanceFilterDepth};
    bool                          useMedianFilter = true;

    // ── Step 2: Config ───────────────────────────────────────────────────────
    QComboBox*      cameraCombo           = nullptr;
    QLabel*         resolutionLabel       = nullptr;
    QSpinBox*       exposureSpinBox       = nullptr;
    QLabel*         alsExposureHintLabel  = nullptr;  // "Setting exposure to X µs"
    QDoubleSpinBox* analogGainSpinBox     = nullptr;
    QDoubleSpinBox* digitalGainSpinBox    = nullptr;
    QLabel*         digitalGainNote       = nullptr;
    QSpinBox*       brightnessSpinBox     = nullptr;
    std::array<QCheckBox*, 32> ledCheckBoxes = {};

    // ── ALS state ────────────────────────────────────────────────────────────
    uint32_t lastAlsClear  = 0;     // most recent clear channel count
    float    alsExposureMs = 0.0f;  // ALS exposure used for the reading

    // ── Step 3: Capture ──────────────────────────────────────────────────────
    QLabel* captureStatus = nullptr;
    QLabel* captureFrame1 = nullptr;   // Dark 1
    QLabel* captureFrame2 = nullptr;   // Illuminated 1
    QLabel* captureFrame3 = nullptr;   // Illuminated 2
    QLabel* captureFrame4 = nullptr;   // Illuminated 3
    QLabel* captureFrame5 = nullptr;   // Dark 2

    // ── Step 4: Transfer ─────────────────────────────────────────────────────
    QLabel*       transferStatus  = nullptr;
    QProgressBar* dark1Progress   = nullptr;
    QProgressBar* illum1Progress  = nullptr;
    QProgressBar* illum2Progress  = nullptr;
    QProgressBar* illum3Progress  = nullptr;
    QProgressBar* dark2Progress   = nullptr;
    bool          dark1Received   = false;
    bool          illum1Received  = false;
    bool          illum2Received  = false;
    bool          illum3Received  = false;
    bool          dark2Received   = false;
    int           pendingPreviews = 0;

    // ── Step 5: Processing ───────────────────────────────────────────────────
    QLabel* processingStage = nullptr;

    // ── Step 6: Result ───────────────────────────────────────────────────────
    QTabWidget*      resultTabs      = nullptr;
    ZoomImageWidget* dark1Viewer     = nullptr;
    ZoomImageWidget* illum1Viewer    = nullptr;
    ZoomImageWidget* illum2Viewer    = nullptr;
    ZoomImageWidget* illum3Viewer    = nullptr;
    ZoomImageWidget* dark2Viewer     = nullptr;
    QLabel*          dark1Timestamp  = nullptr;
    QLabel*          illum1Timestamp = nullptr;
    QLabel*          illum2Timestamp = nullptr;
    QLabel*          illum3Timestamp = nullptr;
    QLabel*          dark2Timestamp  = nullptr;
    QPushButton*     btnRetake       = nullptr;
    QPushButton*     btnSave         = nullptr;
    QTimer*          transferWatchdog = nullptr;

    // ── Right-panel frame viewers (visible during capture/transfer) ──────────
    ZoomImageWidget* rightDark1Viewer     = nullptr;
    ZoomImageWidget* rightIllum1Viewer    = nullptr;
    ZoomImageWidget* rightIllum2Viewer    = nullptr;
    ZoomImageWidget* rightIllum3Viewer    = nullptr;
    ZoomImageWidget* rightDark2Viewer     = nullptr;
    QLabel*          rightDark1Timestamp  = nullptr;
    QLabel*          rightIllum1Timestamp = nullptr;
    QLabel*          rightIllum2Timestamp = nullptr;
    QLabel*          rightIllum3Timestamp = nullptr;
    QLabel*          rightDark2Timestamp  = nullptr;

    // ── Thresholds ───────────────────────────────────────────────────────────
    static constexpr float kDistanceMinMm  = 150.f;
    static constexpr float kDistanceMaxMm  = 300.f;
    static constexpr float kAmbientUvMax   = 0.5f;
    static constexpr float kMotionScoreMax = 0.1f;
};
