#pragma once

// ============================================================================
// UVBFVBlankDialog.h
//
// Dialog for the UVBF_VBLANK_CAPTURE timing experiment.
//
// Flow:
//   Page 0 (Config)   — camera, exposure, gain, LED selection → Start button
//   Page 1 (Progress) — capture + transfer progress bars
//   Page 2 (Results)  — per-frame timing table from uvbf_vblank_complete
//
// ============================================================================


#include <QDialog>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include "raw_bayer_decoding.h"   // sanuwave::RawImageInfo
#include "uvbf_frame_info.h"
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class ServerConnection;
class ZoomImageWidget;
 
// ---------------------------------------------------------------------------
// Per-frame timing record populated from uvbf_vblank_complete
// ---------------------------------------------------------------------------
struct VBlankFrameTiming
{
    QString  role;
    bool     ledsOn           = false;
    qint64   sensorTs_ns      = 0;
    double   callbackDelta_us = 0.0;
    double   frameDur_us      = 0.0;   // libcamera FrameDuration = actual frame period

    // Motion measurement, populated for illum frames at illum-sequence
    // index k >= 2 when the server-side motion check was enabled.
    // motionValid==false means no measurement is available for this row.
    bool   motionValid       = false;
    double prevTransPx       = 0.0;
    double prevConfidence    = 0.0;
    double anchorTransPx     = 0.0;
    double anchorConfidence  = 0.0;
};

// ---------------------------------------------------------------------------
// UVBFVBlankDialog
// ---------------------------------------------------------------------------
class UVBFVBlankDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UVBFVBlankDialog(ServerConnection* connection,
                              const QString&    camera,
                              double            analogGain,
                              const QString&    sessionId,
                              QWidget*          parent = nullptr);
    ~UVBFVBlankDialog() override = default;

public slots:
    void onVBlankStarted(int frameCount, const QStringList& roles);
    void onVBlankFrameCaptured(const QString& role);
    void onFrameTransferComplete(const UVBFFrameInfo& frameInfo,
                                 const QByteArray&   dngData);
    void onVBlankComplete(const QJsonObject& summary);
    void onVBlankError(const QString& reason);

private slots:
    void onStartClicked();
    void onCameraChanged(int index);
    void onCancelClicked();
    void onExportCsv();
    void onSaveCompleteResults();

private:
    void     buildUI();
    QWidget* buildConfigPage();
    QWidget* buildProgressPage();
    QWidget* buildResultsPage();

    void goToPage(int index);
    void updateCameraControls(const QString& cam);
    void setStatus(const QString& text);
    void loadVBlankConfig();
    void saveVBlankConfig();
    void populateTimingTable(const QVector<VBlankFrameTiming>& frames,
                             double vblankEstimate_us,
                             double rollingShutter_us);
    void populateMotionVerdict(const QVector<VBlankFrameTiming>& frames);

    static QString    tempFilePathForRole(const QString& sessionId,
                                          const QString& role);
    static bool       writeFrameToTempFile(const QString& path,
                                           const QByteArray& payload);
    static QByteArray readFrameFromTempFile(const QString& path);

    // ── Connection / session ─────────────────────────────────────────────────
    ServerConnection* connection        = nullptr;
    QString           sessionId;
    QString           capturedCamera;
    int               expectedFrameCount = 0;
    QStringList       frameRoles;
    int               framesCaptured    = 0;
    int               framesReceived    = 0;
    QMap<QString, QString> tempFilePaths;

    // ── Config page ──────────────────────────────────────────────────────────
    QComboBox*      cameraCombo        = nullptr;
    QComboBox*      resolutionCombo    = nullptr;
    QSpinBox*       exposureSpinBox    = nullptr;
    QDoubleSpinBox* analogGainSpinBox  = nullptr;
    QDoubleSpinBox* digitalGainSpinBox = nullptr;
    QLabel*         digitalGainNote    = nullptr;
    QSpinBox*       brightnessSpinBox  = nullptr;
    QCheckBox*      predictVBlankCheck = nullptr;
    QCheckBox*      kernelStrobeCheck  = nullptr;
    QCheckBox*      motionCheckCheck   = nullptr;   // server-side motion measurement opt-in
    QCheckBox*      ledCheckBoxes[32]  = {};
    QPushButton*    startButton        = nullptr;

    // ── Progress page ────────────────────────────────────────────────────────
    QLabel*       statusLabel        = nullptr;
    QProgressBar* captureBar         = nullptr;
    QLabel*       captureCountLabel  = nullptr;
    QProgressBar* transferBar        = nullptr;
    QLabel*       transferCountLabel = nullptr;

    // ── Results page ─────────────────────────────────────────────────────────
    QTableWidget*       timingTable     = nullptr;
    QLabel*             summaryLabel    = nullptr;
    QLabel*             motionVerdictLabel = nullptr;   // PASS / FAIL banner
    QPushButton*        exportCsvButton = nullptr;
    QPushButton*        saveAllButton   = nullptr;
    class TimingChart*  timingChart     = nullptr;
    QTabWidget*         previewTabs     = nullptr;
    QMap<QString, QPushButton*> frameSaveDngButtons;
    QMap<QString, QPushButton*> frameSavePngButtons;
    // Per-role preview viewers and frame metadata (populated as frames arrive)
    QMap<QString, ZoomImageWidget*>       frameViewers;
    QMap<QString, sanuwave::RawImageInfo> frameInfos;
    int                                   pendingPreviews = 0;

    // Per-role motion data accumulated from frame_transfer headers as
    // illum frames arrive. Folded into VBlankFrameTiming rows when
    // populateTimingTable is called from onVBlankComplete.
    QMap<QString, UVBFFrameInfo::Motion> frameMotions;

    // Whether the operator enabled motion check for this capture. Set
    // when sending the command; consumed by populateMotionVerdict to
    // distinguish "user did not ask" (hide banner) from "user asked but
    // server failed to measure" (show 'not available').
    bool motionCheckRequested = false;

    // Stored for CSV export after table is populated
    QVector<VBlankFrameTiming> capturedFrames;
    double capturedVBlankEstimate_us = 0.0;
    double capturedRollingShutter_us = 0.0;
    int    capturedCommandedExposure_us = 0;
    double capturedCommandedAnalogGain  = 1.0;
    double  capturedFlashTimeout_ms = 0.0;
    int64_t capturedLastStrobeOn_ms = 0;
    bool    capturedTimeoutExceeded = false;
    bool capturedPredictVBlank = false;
    bool capturedKernelStrobe = false;
    // ── Shared ───────────────────────────────────────────────────────────────
    QStackedWidget* stack           = nullptr;
    QPushButton*    cancelButton    = nullptr;
    bool            captureComplete = false;
};
