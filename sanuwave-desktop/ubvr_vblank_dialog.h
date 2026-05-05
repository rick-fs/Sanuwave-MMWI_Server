#pragma once

// ============================================================================
// UVBFVBlankDialog.h
//
// Dialog for the UVBF_VBLANK_CAPTURE timing experiment.
//
// Purpose: send a uvbf_vblank_capture command, receive N frames (dynamic,
// driven by the server's uvbf_started message), display a per-frame timing
// table from uvbf_vblank_complete so we can evaluate whether requestCompleted
// fires inside the VBlank interval.
//
// Frame buffering reuses the UVBFCaptureDialog "poor man's virtual memory"
// pattern: each raw payload is written to a temp file immediately and the
// QByteArray allocation freed, keeping peak RAM to one frame at a time.
//
// IEC 62304: this file is part of the SanuwaveClient software item.
// ============================================================================

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QVBoxLayout;
class ServerConnection;

// ---------------------------------------------------------------------------
// Per-frame timing record — populated from uvbf_vblank_complete
// ---------------------------------------------------------------------------
struct VBlankFrameTiming
{
    QString  role;
    bool     ledsOn          = false;
    qint64   sensorTs_ns     = 0;
    double   callbackDelta_us = 0.0;
    double   framePeriod_us  = 0.0;
    double   frameDur_us     = 0.0;
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

    // -----------------------------------------------------------------------
    // Slots wired by MainWindow (same pattern as UVBFCaptureDialog)
    // -----------------------------------------------------------------------
public slots:
    // uvbf_started — tells us frame_count and roles before any data arrives
    void onVBlankStarted(int frameCount, const QStringList& roles);

    // uvbf_frame_captured — lightweight progress tick (no payload yet)
    void onVBlankFrameCaptured(const QString& role);

    // Called by ServerConnection when a FRAME_TRANSFER binary payload is
    // complete.  Reuses the same uvbfFrameTransferComplete signal path that
    // UVBFCaptureDialog uses — the dialog is responsible for deciding whether
    // the role belongs to this session.
    void onFrameTransferComplete(const QString&    role,
                                 const QString&    sessionId,
                                 QByteArray        payload,   // by value — we own it
                                 const QJsonObject& meta);

    // uvbf_vblank_complete — timing summary
    void onVBlankComplete(const QJsonObject& summary);

    // Error / disconnect path
    void onVBlankError(const QString& reason);

private slots:
    void onCancelClicked();

private:
    // -----------------------------------------------------------------------
    // UI helpers
    // -----------------------------------------------------------------------
    void buildUI();
    void buildTimingTable();
    void populateTimingTable(const QVector<VBlankFrameTiming>& frames,
                             double                             vblankEstimate_us,
                             double                             rollingShutter_us);
    void setStatus(const QString& text);
    void updateFrameProgress(const QString& role, int received, int total);

    // -----------------------------------------------------------------------
    // Temp file helpers (same pattern as UVBFCaptureDialog)
    // -----------------------------------------------------------------------
    static QString  tempFilePathForRole(const QString& sessionId,
                                        const QString& role);
    static bool     writeFrameToTempFile(const QString& path,
                                         const QByteArray& payload);

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------
    ServerConnection* connection   = nullptr;
    QString           camera;
    double            analogGain   = 1.0;
    QString           sessionId;

    // Dynamic frame list (set from uvbf_started)
    int         expectedFrameCount = 0;
    QStringList frameRoles;
    int         framesReceived     = 0;
    int         framesCaptured     = 0;

    // Temp file paths keyed by role
    QMap<QString, QString> tempFilePaths;

    // -----------------------------------------------------------------------
    // UI widgets
    // -----------------------------------------------------------------------
    QLabel*       statusLabel       = nullptr;
    QLabel*       captureCountLabel = nullptr;
    QLabel*       transferCountLabel= nullptr;
    QProgressBar* captureBar        = nullptr;   // capture progress (uvbf_frame_captured)
    QProgressBar* transferBar       = nullptr;   // transfer progress (frame_transfer)
    QTableWidget* timingTable       = nullptr;
    QLabel*       summaryLabel      = nullptr;
    QPushButton*  cancelButton      = nullptr;

    // -----------------------------------------------------------------------
    // State flags
    // -----------------------------------------------------------------------
    bool captureStarted  = false;
    bool captureComplete = false;
};
