// client/src/uvbf_capture_dialog.cpp
// Copyright 2026 Sanuwave Medical LLC.
#include "uvbf_capture_dialog.h"
#include "server_connection.h"
#include "protocol_constants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QSizePolicy>
#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSettings>
#include <QUuid>
#include <QJsonArray>
#include <QScrollBar>
#include <QScreen>
#include <QCursor>
#include <QGuiApplication>
#include <QPixmap>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QtConcurrent/QtConcurrent>
#include <QPainter> 
#include <cmath>
#include "raw_bayer_decoding.h"
#include "dng_exporter.h"
#include "logger.h"
using namespace sanuwave::protocol;

// Transfer watchdog: if no frame arrives within this many ms, abort.
static constexpr int kTransferTimeoutMs = 30'000;

// ============================================================================
// ZoomImageWidget
// ============================================================================
static QBrush makeCheckerboard()
{
    QPixmap tile(16, 16);
    tile.fill(QColor(80, 80, 80));
    QPainter p(&tile);
    p.fillRect(0, 0, 8, 8, QColor(60, 60, 60));
    p.fillRect(8, 8, 8, 8, QColor(60, 60, 60));
    return QBrush(tile);
}

ZoomImageWidget::ZoomImageWidget(QWidget* parent)
    : QScrollArea(parent)
{
    setAlignment(Qt::AlignCenter);
    setWidgetResizable(false);

    viewport()->setAutoFillBackground(true);
    QPalette p = viewport()->palette();
    p.setBrush(QPalette::Window, makeCheckerboard());
    viewport()->setPalette(p);

    imageLabel = new QLabel;
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    imageLabel->setAutoFillBackground(false);
    setWidget(imageLabel);
}

void ZoomImageWidget::setPixmap(const QPixmap& px)
{
    source     = px;
    zoomFactor = 1.0;
    applyZoom();
}

void ZoomImageWidget::clearImage()
{
    source = {};
    imageLabel->clear();
    imageLabel->resize(0, 0);
}

void ZoomImageWidget::wheelEvent(QWheelEvent* e)
{
    if (source.isNull()) { QScrollArea::wheelEvent(e); return; }
    double delta = e->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
    zoomFactor   = std::clamp(zoomFactor * delta, 0.1, 10.0);
    applyZoom();
    e->accept();
}

void ZoomImageWidget::applyZoom()
{
    if (source.isNull()) return;
    int w = static_cast<int>(source.width()  * zoomFactor);
    int h = static_cast<int>(source.height() * zoomFactor);
    imageLabel->setPixmap(source.scaled(w, h, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
    imageLabel->resize(w, h);
}

// ============================================================================
// Construction
// ============================================================================

UVBFCaptureDialog::UVBFCaptureDialog(ServerConnection* client,
                                     bool distanceAlreadyActive,
                                     QWidget* parent)
    : QDialog(parent)
    , client(client)
    , distanceWasActive(distanceAlreadyActive)
{
    setWindowTitle(tr("UV Fluorescence Capture"));
    setModal(true);

    session.sessionId = QUuid::createUuid()
                            .toString(QUuid::WithoutBraces).left(8);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(0);

    auto* left = buildLeftPanel();
    left->setFixedWidth(580);
    root->addWidget(left);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    root->addWidget(buildRightPanel(), 1);

    connect(client, &ServerConnection::uvbfStarted,
        this, &UVBFCaptureDialog::onUVBFStarted);
    connect(client, &ServerConnection::uvbfFrameCaptured,
        this, &UVBFCaptureDialog::onUVBFFrameCaptured);
    connect(client, &ServerConnection::uvbfError,
        this, &UVBFCaptureDialog::onUVBFError);
    connect(client, &ServerConnection::uvbfFrameTransferProgress,
        this, &UVBFCaptureDialog::onFrameTransferProgress);

    // CHANGED: Qt::QueuedConnection so the temp file write in
    // onFrameTransferComplete does not block onReadyRead from draining
    // the socket.  The QByteArray signal arg is copied into the event
    // queue and the 24 MB allocation in onReadyRead is freed immediately,
    // keeping the TCP receive window open for the next frame.
    connect(client, &ServerConnection::uvbfFrameTransferComplete,
        this, &UVBFCaptureDialog::onFrameTransferComplete,
        Qt::QueuedConnection);

    connect(client, &ServerConnection::disconnected,
        this, &UVBFCaptureDialog::onServerDisconnected);
    connect(client, &ServerConnection::serverError,
        this, &UVBFCaptureDialog::onServerError);

    pollTimer = new QTimer(this);
    pollTimer->setInterval(100);
    connect(pollTimer, &QTimer::timeout,
            this, &UVBFCaptureDialog::pollSensorStatus);

    transferWatchdog = new QTimer(this);
    transferWatchdog->setSingleShot(true);
    transferWatchdog->setInterval(kTransferTimeoutMs);
    connect(transferWatchdog, &QTimer::timeout,
            this, &UVBFCaptureDialog::onTransferTimeout);

    loadConfig();
    goToStep(StepReadiness);

    if (!distanceWasActive && client) {
        QJsonObject cmd;
        cmd[Param::COMMAND] = Command::DISTANCE_START;
        client->sendCommand(cmd);
    }

    // ── Screen-aware sizing ───────────────────────────────────────────────────
    // Use the screen the cursor is on, falling back to primary.  Cap at 95% of
    // the available (work-area) geometry so the dialog never overlaps the
    // taskbar or goes off-screen, even with high-DPI scaling or Windows chrome.
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect avail = screen->availableGeometry();
        const int maxW = static_cast<int>(avail.width()  * 0.95);
        const int maxH = static_cast<int>(avail.height() * 0.90);
        resize(qMin(sizeHint().width(),  maxW),
               qMin(sizeHint().height(), maxH));
        // Centre on the work area (not the raw screen rect).
        move(avail.center() - rect().center());
    }
}

UVBFCaptureDialog::~UVBFCaptureDialog()
{
    pollTimer->stop();
    transferWatchdog->stop();
    if (!distanceWasActive && client) {
        QJsonObject cmd;
        cmd[Param::COMMAND] = Command::DISTANCE_STOP;
        client->sendCommand(cmd);
    }
}

// ============================================================================
// Left panel
// ============================================================================

QWidget* UVBFCaptureDialog::buildLeftPanel()
{
    auto* w      = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    buildStepIndicator(layout);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep);

    leftStack = new QStackedWidget;
    leftStack->addWidget(buildReadinessPage());   // 0
    leftStack->addWidget(buildConfigPage());      // 1
    leftStack->addWidget(buildCapturePage());     // 2
    leftStack->addWidget(buildTransferPage());    // 3
    leftStack->addWidget(buildProcessingPage());  // 4
    leftStack->addWidget(buildResultPage());      // 5
    layout->addWidget(leftStack, 1);

    buildButtonRow(layout);
    return w;
}

// ============================================================================
// Right panel
// ============================================================================

QWidget* UVBFCaptureDialog::buildRightPanel()
{
    auto* w      = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(8, 4, 4, 4);
    layout->setSpacing(0);

    rightStack = new QStackedWidget;
    rightStack->addWidget(buildRightReadiness()); // 0
    rightStack->addWidget(buildRightConfig());    // 1
    rightStack->addWidget(buildRightFrames());    // 2
    rightStack->addWidget(buildRightResult());    // 3
    layout->addWidget(rightStack, 1);
    return w;
}

QWidget* UVBFCaptureDialog::buildRightReadiness()
{
    auto* w      = new QWidget;
    auto* layout = new QVBoxLayout(w);
    auto* lbl    = new QLabel(
        tr("Live camera preview will appear here.\n\n"
           "Position the device over the wound site\n"
           "and wait for all readiness checks to pass."));
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("color: #888; font-size: 13px;");
    lbl->setWordWrap(true);
    layout->addStretch();
    layout->addWidget(lbl);
    layout->addStretch();
    return w;
}

QWidget* UVBFCaptureDialog::buildRightConfig()
{
    auto* w      = new QWidget;
    auto* layout = new QVBoxLayout(w);
    auto* lbl    = new QLabel(
        tr("Configure capture settings on the left,\n"
           "then press Capture to begin."));
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("color: #888; font-size: 13px;");
    lbl->setWordWrap(true);
    layout->addStretch();
    layout->addWidget(lbl);
    layout->addStretch();
    return w;
}

QWidget* UVBFCaptureDialog::buildRightFrames()
{
    auto* w      = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* title = new QLabel(tr("<b>Frames</b>"));
    layout->addWidget(title);

    auto* hbox = new QHBoxLayout;
    hbox->setSpacing(4);

    auto makeViewer = [&](const QString& label,
                           QLabel*& tsLabel) -> ZoomImageWidget* {
        auto* container = new QWidget;
        // Each of the five columns gets an equal share; prevent any one viewer
        // from blowing out the dialog width when a pixmap loads.
        container->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        auto* vl        = new QVBoxLayout(container);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(2);
        auto* hdr = new QLabel(label);
        hdr->setStyleSheet("font-weight: bold; font-size: 11px;");
        vl->addWidget(hdr);
        auto* v = new ZoomImageWidget;
        v->setMinimumHeight(200);
        v->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        vl->addWidget(v, 1);
        tsLabel = new QLabel(tr("—"));
        tsLabel->setAlignment(Qt::AlignCenter);
        tsLabel->setStyleSheet("color: #888; font-size: 12px; font-weight: bold;");
        vl->addWidget(tsLabel);
        hbox->addWidget(container, 1);
        return v;
    };

    rightDark1Viewer  = makeViewer(tr("Dark 1"),        rightDark1Timestamp);
    rightIllum1Viewer = makeViewer(tr("Illuminated 1"), rightIllum1Timestamp);
    rightIllum2Viewer = makeViewer(tr("Illuminated 2"), rightIllum2Timestamp);
    rightIllum3Viewer = makeViewer(tr("Illuminated 3"), rightIllum3Timestamp);
    rightDark2Viewer  = makeViewer(tr("Dark 2"),        rightDark2Timestamp);

    layout->addLayout(hbox, 1);

    auto* hint = new QLabel(tr("Mouse wheel to zoom  ·  Images appear as frames arrive"));
    hint->setStyleSheet("color: #888; font-size: 10px;");
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);

    return w;
}

QWidget* UVBFCaptureDialog::buildRightResult()
{
    auto* w      = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    resultTabs = new QTabWidget;

    // makeTab: viewer shows the subsampled preview thumbnail.
    // Save PNG and Save DNG both read from the temp file on demand —
    // only one frame's worth of data in memory at a time.
    auto makeTab = [&](const QString& name,
                       ZoomImageWidget*& viewer,
                       QLabel*& tsOut,
                       QString               UVBFSession::* tempFileMember,
                       sanuwave::RawImageInfo UVBFSession::* infoMember,
                       QPixmap               UVBFSession::* previewMember) {
        auto* container = new QWidget;
        auto* vl        = new QVBoxLayout(container);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(4);

        viewer = new ZoomImageWidget;
        vl->addWidget(viewer, 1);

        tsOut = new QLabel(tr("—"));
        tsOut->setAlignment(Qt::AlignCenter);
        tsOut->setStyleSheet("color: #888; font-size: 12px; font-weight: bold;");
        vl->addWidget(tsOut);

        auto* btnRow     = new QHBoxLayout;
        auto* saveDngBtn = new QPushButton(tr("💾  Save DNG…"));
        saveDngBtn->setMaximumWidth(160);
        auto* savePngBtn = new QPushButton(tr("🖼  Save PNG…"));
        savePngBtn->setMaximumWidth(160);
        btnRow->addStretch();
        btnRow->addWidget(saveDngBtn);
        btnRow->addWidget(savePngBtn);
        vl->addLayout(btnRow);

        // ── Save DNG — reads temp file on demand ──────────────────────────────
        connect(saveDngBtn, &QPushButton::clicked, this,
                [this, name, tempFileMember, infoMember]() {
            const QString& tempPath = session.*tempFileMember;
            if (tempPath.isEmpty()) {
                QMessageBox::warning(this, tr("No Data"),
                    tr("No frame data available for %1.").arg(name));
                return;
            }
            QString savePath = QFileDialog::getSaveFileName(
                this,
                tr("Save %1 DNG").arg(name),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                    + QString("/%1_%2.dng")
                          .arg(session.sessionId)
                          .arg(name.toLower().replace(' ', '_')),
                tr("DNG files (*.dng)"));
            if (savePath.isEmpty()) return;

            QByteArray payload = readFrameFromTempFile(tempPath);
            if (payload.isEmpty()) {
                QMessageBox::critical(this, tr("Read Error"),
                    tr("Failed to read temp file for %1.").arg(name));
                return;
            }

            const sanuwave::RawImageInfo& storedInfo = session.*infoMember;
            size_t headerLen = 0;
            sanuwave::RawImageInfo parsedInfo;
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(payload.constData());
            if (!sanuwave::RawBayerDecoder::parseHeader(bytes, payload.size(),
                                                        parsedInfo, headerLen)) {
                QMessageBox::critical(this, tr("DNG Error"),
                    tr("Failed to parse frame header for %1.").arg(name));
                return;
            }
            if (storedInfo.width > 0) parsedInfo = storedInfo;

            QString camera = session.config.camera;
            sanuwave::RawImageData raw = sanuwave::DngExporter::buildFromCapture(
                bytes + headerLen,
                static_cast<size_t>(payload.size()) - headerLen,
                parsedInfo,
                camera.toStdString());

            QString errMsg;
            if (!sanuwave::DngExporter::writeDng(savePath, raw, errMsg))
                QMessageBox::critical(this, tr("Save Failed"),
                    tr("Failed to write DNG for %1:\n%2").arg(name, errMsg));
        });

        // ── Save PNG — full-resolution decode from temp file on demand ────────
        connect(savePngBtn, &QPushButton::clicked, this,
                [this, name, tempFileMember]() {
            const QString& tempPath = session.*tempFileMember;
            if (tempPath.isEmpty()) {
                QMessageBox::warning(this, tr("No Data"),
                    tr("No frame data available for %1.").arg(name));
                return;
            }

            QString savePath = QFileDialog::getSaveFileName(
                this,
                tr("Save %1 PNG").arg(name),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                    + QString("/%1_%2.png")
                          .arg(session.sessionId)
                          .arg(name.toLower().replace(' ', '_')),
                tr("PNG files (*.png)"));
            if (savePath.isEmpty()) return;

            QByteArray payload = readFrameFromTempFile(tempPath);
            if (payload.isEmpty()) {
                QMessageBox::critical(this, tr("Read Error"),
                    tr("Failed to read temp file for %1.").arg(name));
                return;
            }

            size_t headerLen = 0;
            sanuwave::RawImageInfo info;
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(payload.constData());
            if (!sanuwave::RawBayerDecoder::parseHeader(bytes, payload.size(),
                                                        info, headerLen)) {
                QMessageBox::critical(this, tr("Decode Error"),
                    tr("Failed to parse frame header for %1.").arg(name));
                return;
            }

            std::vector<uint8_t> rgb = sanuwave::RawBayerDecoder::decode(
                bytes + headerLen,
                static_cast<size_t>(payload.size()) - headerLen,
                info);

            if (rgb.empty()) {
                QMessageBox::critical(this, tr("Decode Error"),
                    tr("Failed to decode raw frame for %1.").arg(name));
                return;
            }

            // .copy() transfers ownership before rgb destructs
            QImage img = QImage(rgb.data(), info.width, info.height,
                                info.width * 3, QImage::Format_RGB888).copy();

            if (!img.save(savePath, "PNG"))
                QMessageBox::warning(this, tr("Save Failed"),
                    tr("Failed to save PNG to:\n%1").arg(savePath));
        });

        resultTabs->addTab(container, name);
        Q_UNUSED(previewMember)
    };

    makeTab(tr("Dark 1"),        dark1Viewer,  dark1Timestamp,
            &UVBFSession::dark1TempFile,  &UVBFSession::dark1Info,  &UVBFSession::dark1Preview);
    makeTab(tr("Illuminated 1"), illum1Viewer, illum1Timestamp,
            &UVBFSession::illum1TempFile, &UVBFSession::illum1Info, &UVBFSession::illum1Preview);
    makeTab(tr("Illuminated 2"), illum2Viewer, illum2Timestamp,
            &UVBFSession::illum2TempFile, &UVBFSession::illum2Info, &UVBFSession::illum2Preview);
    makeTab(tr("Illuminated 3"), illum3Viewer, illum3Timestamp,
            &UVBFSession::illum3TempFile, &UVBFSession::illum3Info, &UVBFSession::illum3Preview);
    makeTab(tr("Dark 2"),        dark2Viewer,  dark2Timestamp,
            &UVBFSession::dark2TempFile,  &UVBFSession::dark2Info,  &UVBFSession::dark2Preview);

    layout->addWidget(resultTabs, 1);

    auto* hint = new QLabel(tr("Mouse wheel to zoom  ·  Scroll to pan"));
    hint->setStyleSheet("color: #888; font-size: 10px;");
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);

    return w;
}

// ============================================================================
// Step indicator
// ============================================================================

void UVBFCaptureDialog::buildStepIndicator(QVBoxLayout* parent)
{
    stepIndicator = new QWidget;
    auto* layout  = new QHBoxLayout(stepIndicator);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(2);

    const QStringList names = {
        tr("1.Ready"), tr("2.Config"), tr("3.Capture"),
        tr("4.Transfer"), tr("5.Process"), tr("6.Result"),
    };
    for (int i = 0; i < names.size(); ++i) {
        if (i > 0) {
            auto* arrow = new QLabel("›");
            arrow->setAlignment(Qt::AlignCenter);
            layout->addWidget(arrow);
        }
        auto* lbl = new QLabel(names[i]);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        lbl->setStyleSheet("font-size: 11px;");
        stepLabels.append(lbl);
        layout->addWidget(lbl);
    }
    parent->addWidget(stepIndicator);
}

void UVBFCaptureDialog::updateStepIndicator(Step step)
{
    for (int i = 0; i < stepLabels.size(); ++i) {
        bool active = (i == static_cast<int>(step));
        QFont f = stepLabels[i]->font();
        f.setBold(active);
        stepLabels[i]->setFont(f);
        stepLabels[i]->setStyleSheet(
            active ? "font-size: 11px; color: palette(highlight);"
                   : "font-size: 11px;");
    }
}

// ============================================================================
// Left page builders
// ============================================================================

QWidget* UVBFCaptureDialog::buildReadinessPage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(10);

    layout->addWidget(new QLabel(tr("<b>Pre-Capture Readiness</b>")));

    auto* info = new QLabel(
        tr("All checks must pass (or be individually overridden) to proceed."));
    info->setWordWrap(true);
    info->setStyleSheet("font-size: 11px; color: #666;");
    layout->addWidget(info);

    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    grid->addWidget(new QLabel(tr("<b>Check</b>")),    0, 0);
    grid->addWidget(new QLabel(tr("<b>Value</b>")),    0, 1);
    grid->addWidget(new QLabel(tr("<b>Status</b>")),   0, 2);
    grid->addWidget(new QLabel(tr("<b>Override</b>")), 0, 3);

    auto makeIndicator = []() {
        auto* l = new QLabel("●");
        l->setAlignment(Qt::AlignCenter);
        l->setFixedWidth(22);
        return l;
    };

    // ── Distance row ──────────────────────────────────────────────────────────
    grid->addWidget(new QLabel(tr("Distance")), 1, 0);
    distanceValue     = new QLabel("— mm");
    distanceIndicator = makeIndicator();
    distanceOverride  = new QCheckBox;
    grid->addWidget(distanceValue,     1, 1);
    grid->addWidget(distanceIndicator, 1, 2);
    grid->addWidget(distanceOverride,  1, 3);

    medianFilterCheck = new QCheckBox(tr("Median filter"));
    medianFilterCheck->setChecked(true);
    medianFilterCheck->setToolTip(
        tr("Apply a sliding-window median filter to raw ToF distance readings.\n"
           "Uncheck to see raw sensor values for diagnostics."));
    medianFilterCheck->setStyleSheet("font-size: 10px; color: #555;");
    grid->addWidget(medianFilterCheck, 1, 4);

    // ── Ambient UV row ────────────────────────────────────────────────────────
    grid->addWidget(new QLabel(tr("Ambient UV")), 2, 0);
    ambientValue     = new QLabel("— W/m²");
    ambientIndicator = makeIndicator();
    ambientOverride  = new QCheckBox;
    grid->addWidget(ambientValue,     2, 1);
    grid->addWidget(ambientIndicator, 2, 2);
    grid->addWidget(ambientOverride,  2, 3);

    // ── Stability row ─────────────────────────────────────────────────────────
    grid->addWidget(new QLabel(tr("Stability")), 3, 0);
    motionValue     = new QLabel("—");
    motionIndicator = makeIndicator();
    motionOverride  = new QCheckBox;
    grid->addWidget(motionValue,     3, 1);
    grid->addWidget(motionIndicator, 3, 2);
    grid->addWidget(motionOverride,  3, 3);

    layout->addLayout(grid);
    layout->addStretch();

    connect(distanceOverride,  &QCheckBox::toggled,
            this, &UVBFCaptureDialog::onDistanceOverrideToggled);
    connect(ambientOverride,   &QCheckBox::toggled,
            this, &UVBFCaptureDialog::onAmbientOverrideToggled);
    connect(motionOverride,    &QCheckBox::toggled,
            this, &UVBFCaptureDialog::onMotionOverrideToggled);
    connect(medianFilterCheck, &QCheckBox::toggled,
            this, &UVBFCaptureDialog::onMedianFilterToggled);

    return page;
}

QWidget* UVBFCaptureDialog::buildConfigPage()
{
    auto* page   = new QWidget;
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* inner  = new QWidget;
    auto* layout = new QVBoxLayout(inner);
    layout->setSpacing(10);

    layout->addWidget(new QLabel(tr("<b>Capture Configuration</b>")));

    auto* camGroup  = new QGroupBox(tr("Camera"));
    auto* camLayout = new QFormLayout(camGroup);
    cameraCombo = new QComboBox;
    cameraCombo->addItem(tr("IMX219 / Arducam NoIR"), QString(Camera::IMX219));
    cameraCombo->addItem(tr("IMX708 / RGB"),           QString(Camera::IMX708));
    cameraCombo->setCurrentIndex(1);
    camLayout->addRow(tr("Camera:"), cameraCombo);
    resolutionLabel = new QLabel;
    resolutionLabel->setStyleSheet("color: #666; font-style: italic;");
    camLayout->addRow(tr("Resolution:"), resolutionLabel);
    layout->addWidget(camGroup);

    auto* expGroup  = new QGroupBox(tr("Exposure (manual — all auto disabled)"));
    auto* expLayout = new QFormLayout(expGroup);
    exposureSpinBox = new QSpinBox;
    exposureSpinBox->setRange(100, 1000000);
    exposureSpinBox->setValue(20000);
    exposureSpinBox->setSuffix(tr(" µs"));
    expLayout->addRow(tr("Exposure:"), exposureSpinBox);

    // ALS exposure hint — populated when transitioning to this step
    alsExposureHintLabel = new QLabel;
    alsExposureHintLabel->setStyleSheet(
        "color: #27ae60; font-style: italic; font-size: 11px;");
    alsExposureHintLabel->setWordWrap(true);
    alsExposureHintLabel->setVisible(false);
    expLayout->addRow("", alsExposureHintLabel);

    analogGainSpinBox = new QDoubleSpinBox;
    analogGainSpinBox->setRange(1.0, 16.0);
    analogGainSpinBox->setSingleStep(0.1);
    analogGainSpinBox->setValue(1.0);
    expLayout->addRow(tr("Analog Gain:"), analogGainSpinBox);
    digitalGainSpinBox = new QDoubleSpinBox;
    digitalGainSpinBox->setRange(1.0, 4.0);
    digitalGainSpinBox->setSingleStep(0.1);
    digitalGainSpinBox->setValue(1.0);
    expLayout->addRow(tr("Digital Gain:"), digitalGainSpinBox);
    digitalGainNote = new QLabel(tr("Digital gain not supported on IMX219"));
    digitalGainNote->setStyleSheet("color: #888; font-style: italic; font-size: 10px;");
    expLayout->addRow("", digitalGainNote);
    layout->addWidget(expGroup);

    auto* ledGroup  = new QGroupBox(tr("LED Illumination"));
    auto* ledLayout = new QVBoxLayout(ledGroup);
    auto* ledForm   = new QFormLayout;
    brightnessSpinBox = new QSpinBox;
    brightnessSpinBox->setRange(0, 255);
    brightnessSpinBox->setValue(200);
    ledForm->addRow(tr("Brightness (0-255):"), brightnessSpinBox);
    ledLayout->addLayout(ledForm);

    auto* gridLabel = new QLabel(tr("LED Selection (0–31):"));
    gridLabel->setStyleSheet("font-weight: bold;");
    ledLayout->addWidget(gridLabel);

    auto* ledGrid = new QGridLayout;
    ledGrid->setSpacing(4);
    for (int i = 0; i < 32; ++i) {
        ledCheckBoxes[i] = new QCheckBox(QString::number(i));
        ledCheckBoxes[i]->setFixedWidth(48);
        ledCheckBoxes[i]->setChecked(true);
        ledGrid->addWidget(ledCheckBoxes[i], i / 8, i % 8);
    }
    ledLayout->addLayout(ledGrid);

    auto* selRow  = new QHBoxLayout;
    auto* allBtn  = new QPushButton(tr("All"));
    auto* noneBtn = new QPushButton(tr("None"));
    allBtn->setMaximumWidth(60);
    noneBtn->setMaximumWidth(60);
    connect(allBtn,  &QPushButton::clicked, this, [this]() {
        for (auto* cb : ledCheckBoxes) cb->setChecked(true);
    });
    connect(noneBtn, &QPushButton::clicked, this, [this]() {
        for (auto* cb : ledCheckBoxes) cb->setChecked(false);
    });
    selRow->addWidget(allBtn);
    selRow->addWidget(noneBtn);
    selRow->addStretch();
    ledLayout->addLayout(selRow);
    layout->addWidget(ledGroup);

    auto* saveBtn = new QPushButton(tr("💾  Save Configuration"));
    connect(saveBtn, &QPushButton::clicked,
            this, &UVBFCaptureDialog::onSaveConfigClicked);
    layout->addWidget(saveBtn);
    layout->addStretch();

    scroll->setWidget(inner);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UVBFCaptureDialog::onCameraChanged);
    onCameraChanged(cameraCombo->currentIndex());

    return page;
}

QWidget* UVBFCaptureDialog::buildCapturePage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(10);

    layout->addWidget(new QLabel(tr("<b>Acquiring Frames</b>")));
    layout->addStretch();

    captureStatus = new QLabel(tr("Initialising..."));
    captureStatus->setAlignment(Qt::AlignCenter);
    captureStatus->setStyleSheet("font-size: 13px;");
    layout->addWidget(captureStatus);

    auto makeFrameLabel = [](const QString& name) -> QLabel* {
        auto* l = new QLabel(QString("○  %1").arg(name));
        l->setStyleSheet("font-size: 12px; color: #888;");
        return l;
    };
    captureFrame1 = makeFrameLabel(tr("Dark 1 (LEDs off)"));
    captureFrame2 = makeFrameLabel(tr("Illuminated 1 (LEDs on)"));
    captureFrame3 = makeFrameLabel(tr("Illuminated 2 (LEDs on)"));
    captureFrame4 = makeFrameLabel(tr("Illuminated 3 (LEDs on)"));
    captureFrame5 = makeFrameLabel(tr("Dark 2 (LEDs off)"));
    layout->addWidget(captureFrame1);
    layout->addWidget(captureFrame2);
    layout->addWidget(captureFrame3);
    layout->addWidget(captureFrame4);
    layout->addWidget(captureFrame5);

    auto* bar = new QProgressBar;
    bar->setRange(0, 0);
    bar->setFixedHeight(6);
    layout->addWidget(bar);

    layout->addStretch();
    return page;
}

QWidget* UVBFCaptureDialog::buildTransferPage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(10);

    layout->addWidget(new QLabel(tr("<b>Receiving Frames</b>")));

    transferStatus = new QLabel(tr("Waiting for server..."));
    transferStatus->setStyleSheet("font-size: 11px; color: #666;");
    layout->addWidget(transferStatus);

    auto makeBar = [&](const QString& label) -> QProgressBar* {
        layout->addWidget(new QLabel(label));
        auto* bar = new QProgressBar;
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setFixedHeight(14);
        layout->addWidget(bar);
        return bar;
    };

    dark1Progress  = makeBar(tr("Dark 1"));
    illum1Progress = makeBar(tr("Illuminated 1"));
    illum2Progress = makeBar(tr("Illuminated 2"));
    illum3Progress = makeBar(tr("Illuminated 3"));
    dark2Progress  = makeBar(tr("Dark 2"));

    layout->addStretch();
    return page;
}

QWidget* UVBFCaptureDialog::buildProcessingPage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(10);

    layout->addWidget(new QLabel(tr("<b>Processing</b>")));
    layout->addStretch();

    processingStage = new QLabel(tr("Waiting..."));
    processingStage->setAlignment(Qt::AlignCenter);
    processingStage->setStyleSheet("font-size: 13px;");
    layout->addWidget(processingStage);

    auto* bar = new QProgressBar;
    bar->setRange(0, 0);
    bar->setFixedHeight(6);
    layout->addWidget(bar);

    layout->addStretch();

    auto* note = new QLabel(tr("<i>Processing pipeline — placeholder</i>"));
    note->setAlignment(Qt::AlignCenter);
    note->setStyleSheet("color: #888; font-size: 10px;");
    layout->addWidget(note);

    return page;
}

QWidget* UVBFCaptureDialog::buildResultPage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(tr("<b>Capture Complete</b>")));

    auto* info = new QLabel(
        tr("Frames captured successfully.\n"
           "View and export each frame using the tabs on the right."));
    info->setWordWrap(true);
    info->setStyleSheet("font-size: 11px;");
    layout->addWidget(info);

    layout->addStretch();

    auto* row = new QHBoxLayout;
    btnRetake = new QPushButton(tr("↩  Retake"));
    btnSave   = new QPushButton(tr("✔  Accept & Close"));
    btnSave->setDefault(true);
    row->addWidget(btnRetake);
    row->addStretch();
    row->addWidget(btnSave);
    layout->addLayout(row);

    connect(btnRetake, &QPushButton::clicked,
            this, &UVBFCaptureDialog::onRetakeClicked);
    connect(btnSave,   &QPushButton::clicked,
            this, &UVBFCaptureDialog::onAcceptAndCloseClicked);

    return page;
}

void UVBFCaptureDialog::buildButtonRow(QVBoxLayout* parent)
{
    auto* row    = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 4, 0, 0);

    btnCancel = new QPushButton(tr("Cancel"));
    btnNext   = new QPushButton(tr("Next ›"));
    btnNext->setDefault(true);

    layout->addWidget(btnCancel);
    layout->addStretch();
    layout->addWidget(btnNext);

    connect(btnNext,   &QPushButton::clicked,
            this, &UVBFCaptureDialog::onNextClicked);
    connect(btnCancel, &QPushButton::clicked,
            this, &UVBFCaptureDialog::onCancelClicked);

    parent->addWidget(row);
}

// ============================================================================
// Navigation
// ============================================================================

void UVBFCaptureDialog::goToStep(Step step)
{
    pollTimer->stop();
    transferWatchdog->stop();

    leftStack->setCurrentIndex(static_cast<int>(step));
    syncRightPanel(step);
    updateStepIndicator(step);
    updateButtonState();

    switch (step) {
    case StepReadiness:
        pollTimer->start();
        break;

    case StepConfig:
    {
        // If we have a valid ALS reading, compute and apply a suggested exposure.
        constexpr uint32_t kTargetCounts = 50000;
        constexpr int      kMinUs        = 1000;
        constexpr int      kMaxUs        = 500000;

        if (lastAlsClear > 100 && alsExposureMs > 0.0f)
        {
            float currentUs = alsExposureMs * 1000.0f;
            int suggested   = static_cast<int>(
                (static_cast<float>(kTargetCounts) /
                 static_cast<float>(lastAlsClear)) * currentUs);
            suggested = std::clamp(suggested, kMinUs, kMaxUs);

            exposureSpinBox->setValue(suggested);

            if (alsExposureHintLabel)
            {
                alsExposureHintLabel->setText(
                    QString("ALS: %1 counts → setting exposure to %2 µs (%3 ms)")
                        .arg(lastAlsClear)
                        .arg(suggested)
                        .arg(suggested / 1000.0, 0, 'f', 1));
                alsExposureHintLabel->setStyleSheet(
                    "color: #27ae60; font-style: italic; font-size: 11px;");
                alsExposureHintLabel->setVisible(true);
            }
        }
        else if (alsExposureHintLabel)
        {
            alsExposureHintLabel->setText(
                tr("ALS not available — set exposure manually"));
            alsExposureHintLabel->setStyleSheet(
                "color: #e67e22; font-style: italic; font-size: 11px;");
            alsExposureHintLabel->setVisible(true);
        }
        break;
    }

    case StepCapture:
        captureFrame1->setText(tr("○  Dark 1 (LEDs off)"));
        captureFrame1->setStyleSheet("font-size: 12px; color: #888;");
        captureFrame2->setText(tr("○  Illuminated 1 (LEDs on)"));
        captureFrame2->setStyleSheet("font-size: 12px; color: #888;");
        captureFrame3->setText(tr("○  Illuminated 2 (LEDs on)"));
        captureFrame3->setStyleSheet("font-size: 12px; color: #888;");
        captureFrame4->setText(tr("○  Illuminated 3 (LEDs on)"));
        captureFrame4->setStyleSheet("font-size: 12px; color: #888;");
        captureFrame5->setText(tr("○  Dark 2 (LEDs off)"));
        captureFrame5->setStyleSheet("font-size: 12px; color: #888;");
        beginCapture();
        break;

    case StepTransfer:
        dark1Received  = false;
        illum1Received = false;
        illum2Received = false;
        illum3Received = false;
        dark2Received  = false;
        pendingPreviews = 0;
        dark1Progress->setValue(0);
        illum1Progress->setValue(0);
        illum2Progress->setValue(0);
        illum3Progress->setValue(0);
        dark2Progress->setValue(0);
        setTransferStatus(tr("Waiting for dark frame 1..."));
        transferWatchdog->start();
        break;

    case StepProcessing:
        // beginProcessing();
        break;

    case StepResult:
        populateResultTabs();
        break;
    }
}

void UVBFCaptureDialog::syncRightPanel(Step step)
{
    switch (step) {
    case StepReadiness:  rightStack->setCurrentIndex(0); break;
    case StepConfig:     rightStack->setCurrentIndex(1); break;
    case StepCapture:
    case StepTransfer:   rightStack->setCurrentIndex(2); break;
    case StepProcessing: rightStack->setCurrentIndex(1); break;
    case StepResult:     rightStack->setCurrentIndex(3); break;
    }
}

void UVBFCaptureDialog::updateButtonState()
{
    auto step = static_cast<Step>(leftStack->currentIndex());

    switch (step) {
    case StepReadiness:
        btnNext->setVisible(true);
        btnNext->setText(tr("Configure ›"));
        btnNext->setEnabled(
            checkAmbient.satisfied() &&
            checkMotion.satisfied());
        btnCancel->setText(tr("Cancel"));
        btnCancel->setVisible(true);
        break;

    case StepConfig:
        btnNext->setVisible(true);
        btnNext->setText(tr("Capture ›"));
        btnNext->setEnabled(true);
        btnCancel->setVisible(true);
        btnCancel->setText(tr("Cancel"));
        break;

    case StepCapture:
    case StepTransfer:
    case StepProcessing:
        btnNext->setVisible(false);
        btnCancel->setVisible(true);
        btnCancel->setText(tr("Cancel"));
        break;

    case StepResult:
        btnNext->setVisible(false);
        btnCancel->setVisible(true);
        btnCancel->setText(tr("Close"));
        break;
    }
}

// ============================================================================
// Button slots
// ============================================================================

void UVBFCaptureDialog::onNextClicked()
{
    auto step = static_cast<Step>(leftStack->currentIndex());
    if      (step == StepReadiness) goToStep(StepConfig);
    else if (step == StepConfig)    goToStep(StepCapture);
}

void UVBFCaptureDialog::onCancelClicked()
{
    pollTimer->stop();
    transferWatchdog->stop();
    reject();
}

void UVBFCaptureDialog::onRetakeClicked()
{
    // Temp files are left on disk and will be overwritten on the next capture.
    // Clear the session paths and previews so the result tabs show no stale data.
    session.dark1TempFile.clear();
    session.illum1TempFile.clear();
    session.illum2TempFile.clear();
    session.illum3TempFile.clear();
    session.dark2TempFile.clear();
    session.dark1Size  = 0;
    session.illum1Size = 0;
    session.illum2Size = 0;
    session.illum3Size = 0;
    session.dark2Size  = 0;
    session.dark1Preview  = {};
    session.illum1Preview = {};
    session.illum2Preview = {};
    session.illum3Preview = {};
    session.dark2Preview  = {};
    // Reset the pending-preview counter so that any callbacks still in flight
    // from the previous capture cycle cannot trigger goToStep(StepProcessing)
    // once they eventually fire.
    pendingPreviews = 0;
    // reset the distance filter so stale readings from the previous
    // capture cycle do not carry over into the new session.
    distanceFilter.reset();
    session.sessionId = QUuid::createUuid()
                            .toString(QUuid::WithoutBraces).left(8);
    goToStep(StepReadiness);
}

void UVBFCaptureDialog::onAcceptAndCloseClicked()
{
    emit captureAccepted(session);
    accept();
}

void UVBFCaptureDialog::onSaveConfigClicked()
{
    saveConfig();
    QMessageBox::information(this, tr("Configuration Saved"),
        tr("UVBF capture configuration saved."));
}

// ============================================================================
// Camera selector
// ============================================================================

void UVBFCaptureDialog::onCameraChanged(int)
{
    updateCameraControls(cameraCombo->currentData().toString());
}

void UVBFCaptureDialog::updateCameraControls(const QString& cam)
{
    bool isIMX708 = (cam == Camera::IMX708);
    digitalGainSpinBox->setVisible(isIMX708);
    digitalGainNote->setVisible(!isIMX708);
    analogGainSpinBox->setRange(1.0, isIMX708 ? 8.57 : 10.66);
    resolutionLabel->setText(isIMX708
        ? tr("4608 × 2592  (12 MP RAW, fixed)")
        : tr("3280 × 2464  (8 MP RAW, fixed)"));
}

// ============================================================================
// Config persistence
// ============================================================================

UVBFConfig UVBFCaptureDialog::readConfig() const
{
    UVBFConfig cfg;
    cfg.camera        = cameraCombo->currentData().toString();
    cfg.exposureUs    = exposureSpinBox->value();
    cfg.analogGain    = analogGainSpinBox->value();
    cfg.digitalGain   = digitalGainSpinBox->value();
    cfg.ledBrightness = brightnessSpinBox->value();
    for (int i = 0; i < 32; ++i)
        cfg.ledIds[i] = ledCheckBoxes[i]->isChecked();
    return cfg;
}

void UVBFCaptureDialog::applyConfig(const UVBFConfig& cfg)
{
    for (int i = 0; i < cameraCombo->count(); ++i)
        if (cameraCombo->itemData(i).toString() == cfg.camera)
            { cameraCombo->setCurrentIndex(i); break; }
    exposureSpinBox->setValue(cfg.exposureUs);
    analogGainSpinBox->setValue(cfg.analogGain);
    digitalGainSpinBox->setValue(cfg.digitalGain);
    brightnessSpinBox->setValue(cfg.ledBrightness);
    for (int i = 0; i < 32; ++i)
        ledCheckBoxes[i]->setChecked(cfg.ledIds[i]);
    updateCameraControls(cfg.camera);
}

void UVBFCaptureDialog::loadConfig()
{
    QSettings s("Sanuwave", "SanuwaveClient");
    s.beginGroup("uvbf_config");
    UVBFConfig cfg;
    cfg.camera        = s.value("camera",          cfg.camera).toString();
    cfg.exposureUs    = s.value("exposure_us",      cfg.exposureUs).toInt();
    cfg.analogGain    = s.value("analog_gain",      cfg.analogGain).toDouble();
    cfg.digitalGain   = s.value("digital_gain",     cfg.digitalGain).toDouble();
    cfg.ledBrightness = s.value("led_brightness",   cfg.ledBrightness).toInt();
    for (int i = 0; i < 32; ++i)
        cfg.ledIds[i] = s.value(QString("led_%1").arg(i), true).toBool();
    s.endGroup();
    applyConfig(cfg);
}

void UVBFCaptureDialog::saveConfig()
{
    UVBFConfig cfg = readConfig();
    QSettings s("Sanuwave", "SanuwaveClient");
    s.beginGroup("uvbf_config");
    s.setValue("camera",          cfg.camera);
    s.setValue("exposure_us",     cfg.exposureUs);
    s.setValue("analog_gain",     cfg.analogGain);
    s.setValue("digital_gain",    cfg.digitalGain);
    s.setValue("led_brightness",  cfg.ledBrightness);
    for (int i = 0; i < 32; ++i)
        s.setValue(QString("led_%1").arg(i), cfg.ledIds[i]);
    s.endGroup();
}

// ============================================================================
// Readiness polling
// ============================================================================

void UVBFCaptureDialog::pollSensorStatus()
{
    if (!client) return;
    QJsonObject cmd;
    cmd[Param::COMMAND] = Command::DISTANCE_READ;
    client->sendCommand(cmd);
}

// ============================================================================
// Distance filter
// ============================================================================

float UVBFCaptureDialog::applyDistanceFilter(float rawMm)
{
    float median = distanceFilter.push(rawMm);
    if (std::abs(rawMm - median) > kDistanceResetDeltaMm) {
        distanceFilter.reset();
        median = distanceFilter.push(rawMm);
    }
    return median;
}

// ============================================================================
// ALS data
// ============================================================================

void UVBFCaptureDialog::onALSData(uint32_t clear, float exposureMs)
{
    lastAlsClear  = clear;
    alsExposureMs = exposureMs;
}

// ============================================================================
// Sensor status
// ============================================================================

void UVBFCaptureDialog::onSensorStatus(float distMm, float ambUv, float motion)
{
    session.distanceMm  = distMm;
    session.ambientUv   = ambUv;
    session.motionScore = motion;

    //route distance through the filter when enabled.
    const float displayDist = useMedianFilter
        ? applyDistanceFilter(distMm)
        : distMm;

    // require a settled window when filter is on so a partially-populated
    // window cannot produce a spurious pass on the first few polls.
    checkDistance.passing =
        (!useMedianFilter || distanceFilter.settled()) &&
        (displayDist >= kDistanceMinMm && displayDist <= kDistanceMaxMm);

    checkAmbient.passing  = (ambUv  <= kAmbientUvMax);
    checkMotion.passing   = (motion <= kMotionScoreMax);

    refreshReadinessRow(distanceIndicator, distanceValue, checkDistance,
        QString("%1 mm").arg(static_cast<int>(displayDist)));
    refreshReadinessRow(ambientIndicator, ambientValue, checkAmbient,
        QString("%1 W/m²").arg(ambUv, 0, 'f', 3));
    refreshReadinessRow(motionIndicator, motionValue, checkMotion,
        QString("%1").arg(motion, 0, 'f', 3));

    evaluateReadiness();
}

void UVBFCaptureDialog::refreshReadinessRow(QLabel* indicator, QLabel* valueLabel,
                                             const ReadinessCheck& check,
                                             const QString& valueText)
{
    valueLabel->setText(valueText);
    if (check.passing)
        indicator->setStyleSheet("color: green;");
    else if (check.overridden)
        indicator->setStyleSheet("color: orange;");
    else
        indicator->setStyleSheet("color: red;");
}

void UVBFCaptureDialog::evaluateReadiness()
{
    if (leftStack->currentIndex() == StepReadiness)
        updateButtonState();
}

void UVBFCaptureDialog::onDistanceOverrideToggled(bool c)
{ checkDistance.overridden = c; evaluateReadiness(); }
void UVBFCaptureDialog::onAmbientOverrideToggled(bool c)
{ checkAmbient.overridden  = c; evaluateReadiness(); }
void UVBFCaptureDialog::onMotionOverrideToggled(bool c)
{ checkMotion.overridden   = c; evaluateReadiness(); }

void UVBFCaptureDialog::onMedianFilterToggled(bool checked)
{
    useMedianFilter = checked;
    distanceFilter.reset();   // discard stale window either way
}

// ============================================================================
// Capture
// ============================================================================

void UVBFCaptureDialog::beginCapture()
{
    if (!client) {
        QMessageBox::critical(this, tr("No Connection"),
            tr("Cannot start capture: not connected to server."));
        goToStep(StepReadiness);
        return;
    }

    session.config = readConfig();
    setCaptureStatus(tr("Configuring camera…"));

    {
        QJsonArray ids, brightnesses;
        for (int i = 0; i < 32; ++i) {
            if (session.config.ledIds[i]) {
                ids.append(i);
                brightnesses.append(session.config.ledBrightness);
            }
        }
        QJsonObject ledCmd;
        ledCmd[Param::COMMAND]          = Command::LED_SELECT;
        ledCmd[Param::LED_IDS]          = ids;
        ledCmd[Param::LED_BRIGHTNESSES] = brightnesses;
        client->sendCommand(ledCmd);
    }

    QJsonObject cmd;
    cmd[Param::COMMAND]          = Command::UVBF_CAPTURE;
    cmd[UVBFParam::CAMERA]       = session.config.camera;
    cmd[UVBFParam::EXPOSURE_US]  = session.config.exposureUs;
    cmd[UVBFParam::ANALOG_GAIN]  = session.config.analogGain;
    cmd[UVBFParam::DIGITAL_GAIN] = session.config.digitalGain;
    cmd[Param::SESSION_ID]       = session.sessionId;
    client->sendCommand(cmd);
}

void UVBFCaptureDialog::setCaptureStatus(const QString& text)
{
    if (captureStatus) captureStatus->setText(text);
}

void UVBFCaptureDialog::onUVBFStarted()
{
    setCaptureStatus(tr("Acquiring dark frame 1…"));
}

void UVBFCaptureDialog::onUVBFFrameCaptured(const QString& role)
{
    auto mark = [](QLabel* lbl, const QString& name) {
        lbl->setText(QString("✔  %1").arg(name));
        lbl->setStyleSheet("font-size: 12px; color: green;");
    };

    if (role == FrameRole::DARK_1) {
        mark(captureFrame1, tr("Dark 1 (LEDs off)"));
        setCaptureStatus(tr("Acquiring illuminated frame 1…"));
    } else if (role == FrameRole::ILLUMINATED_1) {
        mark(captureFrame2, tr("Illuminated 1 (LEDs on)"));
        setCaptureStatus(tr("Acquiring illuminated frame 2…"));
    } else if (role == FrameRole::ILLUMINATED_2) {
        mark(captureFrame3, tr("Illuminated 2 (LEDs on)"));
        setCaptureStatus(tr("Acquiring illuminated frame 3…"));
    } else if (role == FrameRole::ILLUMINATED_3) {
        mark(captureFrame4, tr("Illuminated 3 (LEDs on)"));
        setCaptureStatus(tr("Acquiring dark frame 2…"));
    } else if (role == FrameRole::DARK_2) {
        mark(captureFrame5, tr("Dark 2 (LEDs off)"));
        setCaptureStatus(tr("Transfer starting…"));
        goToStep(StepTransfer);
    }
}

void UVBFCaptureDialog::onUVBFError(const QString& stage, const QString& reason)
{
    transferWatchdog->stop();
    QMessageBox::critical(this, tr("Capture Error"),
        tr("Capture failed at stage '%1':\n%2\n\n"
           "All frames have been discarded.").arg(stage, reason));
    goToStep(StepReadiness);
}

void UVBFCaptureDialog::onCaptureModeAck(bool success, const QString& error)
{
    if (success) return;
    QMessageBox::warning(this, tr("Capture Error"),
        tr("Server rejected capture mode: %1").arg(error));
    goToStep(StepReadiness);
}

void UVBFCaptureDialog::onServerError(const QString& message)
{
    auto step = static_cast<Step>(leftStack->currentIndex());
    if (step == StepReadiness || step == StepResult || step == StepConfig)
        return;
    onUVBFError(tr("server"), message);
}

void UVBFCaptureDialog::onServerDisconnected()
{
    auto step = static_cast<Step>(leftStack->currentIndex());
    if (step == StepReadiness || step == StepResult) return;

    transferWatchdog->stop();
    pollTimer->stop();
    QMessageBox::critical(this, tr("Connection Lost"),
        tr("The server disconnected unexpectedly.\n\n"
           "The capture has been aborted. All frames have been discarded."));
    goToStep(StepReadiness);
}

// ============================================================================
// Transfer
// ============================================================================

void UVBFCaptureDialog::onTransferTimeout()
{
    QMessageBox::critical(this, tr("Transfer Timeout"),
        tr("No data received from the server for %1 seconds.\n\n"
           "The transfer has been aborted. All frames have been discarded.")
            .arg(kTransferTimeoutMs / 1000));
    goToStep(StepReadiness);
}

void UVBFCaptureDialog::setTransferStatus(const QString& text)
{
    if (transferStatus) transferStatus->setText(text);
}

void UVBFCaptureDialog::onFrameTransferProgress(const QString& role,
                                                  int bytesReceived,
                                                  int totalBytes)
{
    transferWatchdog->start();

    int pct = totalBytes > 0 ? bytesReceived * 100 / totalBytes : 0;

    if (role == FrameRole::DARK_1) {
        dark1Progress->setValue(pct);
        setTransferStatus(tr("Receiving dark frame 1 (%1%)…").arg(pct));
    } else if (role == FrameRole::ILLUMINATED_1) {
        illum1Progress->setValue(pct);
        setTransferStatus(tr("Receiving illuminated frame 1 (%1%)…").arg(pct));
    } else if (role == FrameRole::ILLUMINATED_2) {
        illum2Progress->setValue(pct);
        setTransferStatus(tr("Receiving illuminated frame 2 (%1%)…").arg(pct));
    } else if (role == FrameRole::ILLUMINATED_3) {
        illum3Progress->setValue(pct);
        setTransferStatus(tr("Receiving illuminated frame 3 (%1%)…").arg(pct));
    } else if (role == FrameRole::DARK_2) {
        dark2Progress->setValue(pct);
        setTransferStatus(tr("Receiving dark frame 2 (%1%)…").arg(pct));
    }
}

// ============================================================================
// Temp file helpers
// ============================================================================

QString UVBFCaptureDialog::tempFilePathForRole(const QString& role)
{
    // Fixed filenames in the system temp directory — overwritten each capture.
    // Sanitise the role string so it is safe as a filename component.
    QString safe = role;
    safe.replace('/', '_').replace('\\', '_').replace(' ', '_');
    return QDir::tempPath() + QString("/sanuwave_uvbf_%1.dng").arg(safe);
}

bool UVBFCaptureDialog::writeFrameToTempFile(const QString& path,
                                              const QByteArray& payload)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 written = f.write(payload);
    f.close();
    return written == static_cast<qint64>(payload.size());
}

QByteArray UVBFCaptureDialog::readFrameFromTempFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray data = f.readAll();
    f.close();
    return data;
}

// ============================================================================
// Transfer complete — write to temp file, free payload, decode preview async
// ============================================================================

void UVBFCaptureDialog::onFrameTransferComplete(const UVBFFrameInfo& frameInfo,
                                                  QByteArray dngData)
{
    transferWatchdog->start();

    if (dngData.isEmpty()) {
        onUVBFError(frameInfo.role, tr("Server sent an empty frame payload."));
        return;
    }

    const QString role        = frameInfo.role;
    const qint64  payloadSize = static_cast<qint64>(dngData.size());

    // ── Write payload to temp file (DNG / PNG save reads this later) ──────────
    const QString tempPath = tempFilePathForRole(role);
    if (!writeFrameToTempFile(tempPath, dngData)) {
        onUVBFError(role,
            tr("Failed to write frame to temp file:\n%1").arg(tempPath));
        return;
    }

    auto formatTs = [](uint64_t ms) -> QString {
        return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ms))
                   .toString("hh:mm:ss.zzz");
    };

    ZoomImageWidget* rightViewer  = nullptr;
    QLabel*          rightTsLabel = nullptr;
    QString          tsText;
    QPixmap*         previewSlot  = nullptr;

    if (role == FrameRole::DARK_1) {
        session.dark1TempFile     = tempPath;
        session.dark1Size         = payloadSize;
        session.dark1Info         = frameInfo.imageInfo;
        session.dark1Timestamp_ms = frameInfo.captureTimestamp_ms;
        dark1Progress->setValue(100);
        dark1Received  = true;
        rightViewer    = rightDark1Viewer;
        rightTsLabel   = rightDark1Timestamp;
        previewSlot    = &session.dark1Preview;
        tsText         = tr("Captured %1").arg(formatTs(frameInfo.captureTimestamp_ms));

    } else if (role == FrameRole::ILLUMINATED_1) {
        session.illum1TempFile      = tempPath;
        session.illum1Size          = payloadSize;
        session.illum1Info          = frameInfo.imageInfo;
        session.illum1Timestamp_ms  = frameInfo.captureTimestamp_ms;
        session.ledOnTimestamp1_ms  = frameInfo.ledOnTimestamp_ms;
        session.ledOffTimestamp1_ms = frameInfo.ledOffTimestamp_ms;
        illum1Progress->setValue(100);
        illum1Received = true;
        rightViewer    = rightIllum1Viewer;
        rightTsLabel   = rightIllum1Timestamp;
        previewSlot    = &session.illum1Preview;
        tsText         = tr("Captured %1  ·  LED on %2  ·  off %3")
                             .arg(formatTs(frameInfo.captureTimestamp_ms))
                             .arg(formatTs(frameInfo.ledOnTimestamp_ms))
                             .arg(formatTs(frameInfo.ledOffTimestamp_ms));

    } else if (role == FrameRole::ILLUMINATED_2) {
        session.illum2TempFile      = tempPath;
        session.illum2Size          = payloadSize;
        session.illum2Info          = frameInfo.imageInfo;
        session.illum2Timestamp_ms  = frameInfo.captureTimestamp_ms;
        session.ledOnTimestamp2_ms  = frameInfo.ledOnTimestamp_ms;
        session.ledOffTimestamp2_ms = frameInfo.ledOffTimestamp_ms;
        illum2Progress->setValue(100);
        illum2Received = true;
        rightViewer    = rightIllum2Viewer;
        rightTsLabel   = rightIllum2Timestamp;
        previewSlot    = &session.illum2Preview;
        tsText         = tr("Captured %1  ·  LED on %2  ·  off %3")
                             .arg(formatTs(frameInfo.captureTimestamp_ms))
                             .arg(formatTs(frameInfo.ledOnTimestamp_ms))
                             .arg(formatTs(frameInfo.ledOffTimestamp_ms));

    } else if (role == FrameRole::ILLUMINATED_3) {
        session.illum3TempFile      = tempPath;
        session.illum3Size          = payloadSize;
        session.illum3Info          = frameInfo.imageInfo;
        session.illum3Timestamp_ms  = frameInfo.captureTimestamp_ms;
        session.ledOnTimestamp3_ms  = frameInfo.ledOnTimestamp_ms;
        session.ledOffTimestamp3_ms = frameInfo.ledOffTimestamp_ms;
        illum3Progress->setValue(100);
        illum3Received = true;
        rightViewer    = rightIllum3Viewer;
        rightTsLabel   = rightIllum3Timestamp;
        previewSlot    = &session.illum3Preview;
        tsText         = tr("Captured %1  ·  LED on %2  ·  off %3")
                             .arg(formatTs(frameInfo.captureTimestamp_ms))
                             .arg(formatTs(frameInfo.ledOnTimestamp_ms))
                             .arg(formatTs(frameInfo.ledOffTimestamp_ms));

    } else if (role == FrameRole::DARK_2) {
        session.dark2TempFile     = tempPath;
        session.dark2Size         = payloadSize;
        session.dark2Info         = frameInfo.imageInfo;
        session.dark2Timestamp_ms = frameInfo.captureTimestamp_ms;
        dark2Progress->setValue(100);
        dark2Received  = true;
        rightViewer    = rightDark2Viewer;
        rightTsLabel   = rightDark2Timestamp;
        previewSlot    = &session.dark2Preview;
        tsText         = tr("Captured %1").arg(formatTs(frameInfo.captureTimestamp_ms));

    } else {
        onUVBFError(tr("transfer"), tr("Unknown frame role: '%1'").arg(role));
        return;
    }

    if (rightTsLabel)
        rightTsLabel->setText(tsText);

    bool allReceived = dark1Received && illum1Received && illum2Received &&
                       illum3Received && dark2Received;
    if (allReceived)
        transferWatchdog->stop();

    // ── Decode preview on background thread ───────────────────────────────────
    // dngData is moved into the lambda — zero copy, no disk read, no race.
    // The lambda owns the only live copy; it frees after decode so peak RAM
    // is one frame (~24-35 MB) at a time.  The temp file written above is
    // untouched and remains valid for DNG / PNG save on demand.
    QPointer<ZoomImageWidget> safeViewer = rightViewer;
    ++pendingPreviews;
    auto nothing = QtConcurrent::run([this, safeViewer, previewSlot,
                                       allReceived,
                                       payload = std::move(dngData)]() mutable {
        QPixmap px;
        if (!payload.isEmpty()) {
            px = sanuwave::RawBayerDecoder::previewFromRawPayload(payload, 4);
            payload.clear();   // free 16-24 MB before posting result
        }
        if (!px.isNull() && px.width() > 300)
            px = px.scaledToWidth(300, Qt::SmoothTransformation);
        if (px.isNull()) {
            px = QPixmap(300, 225);
            px.fill(Qt::darkGray);
        }
        QMetaObject::invokeMethod(qApp, [this, safeViewer, previewSlot, px]() {
            if (static_cast<Step>(leftStack->currentIndex()) != StepTransfer)
                return;
            if (previewSlot)
                *previewSlot = px;
            if (safeViewer)
                safeViewer->setPixmap(px);
            if (--pendingPreviews == 0 &&
                dark1Received && illum1Received && illum2Received &&
                illum3Received && dark2Received) {
                goToStep(StepResult);
            }
        }, Qt::QueuedConnection);
    });
}

// ============================================================================
// Processing
// ============================================================================
#ifdef DELETEME
void UVBFCaptureDialog::beginProcessing()
{
    finishProcessing();
}

void UVBFCaptureDialog::finishProcessing()
{
    goToStep(StepResult);
}
#endif

// ============================================================================
// Result
// ============================================================================

void UVBFCaptureDialog::populateResultTabs()
{
    auto formatTs = [](uint64_t ms) -> QString {
        if (ms == 0) return tr("—");
        return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ms))
                   .toString("hh:mm:ss.zzz");
    };

    if (dark1Viewer  && !session.dark1Preview.isNull())
        dark1Viewer->setPixmap(session.dark1Preview);
    if (dark1Timestamp)
        dark1Timestamp->setText(tr("Captured %1")
            .arg(formatTs(session.dark1Timestamp_ms)));

    if (illum1Viewer && !session.illum1Preview.isNull())
        illum1Viewer->setPixmap(session.illum1Preview);
    if (illum1Timestamp)
        illum1Timestamp->setText(
            tr("Captured %1  ·  LED on %2  ·  off %3")
                .arg(formatTs(session.illum1Timestamp_ms))
                .arg(formatTs(session.ledOnTimestamp1_ms))
                .arg(formatTs(session.ledOffTimestamp1_ms)));

    if (illum2Viewer && !session.illum2Preview.isNull())
        illum2Viewer->setPixmap(session.illum2Preview);
    if (illum2Timestamp)
        illum2Timestamp->setText(
            tr("Captured %1  ·  LED on %2  ·  off %3")
                .arg(formatTs(session.illum2Timestamp_ms))
                .arg(formatTs(session.ledOnTimestamp2_ms))
                .arg(formatTs(session.ledOffTimestamp2_ms)));

    if (illum3Viewer && !session.illum3Preview.isNull())
        illum3Viewer->setPixmap(session.illum3Preview);
    if (illum3Timestamp)
        illum3Timestamp->setText(
            tr("Captured %1  ·  LED on %2  ·  off %3")
                .arg(formatTs(session.illum3Timestamp_ms))
                .arg(formatTs(session.ledOnTimestamp3_ms))
                .arg(formatTs(session.ledOffTimestamp3_ms)));

    if (dark2Viewer  && !session.dark2Preview.isNull())
        dark2Viewer->setPixmap(session.dark2Preview);
    if (dark2Timestamp)
        dark2Timestamp->setText(tr("Captured %1")
            .arg(formatTs(session.dark2Timestamp_ms)));
}
