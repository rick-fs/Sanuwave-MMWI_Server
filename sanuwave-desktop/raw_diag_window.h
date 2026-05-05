// raw_diagnostic_window.h
#ifndef RAW_DIAGNOSTIC_WINDOW_H
#define RAW_DIAGNOSTIC_WINDOW_H

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QSplitter>
#include <QGroupBox>
#include <QScrollArea>
#include <QTableWidget>
#include <QSet>
#include <QJsonObject>
#include <vector>

#include "protocol_constants.h"
#include "diag_raw_frame.h"

class BayerImageWidget;
class ChannelHistogramWidget;
class ServerConnection;

class RawDiagnosticWindow : public QDialog
{
    Q_OBJECT

public:
    explicit RawDiagnosticWindow(ServerConnection& connection,
                                  QWidget* parent = nullptr);
    ~RawDiagnosticWindow();

private slots:
    void onCaptureClicked();
    void onDarkTestClicked();
    void onDiffTestClicked();
    void onSaveDngClicked();
    void onRawFrameReceived(const sanuwave::protocol::DiagRawFrame& frame);
    void onDiagError(const QString& error);
    void onCameraChanged(int index);
    void onPixelHovered(int x, int y, uint16_t rawValue, int channel);
    void onViewModeChanged(bool demosaic);

    void onLedStatusReceived(const QJsonObject& response);

private:
    void setupUI();
    QWidget* createControlPanel();
    QWidget* createVisualizationPanel();
    QJsonObject buildDiagParams() const;
    void updateCaptureEnabled();
    void     updateMetadataDisplay(const sanuwave::protocol::DiagRawFrame& frame);
    void     updateHistograms(const sanuwave::protocol::DiagRawFrame& frame);
    void     appendLog(const QString& text);

    // LED helpers
    void requestLedStatus();
    void populateLedTable(const QJsonArray& availableIds);
    void sendLedSelect();
    void sendLedDeselect();
    bool hasLedSelection() const;

    // Connection
    ServerConnection& connection;

    // --- Control panel widgets ---
    QComboBox*      cameraCombo;
    QSpinBox*       exposureSpin;
    QDoubleSpinBox* gainSpin;
    QCheckBox*      disableAWB;
    QCheckBox*      disableAE;
    QCheckBox*      disableDenoise;
    QSpinBox*       frameCountSpin;

    // Buttons
    QPushButton*    captureBtn;
    QPushButton*    darkTestBtn;
    QPushButton*    diffTestBtn;
    QPushButton*    saveDngBtn;

    // --- LED Illumination panel ---
    QTableWidget*   ledTable;           // columns: Enable, LED ID, Brightness
    QLabel*         ledStatusLabel;

    // LED state
    QSet<int>       availableLedIds;
    bool            ledStatusReceived = false;

    // --- Metadata panel ---
    QLabel*         metaExposure;
    QLabel*         metaGain;
    QLabel*         metaDigitalGain;
    QLabel*         metaBlackLevel;
    QLabel*         metaAWBGains;
    QLabel*         metaFormat;
    QLabel*         metaBits;
    QLabel*         metaColorTemp;

    // --- Visualization ---
    BayerImageWidget*       bayerView;
    ChannelHistogramWidget* histR;
    ChannelHistogramWidget* histGr;
    ChannelHistogramWidget* histGb;
    ChannelHistogramWidget* histB;
    QLabel*                 pixelInfoLabel;
    QCheckBox*              demosaicToggle;

    // --- Analysis log ---
    QTextEdit*      analysisLog;

    // --- State ---
    sanuwave::protocol::DiagRawFrame lastCapturedFrame;
    std::vector<sanuwave::protocol::DiagRawFrame> capturedFrames;
    bool darkTestRunning  = false;
    bool diffTestRunning  = false;
    int  diffFramesTarget = 0;
};

#endif // RAW_DIAGNOSTIC_WINDOW_H
