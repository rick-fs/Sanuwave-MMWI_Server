#include "raw_diag_window.h"
#include "bayer_image_widget.h"
#include "channel_histogram_widget.h"
#include "server_connection.h"
#include "raw_bayer_decoding.h"
#include "dng_exporter.h"
#include "protocol_constants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QScrollArea>
#include <QLabel>
#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonArray>

namespace Cmd = sanuwave::protocol::Command;

// ============================================================================
// Construction
// ============================================================================

RawDiagnosticWindow::RawDiagnosticWindow(ServerConnection& connection,
                                           QWidget* parent)
    : QDialog(parent)
    , connection(connection)
{
    setWindowTitle("Raw Image Diagnostic");
    setMinimumSize(1100, 750);
    resize(1300, 850);
    setModal(true);

    setupUI();

    connect(&connection, &ServerConnection::diagRawFrameReceived,
            this, &RawDiagnosticWindow::onRawFrameReceived);
    connect(&connection, &ServerConnection::diagErrorReceived,
        this, [this](const QString& /*camera*/, int /*code*/, const QString& msg) {
            onDiagError(msg);
        });
    connect(&connection, &ServerConnection::ledStatusReceived,
            this, &RawDiagnosticWindow::onLedStatusReceived);

    // Query LED availability as soon as window opens
    requestLedStatus();
}

RawDiagnosticWindow::~RawDiagnosticWindow()
{
}
// ============================================================================
// UI Setup
// ============================================================================

void RawDiagnosticWindow::setupUI()
{
    auto* mainLayout = new QHBoxLayout(this);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    auto* controlScroll = new QScrollArea;
    controlScroll->setWidgetResizable(true);
    controlScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controlScroll->setMinimumWidth(240);
    controlScroll->setMaximumWidth(320);
    controlScroll->setWidget(createControlPanel());
    splitter->addWidget(controlScroll);

    splitter->addWidget(createVisualizationPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter);
}

QWidget* RawDiagnosticWindow::createControlPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);

    // --- Camera Selection ---
    auto* cameraGroup = new QGroupBox("Camera");
    auto* cameraLayout = new QVBoxLayout(cameraGroup);
    cameraCombo = new QComboBox;
    cameraCombo->addItem("RGB (IMX708)",     sanuwave::protocol::Camera::IMX708);
    cameraCombo->addItem("Arducam (IMX219)", sanuwave::protocol::Camera::IMX219);
    cameraCombo->addItem("Thermal (Lepton)", sanuwave::protocol::Camera::THERMAL);
    connect(cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RawDiagnosticWindow::onCameraChanged);
    cameraLayout->addWidget(cameraCombo);
    layout->addWidget(cameraGroup);

    // --- Capture Settings ---
    auto* settingsGroup = new QGroupBox("Capture Settings");
    auto* settingsLayout = new QGridLayout(settingsGroup);

    settingsLayout->addWidget(new QLabel("Exposure (µs):"), 0, 0);
    exposureSpin = new QSpinBox;
    exposureSpin->setRange(100, 2000000);
    exposureSpin->setValue(10000);
    exposureSpin->setSingleStep(1000);
    settingsLayout->addWidget(exposureSpin, 0, 1);

    settingsLayout->addWidget(new QLabel("Analog Gain:"), 1, 0);
    gainSpin = new QDoubleSpinBox;
    gainSpin->setRange(1.0, 16.0);
    gainSpin->setValue(1.0);
    gainSpin->setSingleStep(0.5);
    gainSpin->setDecimals(1);
    settingsLayout->addWidget(gainSpin, 1, 1);

    disableAE = new QCheckBox("Disable AE");
    disableAE->setChecked(true);
    settingsLayout->addWidget(disableAE, 2, 0, 1, 2);

    disableAWB = new QCheckBox("Disable AWB");
    disableAWB->setChecked(true);
    settingsLayout->addWidget(disableAWB, 3, 0, 1, 2);

    disableDenoise = new QCheckBox("Disable Denoise");
    disableDenoise->setChecked(true);
    settingsLayout->addWidget(disableDenoise, 4, 0, 1, 2);

    layout->addWidget(settingsGroup);

    // --- LED Illumination ---
    auto* ledGroup = new QGroupBox("LED Illumination");
    auto* ledLayout = new QVBoxLayout(ledGroup);

    ledStatusLabel = new QLabel("Querying LED availability...");
    ledStatusLabel->setStyleSheet("QLabel { color: #888; font-size: 11px; }");
    ledStatusLabel->setWordWrap(true);
    ledLayout->addWidget(ledStatusLabel);

    // Table: Enable | LED ID | Brightness
    ledTable = new QTableWidget(0, 3);
    ledTable->setHorizontalHeaderLabels({"On", "LED", "Brightness"});
    ledTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ledTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ledTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ledTable->setSelectionMode(QAbstractItemView::NoSelection);
    ledTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ledTable->verticalHeader()->setVisible(false);
    ledTable->setMinimumHeight(100);
    ledTable->setMaximumHeight(220);
    ledLayout->addWidget(ledTable);

    layout->addWidget(ledGroup);

    // --- Diff Test Frame Count ---
    auto* diffGroup = new QGroupBox("Diff Test");
    auto* diffLayout = new QHBoxLayout(diffGroup);
    diffLayout->addWidget(new QLabel("Frames:"));
    frameCountSpin = new QSpinBox;
    frameCountSpin->setRange(2, 50);
    frameCountSpin->setValue(10);
    diffLayout->addWidget(frameCountSpin);
    layout->addWidget(diffGroup);

    // --- Action Buttons ---
    auto* actionGroup = new QGroupBox("Actions");
    auto* actionLayout = new QVBoxLayout(actionGroup);

    captureBtn = new QPushButton("Capture Raw Frame");
    captureBtn->setStyleSheet("QPushButton { font-weight: bold; padding: 8px; }");
    connect(captureBtn, &QPushButton::clicked,
            this, &RawDiagnosticWindow::onCaptureClicked);
    actionLayout->addWidget(captureBtn);

    darkTestBtn = new QPushButton("Dark Frame Test");
    darkTestBtn->setToolTip("Capture with lens capped to analyze black levels");
    connect(darkTestBtn, &QPushButton::clicked,
            this, &RawDiagnosticWindow::onDarkTestClicked);
    actionLayout->addWidget(darkTestBtn);

    diffTestBtn = new QPushButton("Frame Diff Test");
    diffTestBtn->setToolTip("Capture N frames of static scene, check consistency");
    connect(diffTestBtn, &QPushButton::clicked,
            this, &RawDiagnosticWindow::onDiffTestClicked);
    actionLayout->addWidget(diffTestBtn);

    layout->addWidget(actionGroup);

    // --- Export ---
    auto* exportGroup = new QGroupBox("Export");
    auto* exportLayout = new QVBoxLayout(exportGroup);

    saveDngBtn = new QPushButton("Save as DNG...");
    saveDngBtn->setToolTip("Save the last captured raw frame as a DNG file");
    saveDngBtn->setEnabled(false);
    connect(saveDngBtn, &QPushButton::clicked,
            this, &RawDiagnosticWindow::onSaveDngClicked);
    exportLayout->addWidget(saveDngBtn);
    layout->addWidget(exportGroup);

    // --- Metadata Display ---
    auto* metaGroup = new QGroupBox("Frame Metadata");
    auto* metaLayout = new QGridLayout(metaGroup);

    auto addMeta = [&](const QString& label, QLabel*& value, int row) {
        metaLayout->addWidget(new QLabel(label), row, 0);
        value = new QLabel("—");
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        metaLayout->addWidget(value, row, 1);
    };

    addMeta("Exposure:",     metaExposure,   0);
    addMeta("Analog Gain:",  metaGain,       1);
    addMeta("Digital Gain:", metaDigitalGain, 2);
    addMeta("Black Level:",  metaBlackLevel,  3);
    addMeta("AWB Gains:",    metaAWBGains,    4);
    addMeta("Format:",       metaFormat,      5);
    addMeta("Bits:",         metaBits,        6);
    addMeta("Color Temp:",   metaColorTemp,   7);

    layout->addWidget(metaGroup);
    layout->addStretch();
    return panel;
}

QWidget* RawDiagnosticWindow::createVisualizationPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* topBar = new QHBoxLayout;
    pixelInfoLabel = new QLabel("Hover over image to inspect pixels");
    pixelInfoLabel->setStyleSheet(
        "QLabel { font-family: monospace; font-size: 12px; padding: 4px; "
        "background: #1a1a2e; color: #e0e0e0; border-radius: 3px; }");
    topBar->addWidget(pixelInfoLabel, 1);

    demosaicToggle = new QCheckBox("Demosaic Preview");
    demosaicToggle->setToolTip("Toggle between raw Bayer mosaic and debayered RGB preview");
    connect(demosaicToggle, &QCheckBox::toggled,
            this, &RawDiagnosticWindow::onViewModeChanged);
    topBar->addWidget(demosaicToggle);
    layout->addLayout(topBar);

    bayerView = new BayerImageWidget;
    connect(bayerView, &BayerImageWidget::pixelHovered,
            this, &RawDiagnosticWindow::onPixelHovered);

    auto* imageScroll = new QScrollArea;
    imageScroll->setWidget(bayerView);
    imageScroll->setWidgetResizable(true);
    imageScroll->setMinimumHeight(300);
    layout->addWidget(imageScroll, 3);

    auto* histGroup = new QGroupBox("Per-Channel Histograms");
    auto* histGrid = new QGridLayout(histGroup);
    histGrid->setSpacing(4);

    histR  = new ChannelHistogramWidget("R",  QColor(220, 50, 50));
    histGr = new ChannelHistogramWidget("Gr", QColor(50, 180, 50));
    histGb = new ChannelHistogramWidget("Gb", QColor(30, 150, 30));
    histB  = new ChannelHistogramWidget("B",  QColor(50, 50, 220));

    histGrid->addWidget(histR,  0, 0);
    histGrid->addWidget(histGr, 0, 1);
    histGrid->addWidget(histGb, 1, 0);
    histGrid->addWidget(histB,  1, 1);
    layout->addWidget(histGroup, 2);

    auto* logGroup = new QGroupBox("Analysis Log");
    auto* logLayout = new QVBoxLayout(logGroup);
    analysisLog = new QTextEdit;
    analysisLog->setReadOnly(true);
    analysisLog->setFont(QFont("Monospace", 9));
    analysisLog->setMaximumHeight(150);
    logLayout->addWidget(analysisLog);
    layout->addWidget(logGroup, 1);

    return panel;
}

QJsonObject RawDiagnosticWindow::buildDiagParams() const
{
    QJsonObject params;
    params["camera"]          = cameraCombo->currentData().toString();
    params["exposure_us"]     = exposureSpin->value();
    params["analog_gain"]     = gainSpin->value();
    params["disable_ae"]      = disableAE->isChecked();
    params["disable_awb"]     = disableAWB->isChecked();
    params["disable_denoise"] = disableDenoise->isChecked();

    QJsonArray ids, brightnesses;
    for (int row = 0; row < ledTable->rowCount(); ++row)
    {
        if (ledTable->item(row, 0)->checkState() != Qt::Checked)
            continue;
        int id = ledTable->item(row, 1)->data(Qt::UserRole).toInt();
        auto* spin = qobject_cast<QSpinBox*>(ledTable->cellWidget(row, 2));
        ids.append(id);
        brightnesses.append(spin ? spin->value() : 128);
    }
    if (!ids.isEmpty())
    {
        params[sanuwave::protocol::Param::LED_IDS]          = ids;
        params[sanuwave::protocol::Param::LED_BRIGHTNESSES] = brightnesses;
    }
    return params;
}

// ============================================================================
// LED helpers
// ============================================================================

void RawDiagnosticWindow::requestLedStatus()
{
    QJsonObject cmd;
    cmd["command"] = Cmd::LED_GET_STATUS;
    connection.sendCommand(cmd);
}

void RawDiagnosticWindow::populateLedTable(const QJsonArray& availableIds)
{
    ledTable->setRowCount(0);

    for (const auto& v : availableIds)
    {
        int id  = v.toInt();
        int row = ledTable->rowCount();
        ledTable->insertRow(row);

        // Column 0: checkbox
        auto* chk = new QTableWidgetItem;
        chk->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        chk->setCheckState(Qt::Unchecked);
        chk->setTextAlignment(Qt::AlignCenter);
        ledTable->setItem(row, 0, chk);

        // Column 1: LED ID (read-only)
        auto* idItem = new QTableWidgetItem(QString::number(id));
        idItem->setTextAlignment(Qt::AlignCenter);
        idItem->setData(Qt::UserRole, id);   // store int id for retrieval
        ledTable->setItem(row, 1, idItem);

        // Column 2: brightness spinbox
        auto* brightSpin = new QSpinBox;
        brightSpin->setRange(0, 255);
        brightSpin->setValue(128);
        brightSpin->setFrame(false);
        ledTable->setCellWidget(row, 2, brightSpin);
    }

    ledTable->resizeRowsToContents();
}

void RawDiagnosticWindow::onLedStatusReceived(const QJsonObject& response)
{
    availableLedIds.clear();
    QJsonArray ids = response.value("available_ids").toArray();
    for (const auto& v : ids)
        availableLedIds.insert(v.toInt());

    int total     = response.value("total").toInt(32);
    int available = availableLedIds.size();

    ledStatusLabel->setText(
        QString("%1 / %2 LEDs available").arg(available).arg(total));
    ledStatusLabel->setStyleSheet(
        available > 0
            ? "QLabel { color: #4caf50; font-size: 11px; }"
            : "QLabel { color: #f44336; font-size: 11px; }");

    populateLedTable(ids);
    ledStatusReceived = true;

    appendLog(QString("LED status: %1/%2 available").arg(available).arg(total));
}


// ============================================================================
// Capture State
// ============================================================================

void RawDiagnosticWindow::updateCaptureEnabled()
{
    bool canCapture = !darkTestRunning && !diffTestRunning;
    captureBtn->setEnabled(canCapture);
    darkTestBtn->setEnabled(canCapture);
    diffTestBtn->setEnabled(canCapture);
}

// ============================================================================
// Capture Actions
// ============================================================================

void RawDiagnosticWindow::onCaptureClicked()
{
    appendLog("--- Single Raw Capture ---");
    connection.sendDiagRawRequest(buildDiagParams());
    captureBtn->setEnabled(false);
    captureBtn->setText("Capturing...");
}

void RawDiagnosticWindow::onDarkTestClicked()
{
    appendLog("=== Dark Frame Test ===");
    appendLog("Ensure lens is capped or sensor is covered.");
    darkTestRunning = true;
    capturedFrames.clear();
    updateCaptureEnabled();

    QJsonObject params = buildDiagParams();
    params.remove(sanuwave::protocol::Param::LED_IDS);
    params.remove(sanuwave::protocol::Param::LED_BRIGHTNESSES);
    params["analog_gain"] = 1.0;
    params["disable_ae"]  = true;
    params["disable_awb"] = true;
    connection.sendDiagRawRequest(params);
}

void RawDiagnosticWindow::onDiffTestClicked()
{
    diffFramesTarget = frameCountSpin->value();
    appendLog(QString("=== Frame Diff Test (%1 frames) ===").arg(diffFramesTarget));
    appendLog("Keep scene static during capture.");
    diffTestRunning = true;
    capturedFrames.clear();
    updateCaptureEnabled();
    connection.sendDiagRawRequest(buildDiagParams());
}
// ============================================================================
// Save DNG
// ============================================================================

void RawDiagnosticWindow::onSaveDngClicked()
{
    if (lastCapturedFrame.pixelData.empty())
    {
        QMessageBox::warning(this, "No Data", "No raw frame has been captured yet.");
        return;
    }

    QString camera = QString::fromStdString(lastCapturedFrame.camera);
    if (camera.isEmpty())
        camera = cameraCombo->currentText().section(' ', 0, 0).toLower();

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultName = QString("diag_%1_%2.dng").arg(camera, timestamp);

    QString filename = QFileDialog::getSaveFileName(
        this, "Save Raw Frame as DNG", defaultName,
        "DNG Files (*.dng);;All Files (*)");

    if (filename.isEmpty())
        return;

    sanuwave::RawImageData raw;
    raw.width  = lastCapturedFrame.width;
    raw.height = lastCapturedFrame.height;
    raw.bitsPerSample = static_cast<uint16_t>(
        lastCapturedFrame.sensorBitDepth > 0
            ? lastCapturedFrame.sensorBitDepth
            : lastCapturedFrame.bitsPerPixel);

    using BP = sanuwave::protocol::BayerPattern;
    switch (lastCapturedFrame.bayerPattern)
    {
        case BP::RGGB: raw.cfaPattern[0]=0; raw.cfaPattern[1]=1; raw.cfaPattern[2]=1; raw.cfaPattern[3]=2; break;
        case BP::BGGR: raw.cfaPattern[0]=2; raw.cfaPattern[1]=1; raw.cfaPattern[2]=1; raw.cfaPattern[3]=0; break;
        case BP::GBRG: raw.cfaPattern[0]=1; raw.cfaPattern[1]=2; raw.cfaPattern[2]=0; raw.cfaPattern[3]=1; break;
        case BP::GRBG: raw.cfaPattern[0]=1; raw.cfaPattern[1]=0; raw.cfaPattern[2]=2; raw.cfaPattern[3]=1; break;
        default:       raw.cfaPattern[0]=0; raw.cfaPattern[1]=1; raw.cfaPattern[2]=1; raw.cfaPattern[3]=2; break;
    }

    size_t pixelCount    = static_cast<size_t>(raw.width) * raw.height;
    size_t expectedBytes = pixelCount * 2;

    if (lastCapturedFrame.pixelData.size() >= expectedBytes)
    {
        raw.rawPixels.resize(pixelCount);
        const uint16_t* src = reinterpret_cast<const uint16_t*>(
            lastCapturedFrame.pixelData.data());
        std::copy(src, src + pixelCount, raw.rawPixels.begin());
    }
    else
    {
        QMessageBox::warning(this, "Data Error",
            QString("Pixel data size mismatch: expected %1 bytes, got %2")
                .arg(expectedBytes).arg(lastCapturedFrame.pixelData.size()));
        return;
    }

    raw.exposureTime_s = lastCapturedFrame.metadata.actualExposureUs / 1000000.0;
    raw.analogGain     = lastCapturedFrame.metadata.actualAnalogGain;
    raw.blackLevel = static_cast<uint16_t>(
        lastCapturedFrame.metadata.sensorBlackLevels[0] > 0
            ? lastCapturedFrame.metadata.sensorBlackLevels[0]
            : 64);
    raw.whiteLevel = static_cast<uint16_t>((1 << raw.bitsPerSample) - 1);

    if (lastCapturedFrame.camera == sanuwave::protocol::Camera::IMX708)
        raw.cameraModel = "imx708";
    else if (lastCapturedFrame.camera == sanuwave::protocol::Camera::IMX219)
        raw.cameraModel = "imx219";
    else
        raw.cameraModel = lastCapturedFrame.camera;

    sanuwave::DngExporter::populateSensorDefaults(
        raw, lastCapturedFrame.bayerPattern, raw.width);

    QString errorMsg;
    bool ok = sanuwave::DngExporter::writeDng(filename, raw, errorMsg);

    if (ok)
        appendLog(QString("DNG saved: %1 (%2x%3, %4-bit)")
            .arg(filename).arg(raw.width).arg(raw.height).arg(raw.bitsPerSample));
    else
    {
        appendLog(QString("DNG save FAILED: %1").arg(errorMsg));
        QMessageBox::critical(this, "Save Failed",
            QString("Failed to save DNG file:\n%1").arg(errorMsg));
    }
}

// ============================================================================
// Frame Reception
// ============================================================================

void RawDiagnosticWindow::onRawFrameReceived(const sanuwave::protocol::DiagRawFrame& frame)
{
    appendLog(QString("Received frame: %1x%2, %3bpp (sensor %4-bit), format=%5")
              .arg(frame.width).arg(frame.height)
              .arg(frame.bitsPerPixel)
              .arg(frame.sensorBitDepth)
              .arg(QString::fromStdString(frame.pixelFormat)));

    lastCapturedFrame = frame;
    saveDngBtn->setEnabled(!frame.pixelData.empty());

    bayerView->setRawData(frame.pixelData.data(), frame.width, frame.height,
                          frame.bitsPerPixel,
                          QString(sanuwave::protocol::bayerPatternToString(frame.bayerPattern)));
    updateMetadataDisplay(frame);
    updateHistograms(frame);

    captureBtn->setEnabled(true);
    captureBtn->setText("Capture Raw Frame");

    if (darkTestRunning)
    {
        darkTestRunning = false;
        updateCaptureEnabled();
        appendLog("Dark frame analysis complete. (analyzer not yet implemented)");
        return;
    }

    if (diffTestRunning)
    {
        capturedFrames.push_back(frame);
        int received = static_cast<int>(capturedFrames.size());
        appendLog(QString("  Frame %1/%2 captured").arg(received).arg(diffFramesTarget));

        if (received < diffFramesTarget)
        {
            connection.sendDiagRawRequest(buildDiagParams());
        }
        else
        {
            diffTestRunning = false;
            updateCaptureEnabled();
            appendLog("Diff test complete. (analyzer not yet implemented)");
        }
        return;
    }
}

void RawDiagnosticWindow::onDiagError(const QString& error)
{
    appendLog(QString("ERROR: %1").arg(error));
    captureBtn->setEnabled(true);
    captureBtn->setText("Capture Raw Frame");
    darkTestRunning = false;
    diffTestRunning = false;
    updateCaptureEnabled();
}

// ============================================================================
// UI Updates
// ============================================================================

void RawDiagnosticWindow::onCameraChanged(int /*index*/)
{
    bool isThermal = (cameraCombo->currentData().toString() == "thermal");
    disableAWB->setEnabled(!isThermal);
    disableDenoise->setEnabled(!isThermal);
}

void RawDiagnosticWindow::onPixelHovered(int x, int y, uint16_t rawValue, int channel)
{
    static const char* chNames[] = {"R", "Gr", "Gb", "B"};
    const char* ch = (channel >= 0 && channel < 4) ? chNames[channel] : "?";
    pixelInfoLabel->setText(
        QString("(%1, %2)  %3 = 0x%4 (%5)")
            .arg(x).arg(y).arg(ch)
            .arg(rawValue, 4, 16, QChar('0'))
            .arg(rawValue));
}

void RawDiagnosticWindow::onViewModeChanged(bool demosaic)
{
    if (lastCapturedFrame.pixelData.empty())
        return;

    if (demosaic)
    {
        sanuwave::RawImageInfo info;
        info.width       = static_cast<int>(lastCapturedFrame.width);
        info.height      = static_cast<int>(lastCapturedFrame.height);
        info.bitDepth    = static_cast<int>(lastCapturedFrame.sensorBitDepth > 0
                               ? lastCapturedFrame.sensorBitDepth
                               : lastCapturedFrame.bitsPerPixel);
        info.storageBits = static_cast<int>(lastCapturedFrame.bitsPerPixel);
        info.blackLevel  = lastCapturedFrame.metadata.sensorBlackLevels[0];
        info.pattern     = lastCapturedFrame.bayerPattern;

        std::vector<uint8_t> rgb = sanuwave::RawBayerDecoder::decode(
            lastCapturedFrame.pixelData.data(),
            lastCapturedFrame.pixelData.size(),
            info);

        if (!rgb.empty())
        {
            QImage image(rgb.data(), info.width, info.height,
                         info.width * 3, QImage::Format_RGB888);
            bayerView->setDemosaicImage(image.copy());
        }
        else
        {
            appendLog("Demosaic failed — showing raw mosaic");
            demosaicToggle->blockSignals(true);
            demosaicToggle->setChecked(false);
            demosaicToggle->blockSignals(false);
        }
    }
    else
    {
        bayerView->setRawData(lastCapturedFrame.pixelData.data(),
                              lastCapturedFrame.width, lastCapturedFrame.height,
                              lastCapturedFrame.bitsPerPixel,
                              QString(sanuwave::protocol::bayerPatternToString(
                                  lastCapturedFrame.bayerPattern)));
    }
}

void RawDiagnosticWindow::updateMetadataDisplay(const sanuwave::protocol::DiagRawFrame& f)
{
    metaExposure->setText(QString("%1 µs").arg(f.metadata.actualExposureUs));
    metaGain->setText(QString::number(f.metadata.actualAnalogGain, 'f', 2));
    metaDigitalGain->setText(QString::number(f.metadata.actualDigitalGain, 'f', 2));
    metaBlackLevel->setText(QString("%1, %2, %3, %4")
        .arg(f.metadata.sensorBlackLevels[0]).arg(f.metadata.sensorBlackLevels[1])
        .arg(f.metadata.sensorBlackLevels[2]).arg(f.metadata.sensorBlackLevels[3]));
    metaAWBGains->setText(QString("%1, %2")
        .arg(f.metadata.colourGains[0], 0, 'f', 3)
        .arg(f.metadata.colourGains[1], 0, 'f', 3));
    metaFormat->setText(QString::fromStdString(f.pixelFormat));
    if (f.sensorBitDepth > 0 && f.sensorBitDepth != f.bitsPerPixel)
        metaBits->setText(QString("%1-bit sensor / %2-bit data")
            .arg(f.sensorBitDepth).arg(f.bitsPerPixel));
    else
        metaBits->setText(QString::number(f.bitsPerPixel));
    metaColorTemp->setText(f.metadata.colourTemperature > 0
        ? QString("%1 K").arg(f.metadata.colourTemperature) : "—");
}

void RawDiagnosticWindow::updateHistograms(const sanuwave::protocol::DiagRawFrame& frame)
{
    if (frame.pixelData.empty() || frame.width == 0 || frame.height == 0)
        return;

    uint32_t w = frame.width, h = frame.height, bpp = frame.bitsPerPixel;

    std::vector<uint16_t> chR, chGr, chGb, chB;
    size_t total = (w / 2) * (h / 2);
    chR.reserve(total); chGr.reserve(total);
    chGb.reserve(total); chB.reserve(total);

    auto getPixel = [&](uint32_t x, uint32_t y) -> uint16_t {
        size_t idx = (y * w + x) * 2;
        if (idx + 1 >= frame.pixelData.size()) return 0;
        return static_cast<uint16_t>(frame.pixelData[idx])
             | (static_cast<uint16_t>(frame.pixelData[idx + 1]) << 8);
    };

    for (uint32_t y = 0; y < h - 1; y += 2)
        for (uint32_t x = 0; x < w - 1; x += 2)
        {
            chR.push_back(getPixel(x,     y));
            chGr.push_back(getPixel(x + 1, y));
            chGb.push_back(getPixel(x,     y + 1));
            chB.push_back(getPixel(x + 1, y + 1));
        }

    histR->setData(chR,   bpp);
    histGr->setData(chGr, bpp);
    histGb->setData(chGb, bpp);
    histB->setData(chB,   bpp);
}

void RawDiagnosticWindow::appendLog(const QString& text)
{
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    analysisLog->append(QString("[%1] %2").arg(ts, text));
}
