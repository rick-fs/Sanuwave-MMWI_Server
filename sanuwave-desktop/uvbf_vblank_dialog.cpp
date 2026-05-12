

#include "uvbf_vblank_dialog.h"
#include "server_connection.h"   
#include "protocol_constants.h"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QStandardPaths>
#include <QSettings>
#include <QTabWidget>
#include <QTextStream>
#include <QToolTip>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrent>

#include "dng_exporter.h"
#include "raw_bayer_decoding.h"
#include "logger.h"

#include <cmath>

namespace Camera    = sanuwave::protocol::Camera;
namespace Command   = sanuwave::protocol::Command;
namespace Param     = sanuwave::protocol::Param;
namespace UVBFParam = sanuwave::protocol::UVBFParam;

// Forward declaration — defined before populateTimingTable below
static QString displayRole(const QString& role);

// ============================================================================
// Constructor
// ============================================================================

UVBFVBlankDialog::UVBFVBlankDialog(ServerConnection* connection_,
                                   const QString&    camera_,
                                   double            analogGain_,
                                   const QString&    sessionId_,
                                   QWidget*          parent)
    : QDialog(parent)
    , connection(connection_)
    , sessionId(sessionId_)
{
    setWindowTitle(tr("UVBF VBlank Timing Experiment"));
    setModal(true);
    setMinimumWidth(820);

    buildUI();

    // Seed from caller-supplied defaults, then overlay with saved settings.
    // loadVBlankConfig runs second so saved values win over the caller defaults
    // (the caller defaults are the fallback for a first-ever run).
    if (cameraCombo) {
        int idx = cameraCombo->findData(camera_);
        if (idx >= 0) cameraCombo->setCurrentIndex(idx);
    }
    if (analogGainSpinBox)
        analogGainSpinBox->setValue(analogGain_);

    updateCameraControls(camera_);
    loadVBlankConfig();
}

// ============================================================================
// buildUI — outer shell with QStackedWidget
// ============================================================================

void UVBFVBlankDialog::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(10, 10, 10, 10);

    stack = new QStackedWidget;
    stack->addWidget(buildConfigPage());    // page 0
    stack->addWidget(buildProgressPage()); // page 1
    stack->addWidget(buildResultsPage());  // page 2
    root->addWidget(stack);

    auto* btnRow = new QHBoxLayout;
    cancelButton = new QPushButton(tr("Cancel"));
    btnRow->addStretch();
    btnRow->addWidget(cancelButton);
    root->addLayout(btnRow);

    connect(cancelButton, &QPushButton::clicked,
            this,         &UVBFVBlankDialog::onCancelClicked);

    goToPage(0);
}

// ============================================================================
// Page 0 — Configuration
// ============================================================================

QWidget* UVBFVBlankDialog::buildConfigPage()
{
    auto* page   = new QWidget;
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* inner  = new QWidget;
    auto* layout = new QVBoxLayout(inner);
    layout->setSpacing(10);

    layout->addWidget(new QLabel(tr("<b>VBlank Timing Experiment — Configuration</b>")));

    // ── Camera ───────────────────────────────────────────────────────────────
    auto* camGroup  = new QGroupBox(tr("Camera"));
    auto* camLayout = new QFormLayout(camGroup);

    cameraCombo = new QComboBox;
    cameraCombo->addItem(tr("IMX219 / Arducam NoIR"), QString(Camera::IMX219));
    cameraCombo->addItem(tr("IMX708 / RGB"),           QString(Camera::IMX708));
    cameraCombo->setCurrentIndex(0);
    camLayout->addRow(tr("Camera:"), cameraCombo);

    resolutionCombo = new QComboBox;
    camLayout->addRow(tr("Resolution:"), resolutionCombo);

    auto* resNote = new QLabel(
        tr("Lower resolutions use less CMA. Use full resolution only if "
           "timing differs and you have cma= configured in config.txt."));
    resNote->setStyleSheet("color: #666; font-style: italic; font-size: 10px;");
    resNote->setWordWrap(true);
    camLayout->addRow("", resNote);

    layout->addWidget(camGroup);

    connect(cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UVBFVBlankDialog::onCameraChanged);

    // ── Exposure ─────────────────────────────────────────────────────────────
    auto* expGroup  = new QGroupBox(tr("Exposure (manual — all auto disabled)"));
    auto* expLayout = new QFormLayout(expGroup);

    exposureSpinBox = new QSpinBox;
    exposureSpinBox->setRange(100, 1000000);
    exposureSpinBox->setValue(20000);
    exposureSpinBox->setSuffix(tr(" µs"));
    expLayout->addRow(tr("Exposure:"), exposureSpinBox);

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

    // ── LED Illumination ─────────────────────────────────────────────────────
    auto* ledGroup  = new QGroupBox(tr("LED Illumination"));
    auto* ledLayout = new QVBoxLayout(ledGroup);
    auto* ledForm   = new QFormLayout;

    brightnessSpinBox = new QSpinBox;
    brightnessSpinBox->setRange(0, 255);
    brightnessSpinBox->setValue(200);
    ledForm->addRow(tr("Brightness (0–255):"), brightnessSpinBox);

    predictVBlankCheck = new QCheckBox(tr("Predict VBlank strobeOff"));
    predictVBlankCheck->setToolTip(
        tr("Turn LEDs off after one full frame period instead of waiting\n"
           "for the next frame callback.  The LED stays on through the\n"
           "VBlank gap and the illuminated frame's readout, turning off\n"
           "right at the next frame boundary."));
    ledForm->addRow("", predictVBlankCheck);

    kernelStrobeCheck = new QCheckBox(tr("Kernel GPIO strobe (deterministic)"));
    kernelStrobeCheck->setToolTip(
        tr("Drive LED strobe GPIOs from the kernel camera ISR instead of\n"
           "userspace.  Locks LED on/off transitions to actual frame\n"
           "boundaries with sub-microsecond precision.\n\n"
           "Requires the patched rp1-cfe kernel module with sysfs\n"
           "strobe interface.  Falls back to userspace if sysfs not found."));
    ledForm->addRow("", kernelStrobeCheck);

    // Quality check: server-side phase correlation between the illum
    // frames of the burst. Measures device drift across the capture;
    // results appear in the Results table's "Drift" columns and as a
    // PASS/FAIL banner. Off by default; persisted via QSettings.
    motionCheckCheck = new QCheckBox(tr("Measure inter-frame motion (quality check)"));
    motionCheckCheck->setToolTip(
        tr("Server runs phase correlation between all illuminated frames\n"
           "in the burst and reports drift per illum frame. Adds ~3 ms\n"
           "per pair on the Pi 5 - tens of ms total for a 9-illum burst,\n"
           "negligible vs transfer time. All work happens AFTER the burst\n"
           "completes, so it does NOT impact the 400 ms LED-flash-timeout\n"
           "budget."));
    ledForm->addRow("", motionCheckCheck);

    ledLayout->addLayout(ledForm);

    // 4 × 8 LED checkbox grid
    auto* gridLabel = new QLabel(tr("LED Selection (0–31):"));
    gridLabel->setStyleSheet("font-weight: bold;");
    ledLayout->addWidget(gridLabel);

    auto* ledGrid = new QGridLayout;
    ledGrid->setSpacing(4);
    for (int i = 0; i < 32; ++i) {
        ledCheckBoxes[i] = new QCheckBox(QString::number(i));
        ledCheckBoxes[i]->setFixedWidth(48);
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

    // ── Start button ─────────────────────────────────────────────────────────
    startButton = new QPushButton(tr("▶  Start Experiment"));
    startButton->setMinimumHeight(40);
    startButton->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; }");
    connect(startButton, &QPushButton::clicked,
            this, &UVBFVBlankDialog::onStartClicked);
    layout->addWidget(startButton);

    layout->addStretch();
    scroll->setWidget(inner);

    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    // Set initial camera-dependent state
    updateCameraControls(cameraCombo->currentData().toString());

    return page;
}

// ============================================================================
// Page 1 — Progress
// ============================================================================

QWidget* UVBFVBlankDialog::buildProgressPage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(10);

    layout->addWidget(new QLabel(tr("<b>Capture in Progress</b>")));

    statusLabel = new QLabel(tr("Waiting for server…"));
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    auto* captureGroup = new QGroupBox(tr("Capture"));
    auto* cg           = new QGridLayout(captureGroup);
    cg->addWidget(new QLabel(tr("Frames:")), 0, 0);
    captureBar = new QProgressBar;
    captureBar->setRange(0, 100);
    cg->addWidget(captureBar, 0, 1);
    captureCountLabel = new QLabel("0 / ?");
    cg->addWidget(captureCountLabel, 0, 2);
    cg->setColumnStretch(1, 1);
    layout->addWidget(captureGroup);

    auto* transferGroup = new QGroupBox(tr("Transfer"));
    auto* tg            = new QGridLayout(transferGroup);
    tg->addWidget(new QLabel(tr("Frames:")), 0, 0);
    transferBar = new QProgressBar;
    transferBar->setRange(0, 100);
    tg->addWidget(transferBar, 0, 1);
    transferCountLabel = new QLabel("0 / ?");
    tg->addWidget(transferCountLabel, 0, 2);
    tg->setColumnStretch(1, 1);
    layout->addWidget(transferGroup);

    layout->addStretch();
    return page;
}

// ============================================================================
// TimingChart — per-frame horizontal timeline widget
// ============================================================================

class TimingChart : public QWidget
{
public:
    struct Row {
        QString role;
        QString displayRole;      // "Ambient" instead of "dark" for GUI
        bool    ledsOn          = false;
        double  rollingShutter_us = 0.0;
        double  frameDur_us     = 0.0;
        double  callbackDelta_us = 0.0;
        bool    inVBlank        = false;
        // For illuminated frames: LED-on time relative to this frame's start.
        // Negative means LED turned on during the preceding VBlank (correct).
        // kNoLedData when not applicable (ambient frames).
        double  ledOnOffset_us  = kNoLedData;
        // For ambient frames preceding an illuminated frame: the callback
        // position where the LED turns ON, shown as amber tail to frame end.
        double  ledOnTail_us    = kNoLedData;
        static constexpr double kNoLedData = 1e18;
    };

    struct StrobeEvent {
        bool    on;
        int64_t elapsed_ms;
        bool    predicted = false;  // true if this was a predicted VBlank off
    };

    explicit TimingChart(QWidget* parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(40);
        setMouseTracking(true);
    }

    void setRows(const QVector<Row>& rows, const QString& subtitle = QString())
    {
        rows_    = rows;
        subtitle_ = subtitle;

        // Scale to the median frame period so startup frames with slow IPA
        // settling don't distort the x-axis.  Callbacks that land beyond the
        // axis are clipped but still drawn (with a marker at the right edge).
        QVector<double> periods;
        for (const auto& r : rows_)
            if (r.frameDur_us > 0.0) periods.append(r.frameDur_us);

        if (periods.isEmpty()) {
            maxUs_ = 1.0;
        } else {
            std::sort(periods.begin(), periods.end());
            // Use the 75th-percentile value — shows the steady-state period
            // without being pulled up by anomalous startup frames.
            const int idx = qMin(static_cast<int>(periods.size() * 0.75),
                                 periods.size() - 1);
            // Add 10% headroom so the rightmost tick doesn't clip
            maxUs_ = periods[idx] * 1.1;
        }
        if (maxUs_ <= 0.0) maxUs_ = 1.0;

        setFixedHeight(static_cast<int>(rows_.size()) * kRowH + kScaleH + kPadTop
                       + (subtitle_.isEmpty() ? 0 : kSubtitleH));
        update();
    }

    void setStrobeDiagnostics(const QVector<StrobeEvent>& events,
                              double flashTimeout_ms,
                              bool timeoutExceeded)
    {
        strobeEvents_     = events;
        flashTimeout_ms_  = flashTimeout_ms;
        timeoutExceeded_  = timeoutExceeded;
        const int strobeH = (flashTimeout_ms_ > 0.0) ? 28 : 0;
        setFixedHeight(static_cast<int>(rows_.size()) * kRowH + kScaleH
                       + kPadTop
                       + (subtitle_.isEmpty() ? 0 : kSubtitleH)
                       + strobeH);
        update();
    }

    void mouseMoveEvent(QMouseEvent* ev) override
    {
        const int y = ev->pos().y() - kPadTop;
        const int row = y / kRowH;

        if (row >= 0 && row < rows_.size()) {
            const Row& r = rows_[row];
            const double vblankGap = r.frameDur_us - r.rollingShutter_us;
            const double margin    = r.frameDur_us - r.callbackDelta_us;

            QString illumLine;
            if (r.ledsOn && r.ledOnOffset_us < Row::kNoLedData) {
                const bool fullyCovered = (r.ledOnOffset_us <= 0.0 &&
                                           r.callbackDelta_us >= r.rollingShutter_us);
                illumLine = fullyCovered
                    ? tr("<br>✓ <b>Fully illuminated</b> — LED on %1 µs before readout, off %2 µs after")
                          .arg(-r.ledOnOffset_us, 0, 'f', 1)
                          .arg(r.callbackDelta_us - r.rollingShutter_us, 0, 'f', 1)
                    : tr("<br>⚠ <b>Partial illumination</b> — LED on at %1 µs (should be ≤ 0), off at %2 µs")
                          .arg(r.ledOnOffset_us, 0, 'f', 1)
                          .arg(r.callbackDelta_us, 0, 'f', 1);
            } else if (!r.ledsOn && r.ledOnTail_us < Row::kNoLedData) {
                illumLine = tr("<br>→ LED turns ON at %1 µs (VBlank) for next illuminated frame")
                                .arg(r.ledOnTail_us, 0, 'f', 1);
            }

            QString tip = tr(
                "<b>%1</b>  %2<br>"
                "Callback delta:  <b>%3 µs</b><br>"
                "Rolling shutter: %4 µs  (readout ends here)<br>"
                "Frame period:    %5 µs  (next frame starts here)<br>"
                "VBlank window:   %6 µs<br>"
                "Margin:          %7 µs  %8%9")
                .arg(r.displayRole)
                .arg(r.ledsOn ? tr("LEDs ON ●") : tr("LEDs off"))
                .arg(r.callbackDelta_us,  0, 'f', 1)
                .arg(r.rollingShutter_us, 0, 'f', 1)
                .arg(r.frameDur_us,       0, 'f', 1)
                .arg(vblankGap,           0, 'f', 1)
                .arg(margin,              0, 'f', 1)
                .arg(r.inVBlank
                     ? tr("✓ inside VBlank")
                     : tr("✗ outside VBlank — overshot by %1 µs")
                           .arg(-margin, 0, 'f', 1))
                .arg(illumLine);

            QToolTip::showText(ev->globalPosition().toPoint(), tip, this);
        } else {
            QToolTip::hideText();
        }
        QWidget::mouseMoveEvent(ev);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (rows_.isEmpty()) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        const int W    = width() - kLabelW - kRightPad;
        const int left = kLabelW;

        // ── Subtitle (camera + resolution) ────────────────────────────────────
        int subtitleOffset = 0;
        if (!subtitle_.isEmpty()) {
            subtitleOffset = kSubtitleH;
            p.setPen(QColor(80, 80, 80));
            QFont sf = p.font();
            sf.setPointSize(8);
            sf.setItalic(true);
            p.setFont(sf);
            p.drawText(left, kPadTop, W, kSubtitleH - 2,
                       Qt::AlignLeft | Qt::AlignVCenter, subtitle_);
        }

        auto toX = [&](double us) -> int {
            return left + static_cast<int>(us / maxUs_ * W);
        };

        // ── Rows ─────────────────────────────────────────────────────────────
        for (int i = 0; i < rows_.size(); ++i) {
            const Row& r  = rows_[i];
            const int  y  = kPadTop + subtitleOffset + i * kRowH;
            const int  barY = y + 4;
            const int  barH = kRowH - 8;

            // Label
            p.setPen(Qt::black);
            QFont f = p.font();
            f.setPointSize(8);
            p.setFont(f);
            p.drawText(0, y, kLabelW - 4, kRowH,
                       Qt::AlignRight | Qt::AlignVCenter,
                       r.displayRole + (r.ledsOn ? " ●" : ""));

            // Background — full frame period in light gray (clipped to axis)
            const int frameRight = std::min(toX(r.frameDur_us > 0.0 ? r.frameDur_us : maxUs_),
                                            left + W);
            p.fillRect(left, barY, frameRight - left, barH,
                       QColor(220, 220, 220));

            // Rolling shutter region — darker gray
            const int rsRight = toX(r.rollingShutter_us);
            p.fillRect(left, barY, rsRight - left, barH,
                       QColor(160, 160, 170));

            // VBlank window — green tint
            const int vbLeft  = rsRight;
            const int vbRight = frameRight;
            if (vbRight > vbLeft) {
                p.fillRect(vbLeft, barY, vbRight - vbLeft, barH,
                           QColor(180, 230, 180));
            }

            // ── Amber tail on ambient rows ────────────────────────────────────
            // When this ambient frame is followed by an illuminated frame, the
            // LED turns on at this frame's callback tick and stays on through
            // to the end of the frame period.  Draw that as a semi-transparent
            // amber bar from the callback to the right edge of the frame bar.
            if (!r.ledsOn && r.ledOnTail_us < Row::kNoLedData) {
                const int tailX = toX(r.ledOnTail_us);
                if (frameRight > tailX) {
                    p.save();
                    p.setOpacity(0.45);
                    p.fillRect(tailX, barY, frameRight - tailX, barH,
                               QColor(255, 200, 0));
                    p.restore();
                }
            }

            // ── Illumination overlay bar ──────────────────────────────────────
            // For illuminated frames: amber semi-transparent bar spanning from
            // ledOnOffset_us (negative = in preceding VBlank) to callbackDelta_us
            // (this frame's VBlank).  If the bar covers the full readout region
            // (0 to rollingShutter_us), the frame is uniformly illuminated.
            if (r.ledsOn && r.ledOnOffset_us < Row::kNoLedData) {
                const int ledOnX  = std::max(toX(r.ledOnOffset_us), left);
                const int ledOffX = std::min(toX(r.callbackDelta_us), left + W);
                if (ledOffX > ledOnX) {
                    p.save();
                    p.setOpacity(0.45);
                    p.fillRect(ledOnX, barY, ledOffX - ledOnX, barH,
                               QColor(255, 200, 0));   // amber
                    p.setOpacity(1.0);
                    // Left edge of overlay = LED-on moment
                    p.setPen(QPen(QColor(180, 130, 0), 2));
                    p.drawLine(ledOnX, barY - 2, ledOnX, barY + barH + 2);
                    p.restore();

                    // Check: does amber bar cover the full readout?
                    const bool fullyCovered = (r.ledOnOffset_us <= 0.0 &&
                                               r.callbackDelta_us >= r.rollingShutter_us);
                    if (fullyCovered) {
                        // Green checkmark at right end of readout region
                        p.setPen(QPen(QColor(0, 150, 0), 2));
                        QFont cf = p.font();
                        cf.setPointSize(8);
                        cf.setBold(true);
                        p.setFont(cf);
                        p.drawText(rsRight + 2, barY, 16, barH,
                                   Qt::AlignLeft | Qt::AlignVCenter, "✓");
                    }
                }
            }

            // Callback tick mark
            const int cbX = toX(r.callbackDelta_us);
            const QColor cbColor = r.inVBlank ? QColor(0, 160, 0) : QColor(200, 0, 0);
            p.setPen(QPen(cbColor, 2));
            p.drawLine(cbX, barY - 2, cbX, barY + barH + 2);

            // Frame border
            p.setPen(QColor(180, 180, 180));
            p.drawRect(left, barY, frameRight - left, barH);
        }

        // ── Scale bar ─────────────────────────────────────────────────────────
        const int scaleY = kPadTop + subtitleOffset + rows_.size() * kRowH + 2;
        p.setPen(Qt::black);
        QFont sf = p.font();
        sf.setPointSize(7);
        p.setFont(sf);

        // Draw tick marks every ~10% of maxUs, rounded to a nice number
        double tickInterval = niceInterval(maxUs_ / 8.0);
        for (double us = 0.0; us <= maxUs_ + tickInterval * 0.5; us += tickInterval) {
            const int x = toX(us);
            p.drawLine(x, scaleY, x, scaleY + 4);
            QString label = us >= 1000.0
                ? QString("%1ms").arg(us / 1000.0, 0, 'f', 1)
                : QString("%1µs").arg(us, 0, 'f', 0);
            p.drawText(x - 20, scaleY + 5, 40, 12,
                       Qt::AlignHCenter, label);
        }

        // ── Legend ────────────────────────────────────────────────────────────
        const int legY = scaleY;
        const int legX = left + W - 290;
        auto legendBox = [&](int x, int y, QColor c, const QString& text) {
            p.fillRect(x, y, 12, 10, c);
            p.setPen(Qt::black);
            p.drawText(x + 14, y - 1, 100, 12, Qt::AlignLeft, text);
        };
        legendBox(legX,       legY, QColor(160, 160, 170), "Readout");
        legendBox(legX + 75,  legY, QColor(180, 230, 180), "VBlank");
        // Amber illumination
        p.save();
        p.setOpacity(0.45);
        p.fillRect(legX + 148, legY, 12, 10, QColor(255, 200, 0));
        p.setOpacity(1.0);
        p.restore();
        p.setPen(Qt::black);
        p.drawText(legX + 162, legY - 1, 70, 12, Qt::AlignLeft, "Illuminated");
        // Callback markers
        p.setPen(QPen(QColor(0, 160, 0), 2));
        p.drawLine(legX + 232, legY + 5, legX + 244, legY + 5);
        p.setPen(Qt::black);
        p.drawText(legX + 246, legY - 1, 20, 12, Qt::AlignLeft, "In");
        p.setPen(QPen(QColor(200, 0, 0), 2));
        p.drawLine(legX + 262, legY + 5, legX + 274, legY + 5);
        p.setPen(Qt::black);
        p.drawText(legX + 276, legY - 1, 30, 12, Qt::AlignLeft, "Out");

        // ── Strobe diagnostics overlay ────────────────────────────────────
        if (flashTimeout_ms_ > 0.0 && !rows_.isEmpty())
        {
            const int diagY = scaleY + kScaleH + 4;

            // Compute the total burst duration for x-scaling
            double burstDuration_ms = flashTimeout_ms_ * 1.15;
            for (const auto& ev : strobeEvents_)
                burstDuration_ms = std::max(burstDuration_ms,
                                            static_cast<double>(ev.elapsed_ms) * 1.1);

            auto toBarX = [&](double ms) -> int {
                return left + static_cast<int>(ms / burstDuration_ms * W);
            };

            // Label
            QFont diagFont = p.font();
            diagFont.setPointSize(7);
            diagFont.setBold(true);
            p.setFont(diagFont);
            p.setPen(Qt::black);
            p.drawText(0, diagY, kLabelW - 4, 20,
                        Qt::AlignRight | Qt::AlignVCenter,
                        tr("Strobe"));

            // Background bar
            p.fillRect(left, diagY + 2, W, 16, QColor(240, 240, 240));

            // Timeout deadline — red vertical line
            const int deadlineX = toBarX(flashTimeout_ms_);
            p.setPen(QPen(QColor(220, 40, 40), 2, Qt::DashLine));
            p.drawLine(deadlineX, diagY, deadlineX, diagY + 20);

            // Label the deadline
            diagFont.setBold(false);
            diagFont.setPointSize(6);
            p.setFont(diagFont);
            p.setPen(QColor(220, 40, 40));
            p.drawText(deadlineX + 3, diagY - 1, 80, 12,
                        Qt::AlignLeft | Qt::AlignVCenter,
                        tr("%1 ms timeout").arg(static_cast<int>(flashTimeout_ms_)));

            // Draw strobe on/off events
            for (int i = 0; i < strobeEvents_.size(); ++i)
            {
                const auto& ev = strobeEvents_[i];
                const int ex = toBarX(static_cast<double>(ev.elapsed_ms));
                const bool pastDeadline =
                    (ev.elapsed_ms >= static_cast<int64_t>(flashTimeout_ms_));

                if (ev.on)
                {
                    // Strobe ON — green or red triangle pointing up
                    QColor c = pastDeadline ? QColor(220, 40, 40)
                                            : QColor(40, 160, 40);
                    p.setPen(QPen(c, 2));
                    p.setBrush(c);
                    QPolygon tri;
                    tri << QPoint(ex, diagY + 16)
                        << QPoint(ex - 4, diagY + 4)
                        << QPoint(ex + 4, diagY + 4);
                    p.drawPolygon(tri);
                    p.setBrush(Qt::NoBrush);

                    // If there's a matching OFF event, draw a filled bar
                    if (i + 1 < strobeEvents_.size() && !strobeEvents_[i+1].on)
                    {
                        const int offX = toBarX(
                            static_cast<double>(strobeEvents_[i+1].elapsed_ms));
                        p.save();
                        p.setOpacity(0.3);
                        p.fillRect(ex, diagY + 4, offX - ex, 12,
                                    pastDeadline ? QColor(220, 40, 40)
                                                 : QColor(255, 200, 0));
                        p.restore();
                    }
                }
                else
                {
                    // Strobe OFF — down tick.
                    // Cyan for predicted VBlank off, gray for normal callback off.
                    QColor offColor = ev.predicted
                        ? QColor(0, 180, 200)    // cyan — predicted
                        : QColor(100, 100, 100);  // gray — callback
                    p.setPen(QPen(offColor, ev.predicted ? 2 : 1));
                    p.drawLine(ex, diagY + 10, ex, diagY + 18);

                    // Label predicted-off ticks
                    if (ev.predicted) {
                        QFont pf = p.font();
                        pf.setPointSize(5);
                        p.setFont(pf);
                        p.setPen(QColor(0, 180, 200));
                        p.drawText(ex + 2, diagY + 14, 10, 8,
                                   Qt::AlignLeft, "P");
                    }
                }
            }

            // Border
            p.setPen(QColor(180, 180, 180));
            p.drawRect(left, diagY + 2, W, 16);
        }
    }

private:
    static double niceInterval(double raw)
    {
        if (raw <= 0.0) return 1.0;
        const double mag = std::pow(10.0, std::floor(std::log10(raw)));
        const double frac = raw / mag;
        if      (frac < 1.5) return mag;
        else if (frac < 3.5) return 2.0 * mag;
        else if (frac < 7.5) return 5.0 * mag;
        else                 return 10.0 * mag;
    }

    QVector<Row> rows_;
    double       maxUs_    = 1.0;
    QString      subtitle_;

    QVector<StrobeEvent> strobeEvents_;
    double flashTimeout_ms_   = 0.0;
    bool   timeoutExceeded_   = false;

    static constexpr int kLabelW    = 70;
    static constexpr int kRightPad  = 8;
    static constexpr int kRowH      = 24;
    static constexpr int kScaleH    = 22;
    static constexpr int kPadTop    = 4;
    static constexpr int kSubtitleH = 16;
};

// ============================================================================
// Page 2 — Results
// ============================================================================

QWidget* UVBFVBlankDialog::buildResultsPage()
{
    auto* page   = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(tr("<b>VBlank Timing Results</b>")));

    // Column count and headers are set by populateTimingTable() once data arrives.
    timingTable = new QTableWidget(0, 0);
    timingTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    timingTable->horizontalHeader()->setStretchLastSection(true);
    timingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    timingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    timingTable->setAlternatingRowColors(true);
    layout->addWidget(timingTable);

    // ── Frame previews ────────────────────────────────────────────────────────
    auto* previewLabel = new QLabel(tr("<b>Frame Previews</b>  "
        "<span style='color:#666;font-size:10px;'>"
        "Tabs appear as frames are received</span>"));
    layout->addWidget(previewLabel);

    previewTabs = new QTabWidget;
    previewTabs->setMinimumHeight(280);
    layout->addWidget(previewTabs);

    // ── Timing chart ─────────────────────────────────────────────────────────
    auto* chartLabel = new QLabel(tr("<b>Frame Timing Diagram</b>  "
        "<span style='color:#666;font-size:10px;'>"
        "Gray = readout · Green = VBlank window · tick = callback</span>"));
    chartLabel->setWordWrap(false);
    layout->addWidget(chartLabel);

    timingChart = new TimingChart;
    layout->addWidget(timingChart);

    summaryLabel = new QLabel;
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    // Motion verdict banner. Populated by populateMotionVerdict() from the
    // collected per-frame motion data. Hidden when motion check was not
    // requested for this capture, so the page looks unchanged from before
    // for users who don't enable the feature.
    motionVerdictLabel = new QLabel;
    motionVerdictLabel->setWordWrap(true);
    motionVerdictLabel->setAlignment(Qt::AlignCenter);
    motionVerdictLabel->setStyleSheet(
        "QLabel { padding: 8px; border-radius: 4px; "
        "background-color: #ecf0f1; font-weight: bold; }");
    motionVerdictLabel->hide();
    layout->addWidget(motionVerdictLabel);

    auto* btnRow   = new QHBoxLayout;
    exportCsvButton = new QPushButton(tr("⬇ Export CSV…"));
    connect(exportCsvButton, &QPushButton::clicked,
            this, &UVBFVBlankDialog::onExportCsv);
    btnRow->addWidget(exportCsvButton);

    saveAllButton = new QPushButton(tr("💾 Save Complete Results…"));
    connect(saveAllButton, &QPushButton::clicked,
            this, &UVBFVBlankDialog::onSaveCompleteResults);
    btnRow->addWidget(saveAllButton);

    btnRow->addStretch();
    layout->addLayout(btnRow);

    return page;
}

// ============================================================================
// goToPage / updateCameraControls / setStatus
// ============================================================================

void UVBFVBlankDialog::goToPage(int index)
{
    stack->setCurrentIndex(index);
    if (index == 0) {
        cancelButton->setText(tr("Cancel"));
        setMinimumHeight(0);
    } else if (index == 1) {
        cancelButton->setText(tr("Cancel"));
    } else {
        cancelButton->setText(tr("Close"));
        adjustSize();
    }
}

void UVBFVBlankDialog::updateCameraControls(const QString& cam)
{
    bool isIMX708 = (cam == Camera::IMX708);
    if (digitalGainSpinBox) digitalGainSpinBox->setVisible(isIMX708);
    if (digitalGainNote)    digitalGainNote->setVisible(!isIMX708);

    if (analogGainSpinBox) {
        analogGainSpinBox->setRange(1.0, isIMX708 ? 8.57 : 10.66);
        if (analogGainSpinBox->value() > analogGainSpinBox->maximum())
            analogGainSpinBox->setValue(analogGainSpinBox->maximum());
    }

    if (!resolutionCombo)
        return;

    resolutionCombo->blockSignals(true);
    resolutionCombo->clear();

    if (isIMX708) {
        resolutionCombo->addItem(tr("4608 × 2592  (12 MP, full — needs large CMA)"),
                                 QVariantList() << 4608 << 2592);
        resolutionCombo->addItem(tr("2304 × 1296  (3 MP)"),
                                 QVariantList() << 2304 << 1296);
        resolutionCombo->addItem(tr("1920 × 1080  (1080p)"),
                                 QVariantList() << 1920 << 1080);
        resolutionCombo->addItem(tr("1280 × 720   (720p)"),
                                 QVariantList() << 1280 << 720);
        resolutionCombo->addItem(tr("640 × 480    (VGA — most buffers)"),
                                 QVariantList() << 640 << 480);
        resolutionCombo->setCurrentIndex(2);  // default 1080p
    } else {
        resolutionCombo->addItem(tr("3280 × 2464  (8 MP, full — needs large CMA)"),
                                 QVariantList() << 3280 << 2464);
        resolutionCombo->addItem(tr("1920 × 1080  (1080p)"),
                                 QVariantList() << 1920 << 1080);
        resolutionCombo->addItem(tr("1640 × 1232  (2×2 binned — recommended)"),
                                 QVariantList() << 1640 << 1232);
        resolutionCombo->addItem(tr("1280 × 720   (720p)"),
                                 QVariantList() << 1280 << 720);
        resolutionCombo->addItem(tr("640 × 480    (VGA — most buffers)"),
                                 QVariantList() << 640 << 480);
        resolutionCombo->setCurrentIndex(2);  // default 1640×1232
    }

    resolutionCombo->blockSignals(false);
}

void UVBFVBlankDialog::setStatus(const QString& text)
{
    if (statusLabel) statusLabel->setText(text);
}

// ============================================================================
// Settings persistence
// ============================================================================

void UVBFVBlankDialog::loadVBlankConfig()
{
    QSettings s("Sanuwave", "SanuwaveClient");
    s.beginGroup("uvbf_vblank_config");

    // Camera
    if (cameraCombo) {
        const QString cam = s.value("camera", Camera::IMX219).toString();
        const int idx = cameraCombo->findData(cam);
        if (idx >= 0) cameraCombo->setCurrentIndex(idx);
        updateCameraControls(cam);
    }

    // Resolution — find by stored width
    if (resolutionCombo) {
        const int w = s.value("resolution_width", 1640).toInt();
        for (int i = 0; i < resolutionCombo->count(); ++i) {
            const QVariantList dims = resolutionCombo->itemData(i).toList();
            if (dims.size() >= 1 && dims[0].toInt() == w) {
                resolutionCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    if (exposureSpinBox)
        exposureSpinBox->setValue(s.value("exposure_us", 20000).toInt());
    if (analogGainSpinBox)
        analogGainSpinBox->setValue(s.value("analog_gain", 1.0).toDouble());
    if (digitalGainSpinBox)
        digitalGainSpinBox->setValue(s.value("digital_gain", 1.0).toDouble());
    if (brightnessSpinBox)
        brightnessSpinBox->setValue(s.value("led_brightness", 200).toInt());
    if (predictVBlankCheck)
        predictVBlankCheck->setChecked(s.value("predict_vblank", false).toBool());
    if (kernelStrobeCheck)
        kernelStrobeCheck->setChecked(s.value("kernel_strobe", false).toBool());
    if (motionCheckCheck)
        motionCheckCheck->setChecked(s.value("motion_check", false).toBool());

    for (int i = 0; i < 32; ++i) {
        if (ledCheckBoxes[i])
            ledCheckBoxes[i]->setChecked(
                s.value(QString("led_%1").arg(i), true).toBool());
    }

    s.endGroup();
}

void UVBFVBlankDialog::saveVBlankConfig()
{
    QSettings s("Sanuwave", "SanuwaveClient");
    s.beginGroup("uvbf_vblank_config");

    if (cameraCombo)
        s.setValue("camera", cameraCombo->currentData().toString());

    if (resolutionCombo) {
        const QVariantList dims = resolutionCombo->currentData().toList();
        if (dims.size() >= 2) {
            s.setValue("resolution_width",  dims[0].toInt());
            s.setValue("resolution_height", dims[1].toInt());
        }
    }

    if (exposureSpinBox)   s.setValue("exposure_us",     exposureSpinBox->value());
    if (analogGainSpinBox) s.setValue("analog_gain",     analogGainSpinBox->value());
    if (digitalGainSpinBox)s.setValue("digital_gain",    digitalGainSpinBox->value());
    if (brightnessSpinBox) s.setValue("led_brightness",  brightnessSpinBox->value());
    if (predictVBlankCheck) s.setValue("predict_vblank", predictVBlankCheck->isChecked());
    if (kernelStrobeCheck)  s.setValue("kernel_strobe",  kernelStrobeCheck->isChecked());
    if (motionCheckCheck)   s.setValue("motion_check",   motionCheckCheck->isChecked());

    for (int i = 0; i < 32; ++i) {
        if (ledCheckBoxes[i])
            s.setValue(QString("led_%1").arg(i), ledCheckBoxes[i]->isChecked());
    }

    s.endGroup();
}

// ============================================================================
// onCameraChanged
// ============================================================================

void UVBFVBlankDialog::onCameraChanged(int /*index*/)
{
    updateCameraControls(cameraCombo->currentData().toString());
}

// ============================================================================
// onStartClicked — send LED_SELECT then UVBF_VBLANK_CAPTURE
// ============================================================================

void UVBFVBlankDialog::onStartClicked()
{
    if (!connection)
        return;

    saveVBlankConfig();

    // Build LED selection arrays
    QJsonArray ids, brightnesses;
    for (int i = 0; i < 32; ++i) {
        if (ledCheckBoxes[i] && ledCheckBoxes[i]->isChecked()) {
            ids.append(i);
            brightnesses.append(brightnessSpinBox->value());
        }
    }

    if (ids.isEmpty()) {
        setStatus(tr("Select at least one LED before starting."));
        // Stay on config page — don't proceed
        return;
    }

    // Arm LEDs
    QJsonObject ledCmd;
    ledCmd[Param::COMMAND]          = Command::LED_SELECT;
    ledCmd[Param::LED_IDS]          = ids;
    ledCmd[Param::LED_BRIGHTNESSES] = brightnesses;
    connection->sendCommand(ledCmd);

    // Generate session ID
    sessionId = QString("vblank_%1")
                    .arg(QDateTime::currentDateTime()
                             .toString("yyyyMMdd_HHmmsszzz"));

    const QString camera = cameraCombo->currentData().toString();
    capturedCamera = camera;

    QVariantList dims = resolutionCombo->currentData().toList();
    const int width  = dims.size() >= 2 ? dims[0].toInt() : 1640;
    const int height = dims.size() >= 2 ? dims[1].toInt() : 1232;

    // Send capture command
    QJsonObject cmd;
    cmd[Param::COMMAND]          = Command::UVBF_VBLANK_CAPTURE;
    cmd[UVBFParam::CAMERA]       = camera;
    cmd[UVBFParam::EXPOSURE_US]  = exposureSpinBox->value();
    cmd[UVBFParam::ANALOG_GAIN]  = analogGainSpinBox->value();
    cmd[UVBFParam::DIGITAL_GAIN] = digitalGainSpinBox->value();
    cmd[Param::WIDTH]            = width;
    cmd[Param::HEIGHT]           = height;
    cmd[Param::SESSION_ID]       = sessionId;
    if (predictVBlankCheck && predictVBlankCheck->isChecked())
        cmd["predict_vblank"]    = true;
    if (kernelStrobeCheck && kernelStrobeCheck->isChecked())
        cmd["kernel_strobe"]     = true;
    motionCheckRequested = motionCheckCheck && motionCheckCheck->isChecked();
    cmd[Param::UVBF_MOTION_CHECK] = motionCheckRequested ? "true" : "false";
    connection->sendCommand(cmd);

    goToPage(1);
    setStatus(tr("Command sent — waiting for server…"));
}

// ============================================================================
// onCancelClicked
// ============================================================================

void UVBFVBlankDialog::onCancelClicked()
{
    reject();
}

// ============================================================================
// Slots — server events
// ============================================================================

void UVBFVBlankDialog::onVBlankStarted(int frameCount, const QStringList& roles)
{
    expectedFrameCount = frameCount;
    frameRoles         = roles;
    framesCaptured     = 0;
    framesReceived     = 0;

    // Clear all state from a previous run.  Delete tab page widgets
    // (removeTab alone does NOT delete them, leaving dangling QPointers
    // and leaked memory) and flush stale temp file paths / button maps.
    frameViewers.clear();
    frameInfos.clear();
    tempFilePaths.clear();
    frameSaveDngButtons.clear();
    frameSavePngButtons.clear();
    frameMotions.clear();
    pendingPreviews = 0;
    if (previewTabs) {
        while (previewTabs->count() > 0) {
            QWidget* w = previewTabs->widget(0);
            previewTabs->removeTab(0);
            delete w;
        }
    }

    captureBar->setRange(0, frameCount);
    captureBar->setValue(0);
    transferBar->setRange(0, frameCount);
    transferBar->setValue(0);
    captureCountLabel->setText(tr("0 / %1").arg(frameCount));
    transferCountLabel->setText(tr("0 / %1").arg(frameCount));

    setStatus(tr("Capturing %1 frames…").arg(frameCount));
}

void UVBFVBlankDialog::onVBlankFrameCaptured(const QString& role)
{
    ++framesCaptured;
    captureBar->setValue(framesCaptured);
    captureCountLabel->setText(
        tr("%1 / %2  (last: %3)")
            .arg(framesCaptured)
            .arg(expectedFrameCount)
            .arg(role));
    setStatus(tr("Captured %1  (%2 / %3)…")
                  .arg(role)
                  .arg(framesCaptured)
                  .arg(expectedFrameCount));
}

void UVBFVBlankDialog::onFrameTransferComplete(const UVBFFrameInfo& frameInfo,
                                                const QByteArray&   dngData)
{
    if (frameInfo.sessionId != sessionId)
        return;

    const QString& role = frameInfo.role;

    if (dngData.isEmpty()) {
        setStatus(tr("Warning: empty payload for role %1 — skipped.").arg(role));
        return;
    }

    const QString path = tempFilePathForRole(sessionId, role);
    if (!writeFrameToTempFile(path, dngData)) {
        setStatus(tr("Error: failed to write temp file for %1").arg(role));
        return;
    }
    tempFilePaths[role] = path;
    frameInfos[role]    = frameInfo.imageInfo;
    // Capture motion measurement if the server emitted one for this frame.
    // motion.valid == false (default) when the sub-object was absent, so
    // storing unconditionally is harmless and keeps the lookup uniform.
    frameMotions[role]  = frameInfo.motion;

    ++framesReceived;
    transferBar->setValue(framesReceived);
    transferCountLabel->setText(
        tr("%1 / %2  (last: %3, %4 B)")
            .arg(framesReceived)
            .arg(expectedFrameCount)
            .arg(role)
            .arg(dngData.size()));
    setStatus(tr("Received %1  (%2 / %3)…")
                  .arg(role)
                  .arg(framesReceived)
                  .arg(expectedFrameCount));

    // ── Create preview tab ────────────────────────────────────────────────────
    if (previewTabs) {
        const QString tabName = displayRole(role);

        auto* container = new QWidget;
        auto* vl        = new QVBoxLayout(container);
        vl->setContentsMargins(4, 4, 4, 4);
        vl->setSpacing(4);

        auto* viewer = new ZoomImageWidget;
        vl->addWidget(viewer, 1);
        frameViewers[role] = viewer;

        auto* btnRow     = new QHBoxLayout;
        auto* saveDngBtn = new QPushButton(tr("💾  Save DNG…"));
        auto* savePngBtn = new QPushButton(tr("🖼  Save PNG…"));
        saveDngBtn->setMaximumWidth(160);
        savePngBtn->setMaximumWidth(160);
        // Disabled until the temp file write succeeds (see below).
        saveDngBtn->setEnabled(false);
        savePngBtn->setEnabled(false);
        btnRow->addStretch();
        btnRow->addWidget(saveDngBtn);
        btnRow->addWidget(savePngBtn);
        vl->addLayout(btnRow);

        frameSaveDngButtons[role] = saveDngBtn;
        frameSavePngButtons[role] = savePngBtn;

        // ── Save DNG ──────────────────────────────────────────────────────────
        connect(saveDngBtn, &QPushButton::clicked, this,
                [this, role, tabName]() {
            const QString tempPath = tempFilePaths.value(role);
            if (tempPath.isEmpty()) {
                QMessageBox::warning(this, tr("No Data"),
                    tr("No frame data available for %1.").arg(tabName));
                return;
            }
            const QString savePath = QFileDialog::getSaveFileName(
                this,
                tr("Save %1 DNG").arg(tabName),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                    + QString("/%1_%2.dng").arg(sessionId, role),
                tr("DNG files (*.dng)"));
            if (savePath.isEmpty()) return;

            QByteArray payload = readFrameFromTempFile(tempPath);
            if (payload.isEmpty()) {
                QMessageBox::critical(this, tr("Read Error"),
                    tr("Failed to read temp file for %1.").arg(tabName));
                return;
            }

            size_t headerLen = 0;
            sanuwave::RawImageInfo parsedInfo;
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(payload.constData());
            if (!sanuwave::RawBayerDecoder::parseHeader(bytes, payload.size(),
                                                        parsedInfo, headerLen)) {
                QMessageBox::critical(this, tr("DNG Error"),
                    tr("Failed to parse frame header for %1.").arg(tabName));
                return;
            }
            const sanuwave::RawImageInfo& stored = frameInfos.value(role);
            if (stored.width > 0) parsedInfo = stored;

            sanuwave::RawImageData raw = sanuwave::DngExporter::buildFromCapture(
                bytes + headerLen,
                static_cast<size_t>(payload.size()) - headerLen,
                parsedInfo,
                capturedCamera.toStdString());

            QString errMsg;
            if (!sanuwave::DngExporter::writeDng(savePath, raw, errMsg))
                QMessageBox::critical(this, tr("Save Failed"),
                    tr("Failed to write DNG for %1:\n%2").arg(tabName, errMsg));
        });

        // ── Save PNG ──────────────────────────────────────────────────────────
        connect(savePngBtn, &QPushButton::clicked, this,
                [this, role, tabName]() {
            const QString tempPath = tempFilePaths.value(role);
            if (tempPath.isEmpty()) {
                QMessageBox::warning(this, tr("No Data"),
                    tr("No frame data available for %1.").arg(tabName));
                return;
            }
            const QString savePath = QFileDialog::getSaveFileName(
                this,
                tr("Save %1 PNG").arg(tabName),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                    + QString("/%1_%2.png").arg(sessionId, role),
                tr("PNG files (*.png)"));
            if (savePath.isEmpty()) return;

            QByteArray payload = readFrameFromTempFile(tempPath);
            if (payload.isEmpty()) {
                QMessageBox::critical(this, tr("Read Error"),
                    tr("Failed to read temp file for %1.").arg(tabName));
                return;
            }

            size_t headerLen = 0;
            sanuwave::RawImageInfo info;
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(payload.constData());
            if (!sanuwave::RawBayerDecoder::parseHeader(bytes, payload.size(),
                                                        info, headerLen)) {
                QMessageBox::critical(this, tr("Decode Error"),
                    tr("Failed to parse frame header for %1.").arg(tabName));
                return;
            }

            std::vector<uint8_t> rgb = sanuwave::RawBayerDecoder::decode(
                bytes + headerLen,
                static_cast<size_t>(payload.size()) - headerLen,
                info);
            if (rgb.empty()) {
                QMessageBox::critical(this, tr("Decode Error"),
                    tr("Failed to decode raw frame for %1.").arg(tabName));
                return;
            }

            QImage img = QImage(rgb.data(), info.width, info.height,
                                info.width * 3, QImage::Format_RGB888).copy();
            if (!img.save(savePath, "PNG"))
                QMessageBox::warning(this, tr("Save Failed"),
                    tr("Failed to save PNG to:\n%1").arg(savePath));
        });

        previewTabs->addTab(container, tabName);

        // ── Decode preview asynchronously ─────────────────────────────────────
        // Use QPointer for both the viewer and this dialog so the background
        // thread's completion callback is safe if the dialog is closed while
        // previews are still decoding.
        QPointer<ZoomImageWidget>  safeViewer = viewer;
        QPointer<UVBFVBlankDialog> safeDialog = this;
        ++pendingPreviews;
        auto future = QtConcurrent::run([safeDialog, safeViewer, payload = dngData]() mutable {
            QPixmap px = sanuwave::RawBayerDecoder::previewFromRawPayload(payload, 4);
            payload.clear();
            if (!px.isNull() && px.width() > 300)
                px = px.scaledToWidth(300, Qt::SmoothTransformation);
            if (px.isNull()) {
                px = QPixmap(300, 225);
                px.fill(Qt::darkGray);
            }
            QMetaObject::invokeMethod(qApp, [safeDialog, safeViewer, px]() {
                if (safeViewer) safeViewer->setPixmap(px);
                if (safeDialog) --safeDialog->pendingPreviews;
            }, Qt::QueuedConnection);
        });
    }

    // Enable Save buttons now that the temp file is confirmed written.
    // These are created inside the previewTabs block above; guard in case
    // previewTabs was null and the buttons were never constructed.
    if (frameSaveDngButtons.contains(role))
        frameSaveDngButtons[role]->setEnabled(true);
    if (frameSavePngButtons.contains(role))
        frameSavePngButtons[role]->setEnabled(true);
}

void UVBFVBlankDialog::onVBlankComplete(const QJsonObject& summary)
{
    captureComplete = true;

    const double   vblankEstimate_us  = summary["vblank_estimate_us"].toDouble();
    const double   rollingShutter_us  = summary["rolling_shutter_us"].toDouble();
    const int      reportedFrameCount = summary["frame_count"].toInt();
    const QJsonArray framesJson       = summary["frames"].toArray();

    QVector<VBlankFrameTiming> frames;
    frames.reserve(framesJson.size());
    for (const QJsonValue& v : framesJson) {
        QJsonObject obj = v.toObject();
        VBlankFrameTiming t;
        t.role             = obj["role"].toString();
        t.ledsOn           = obj["leds_on"].toBool();
        t.sensorTs_ns      = obj["sensor_ts_ns"].toString().toLongLong();
        if (t.sensorTs_ns == 0) {
            // Fallback: server sent a JSON number rather than a quoted string.
            // toDouble() loses sub-microsecond precision on large boot timestamps
            // (~1e15 ns) but is better than zero.
            t.sensorTs_ns = static_cast<qint64>(obj["sensor_ts_ns"].toDouble());
        }
        t.callbackDelta_us = obj["callback_delta_us"].toDouble();
        t.frameDur_us      = obj["frame_dur_us"].toDouble();
        frames.append(t);
    }

    // ── Strobe diagnostics ──────────────────────────────────────────
    const double  flashTimeout_ms   = summary["flash_timeout_ms"].toDouble();
    const int64_t lastStrobeOn_ms   = static_cast<int64_t>(
        summary["arm_elapsed_at_last_strobe_ms"].toDouble());
    const bool    timeoutExceeded   = summary["timeout_exceeded"].toBool();

    QVector<TimingChart::StrobeEvent> strobeEvents;
    const QJsonArray strobeJson = summary["strobe_events"].toArray();
    for (const QJsonValue& v : strobeJson) {
        QJsonObject obj = v.toObject();
        TimingChart::StrobeEvent ev;
        ev.on         = obj["on"].toBool();
        ev.elapsed_ms = static_cast<int64_t>(obj["elapsed_ms"].toDouble());
        ev.predicted  = obj["predicted"].toBool();
        strobeEvents.append(ev);
    }

    goToPage(2);

    // Fold per-frame motion data (collected via onFrameTransferComplete)
    // into the timing frames before display. Map by role since the
    // VBlank summary frames and the per-frame transfers arrive on
    // different code paths but share role identifiers.
    for (auto& f : frames)
    {
        auto it = frameMotions.find(f.role);
        if (it == frameMotions.end()) continue;
        const UVBFFrameInfo::Motion& m = it.value();
        if (!m.valid) continue;
        f.motionValid       = true;
        f.prevTransPx       = m.prevTransPx;
        f.prevConfidence    = m.prevConfidence;
        f.anchorTransPx     = m.anchorTransPx;
        f.anchorConfidence  = m.anchorConfidence;
    }

    capturedFrames              = frames;
    capturedVBlankEstimate_us   = vblankEstimate_us;
    capturedRollingShutter_us   = rollingShutter_us;
    capturedCommandedExposure_us = summary["commanded_exposure_us"].toInt();
    capturedCommandedAnalogGain  = summary["commanded_analog_gain"].toDouble();
    capturedPredictVBlank        = summary["predict_vblank"].toBool();
    capturedKernelStrobe         = summary["kernel_strobe"].toBool();
    populateTimingTable(frames, vblankEstimate_us, rollingShutter_us);
    populateMotionVerdict(frames);

    if (timingChart && flashTimeout_ms > 0.0)
        timingChart->setStrobeDiagnostics(strobeEvents,
                                          flashTimeout_ms,
                                          timeoutExceeded);

    const double summaryFramePeriod_us = rollingShutter_us + vblankEstimate_us;
    int inVBlank = 0;
    for (const auto& f : frames) {
        const double upperBound = f.frameDur_us > 0.0
                                  ? f.frameDur_us
                                  : summaryFramePeriod_us;
        if (f.callbackDelta_us >= rollingShutter_us &&
            f.callbackDelta_us <  upperBound)
            ++inVBlank;
    }
    const QString verdict =
        (inVBlank == reportedFrameCount)
            ? tr("✓ ALL %1 callbacks landed inside VBlank — LED toggling without a prime frame appears feasible.")
                  .arg(inVBlank)
        : (inVBlank > 0)
            ? tr("⚠ %1 / %2 callbacks landed inside VBlank — marginal; review individual rows.")
                  .arg(inVBlank).arg(reportedFrameCount)
            : tr("✗ No callbacks landed inside VBlank — VBlank LED toggling is NOT viable on this configuration.");

    // Compute median VBlank gap from per-frame data (more accurate than
    // server's vblank_estimate_us which averages over slow startup frames).
    QVector<double> gaps;
    for (const auto& f : frames) {
        const double upper = f.frameDur_us > 0.0 ? f.frameDur_us : summaryFramePeriod_us;
        gaps.append(upper - rollingShutter_us);
    }
    std::sort(gaps.begin(), gaps.end());
    const double medianGap_us = gaps.isEmpty() ? 0.0 : gaps[gaps.size() / 2];

    QString timeoutWarning;
    if (timeoutExceeded) {
        timeoutWarning = tr(
            "<br><span style='color:red; font-weight:bold;'>"
            "⚠ FLASH TIMEOUT EXCEEDED — last strobeOn at %1 ms "
            "vs %2 ms hardware limit.  Later illuminated frames "
            "may be dark.</span>")
            .arg(lastStrobeOn_ms)
            .arg(static_cast<int>(flashTimeout_ms));
    }

    const bool predictVBlankUsed = summary["predict_vblank"].toBool();
    const bool kernelStrobeUsed = summary["kernel_strobe"].toBool();
    const QString strobeMode    = summary["strobe_mode"].toString();
    QString modeNote;
    if (kernelStrobeUsed)
        modeNote = tr("<br><span style='color:#00c853; font-weight:bold;'>"
                      "⚡ Kernel ISR strobe active — LED gating locked to frame boundaries</span>");
    else if (predictVBlankUsed)
        modeNote = tr("<br><span style='color:#00b4c8;'>"
                      "⏱ Predicted VBlank strobeOff active — LED off after one frame period</span>");

    const int    commandedExposure_us = summary["commanded_exposure_us"].toInt();
    const double commandedAnalogGain  = summary["commanded_analog_gain"].toDouble();

    summaryLabel->setText(
        tr("<b>Exposure:</b> %1 µs  |  "
           "<b>Analog Gain:</b> %2  |  "
           "<b>Strobe mode:</b> %3  |  "
           "<b>VBlank gap (median):</b> %4 µs  |  "
           "<b>Rolling shutter:</b> %5 µs  |  "
           "<b>Frames:</b> %6<br>%7%8%9")
            .arg(commandedExposure_us)
            .arg(commandedAnalogGain, 0, 'f', 1)
            .arg(kernelStrobeUsed ? tr("kernel ISR") : (predictVBlankUsed ? tr("predict VBlank") : tr("callback")))
            .arg(medianGap_us,       0, 'f', 1)
            .arg(rollingShutter_us,  0, 'f', 1)
            .arg(reportedFrameCount)
            .arg(verdict)
            .arg(timeoutWarning)
            .arg(modeNote));
}

void UVBFVBlankDialog::onVBlankError(const QString& reason)
{
    setStatus(tr("Error: %1").arg(reason));
    cancelButton->setText(tr("Close"));
    captureComplete = true;
    // Stay on progress page so the error message is visible
}

// ============================================================================
// displayRole — GUI-friendly role name (replaces "dark" with "Ambient")
// ============================================================================

static QString displayRole(const QString& role)
{
    // Replace "dark" anywhere in the role string with "ambient" for display.
    QString s = role;
    s.replace("dark", "ambient", Qt::CaseInsensitive);
    return s;
}

// ============================================================================
// populateTimingTable
// ============================================================================

void UVBFVBlankDialog::populateTimingTable(
        const QVector<VBlankFrameTiming>& frames,
        double vblankEstimate_us,
        double rollingShutter_us)
{
    // Expand to 11 columns: 9 timing columns + 2 motion-drift columns.
    // Motion columns stay empty (no text) for frames that have no
    // measurement (dark frames, illum_1, motion check disabled).
    timingTable->setColumnCount(11);
    timingTable->setHorizontalHeaderLabels({
        tr("Frame"),
        tr("Role"),
        tr("LEDs"),
        tr("Timestamp (rel. µs)"),
        tr("Callback Delta (µs)"),
        tr("VBlank Gap (µs)"),
        tr("Rolling Shutter (µs)"),
        tr("Frame Period (µs)"),
        tr("In VBlank?"),
        tr("Drift prev (px)"),
        tr("Drift anchor (px)"),
    });

    // Tooltips on column headers
    const QStringList tips = {
        tr("Frame index"),
        tr("Frame role in the burst sequence"),
        tr("Whether LEDs were on during this frame"),
        tr("Sensor timestamp relative to the first frame"),
        tr("Time from frame start until requestCompleted fired (µs).\n"
           "Must be between Rolling Shutter and Frame Period to land inside VBlank."),
        tr("Computed VBlank gap = Frame Period − Rolling Shutter (µs).\n"
           "This is the window available for LED toggling between frames."),
        tr("Rolling shutter readout duration (µs).\n"
           "This is the lower bound — the callback must fire after this."),
        tr("Actual frame period reported by libcamera (µs).\n"
           "This is the upper bound — the callback must fire before this."),
        tr("YES if the callback landed inside the VBlank window\n"
           "(Rolling Shutter ≤ Callback Delta < Frame Period)"),
        tr("Phase-correlation drift from the PREVIOUS illum frame (px).\n"
           "Inter-frame jitter. Blank for dark frames, illum_1, and when\n"
           "motion check was disabled."),
        tr("Phase-correlation drift from the FIRST illum frame (px).\n"
           "Cumulative drift from the start of illumination. Blank for\n"
           "dark frames, illum_1, and when motion check was disabled."),
    };
    for (int c = 0; c < tips.size(); ++c)
        timingTable->horizontalHeaderItem(c)->setToolTip(tips[c]);

    timingTable->setRowCount(frames.size());

    // Base timestamp for relative display
    const qint64 baseTs_ns = frames.isEmpty() ? 0 : frames[0].sensorTs_ns;

    // Fallback frame period from summary when per-frame value is unavailable
    const double summaryFramePeriod_us = rollingShutter_us + vblankEstimate_us;

    for (int row = 0; row < frames.size(); ++row) {
        const VBlankFrameTiming& f = frames[row];

        auto cell = [&](int col, const QString& text,
                         Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter)
        {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(align);
            timingTable->setItem(row, col, item);
            return item;
        };

        const Qt::Alignment R = Qt::AlignRight | Qt::AlignVCenter;

        cell(0, QString::number(row), Qt::AlignCenter);
        cell(1, displayRole(f.role));

        auto* ledsItem = cell(2, f.ledsOn ? tr("ON") : tr("off"), Qt::AlignCenter);
        if (f.ledsOn) ledsItem->setForeground(Qt::darkGreen);

        // Relative sensor timestamp in µs
        const double relTs_us = (f.sensorTs_ns - baseTs_ns) / 1000.0;
        cell(3, QString::number(relTs_us, 'f', 1), R);

        cell(4, QString::number(f.callbackDelta_us, 'f', 1), R);

        // frame_dur_us from server = libcamera FrameDuration = actual frame period
        // Upper bound: callback must fire before the next frame starts.
        // Lower bound: rolling shutter duration (readout complete).
        // Fall back to summaryFramePeriod_us when frame_dur_us is 0 (first frame).
        const double upperBound = f.frameDur_us > 0.0
                                  ? f.frameDur_us
                                  : summaryFramePeriod_us;

        // Computed VBlank gap = frame period - rolling shutter
        const double vblankGap_us = upperBound - rollingShutter_us;
        cell(5, QString::number(vblankGap_us, 'f', 1), R);

        cell(6, QString::number(rollingShutter_us, 'f', 1), R);  // lower bound
        cell(7, f.frameDur_us > 0.0
                    ? QString::number(f.frameDur_us, 'f', 1)
                    : tr("— (%1)").arg(summaryFramePeriod_us, 0, 'f', 1),
             R);  // upper bound

        // In VBlank: rolling_shutter_us <= callback_delta_us < frame_dur_us
        const bool inVBlank = (f.callbackDelta_us >= rollingShutter_us &&
                               f.callbackDelta_us <  upperBound);
        auto* vbItem = cell(8, inVBlank ? tr("YES") : tr("no"), Qt::AlignCenter);
        vbItem->setForeground(inVBlank ? Qt::darkGreen : Qt::red);
        QFont font = vbItem->font();
        font.setBold(inVBlank);
        vbItem->setFont(font);

        // Drift columns: empty unless server returned a valid motion
        // measurement for this row. Two decimals matches the chart label
        // format used in the streaming motion indicator.
        if (f.motionValid)
        {
            cell(9,  QString::number(f.prevTransPx,   'f', 2), R);
            cell(10, QString::number(f.anchorTransPx, 'f', 2), R);
        }
        else
        {
            cell(9,  QString(), R);
            cell(10, QString(), R);
        }
    }

    // ── Populate chart ────────────────────────────────────────────────────────
    if (timingChart) {
        QVector<TimingChart::Row> chartRows;
        chartRows.reserve(frames.size());
        for (int i = 0; i < frames.size(); ++i) {
            const VBlankFrameTiming& f = frames[i];
            TimingChart::Row r;
            r.role         = f.role;
            r.displayRole  = displayRole(f.role);
            r.ledsOn       = f.ledsOn;
            r.rollingShutter_us = rollingShutter_us;
            r.frameDur_us  = f.frameDur_us > 0.0
                             ? f.frameDur_us : summaryFramePeriod_us;
            r.callbackDelta_us = f.callbackDelta_us;
            const double upper = r.frameDur_us;
            r.inVBlank     = (f.callbackDelta_us >= rollingShutter_us &&
                              f.callbackDelta_us <  upper);

            // For illuminated frames, compute when the LED turned on relative
            // to this frame's start.
            if (f.ledsOn && i > 0) {
                const VBlankFrameTiming& prev = frames[i - 1];
                const double prevFrameDur = prev.frameDur_us > 0.0
                                           ? prev.frameDur_us : summaryFramePeriod_us;
                r.ledOnOffset_us = prev.callbackDelta_us - prevFrameDur;
            }

            // For ambient frames preceding an illuminated frame, mark where
            // the LED turns on (= this frame's callback) so we can draw the
            // amber tail from the callback to the end of the frame bar.
            if (!f.ledsOn && i + 1 < frames.size() && frames[i + 1].ledsOn) {
                r.ledOnTail_us = f.callbackDelta_us;
            }

            chartRows.append(r);
        }
        timingChart->setRows(chartRows,
            QString("%1  ·  %2 × %3")
                .arg(capturedCamera.isEmpty() ? tr("Unknown camera") : capturedCamera)
                .arg(capturedFrames.isEmpty() ? 0 : frameInfos.value(capturedFrames[0].role).width)
                .arg(capturedFrames.isEmpty() ? 0 : frameInfos.value(capturedFrames[0].role).height));
    }
}

// ============================================================================
// populateMotionVerdict
//
// Render a PASS / MARGINAL / FAIL / INCONCLUSIVE banner based on the worst-
// case drift across all illum frames in the burst. Thresholds are client-
// side and tunable via QSettings under [motion]; defaults mirror those used
// in the standard UVBF capture dialog so the operator has a consistent
// frame of reference.
//
// Hidden entirely when motion check was not requested for this capture
// (motionCheckRequested == false). When requested but the server returned
// no valid measurements for any frame, shown as a neutral "not available"
// banner so the operator knows the request was honored even if the data
// wasn't usable.
// ============================================================================

void UVBFVBlankDialog::populateMotionVerdict(const QVector<VBlankFrameTiming>& frames)
{
    if (!motionVerdictLabel) return;

    if (!motionCheckRequested)
    {
        motionVerdictLabel->hide();
        return;
    }

    QSettings settings("Sanuwave", "SanuwaveClient");
    const double maxDriftPx =
        settings.value("motion/uvbfMaxDriftPx", 3.0).toDouble();
    const double warnDriftPx =
        settings.value("motion/uvbfWarnDriftPx", 1.5).toDouble();
    const double minConf =
        settings.value("motion/uvbfMinConfidence", 0.05).toDouble();

    // Scan all rows for valid measurements. Worst-case across BOTH prev
    // and anchor drives the verdict; anchor on the last illum captures
    // cumulative drift while prev captures frame-to-frame jitter, and
    // either failing is enough to flag the capture.
    double worstDrift = 0.0;
    bool   anyValid = false;
    bool   anyLowConfidence = false;
    int    validCount = 0;
    for (const auto& f : frames) {
        if (!f.motionValid) continue;
        anyValid = true;
        ++validCount;
        worstDrift = std::max({worstDrift, f.prevTransPx, f.anchorTransPx});
        if (f.prevConfidence < minConf || f.anchorConfidence < minConf)
            anyLowConfidence = true;
    }

    if (!anyValid)
    {
        motionVerdictLabel->setText(tr(
            "Motion check: <b>not available</b><br>"
            "<span style='font-weight: normal; font-size: 10px;'>"
            "Server did not return inter-frame motion measurements for any "
            "frame in this capture."
            "</span>"));
        motionVerdictLabel->setStyleSheet(
            "QLabel { padding: 8px; border-radius: 4px; "
            "background-color: #ecf0f1; color: #555; }");
        motionVerdictLabel->show();
        return;
    }

    QString verdictText;
    QString bgColor;
    if (anyLowConfidence)
    {
        verdictText = tr(
            "Motion check: <b>INCONCLUSIVE</b><br>"
            "<span style='font-weight: normal; font-size: 10px;'>"
            "Phase-correlation confidence was low on at least one frame "
            "(scene may lack texture). Worst drift across %1 measured "
            "frames: %2 px."
            "</span>")
            .arg(validCount)
            .arg(worstDrift, 0, 'f', 2);
        bgColor = "#f39c12";   // amber
    }
    else if (worstDrift >= maxDriftPx)
    {
        verdictText = tr(
            "Motion check: <b>FAIL</b><br>"
            "<span style='font-weight: normal; font-size: 10px;'>"
            "Worst drift %1 px across %2 measured frames exceeds threshold "
            "%3 px. Consider retaking the capture."
            "</span>")
            .arg(worstDrift, 0, 'f', 2)
            .arg(validCount)
            .arg(maxDriftPx, 0, 'f', 1);
        bgColor = "#c0392b";   // red
    }
    else if (worstDrift >= warnDriftPx)
    {
        verdictText = tr(
            "Motion check: <b>MARGINAL</b><br>"
            "<span style='font-weight: normal; font-size: 10px;'>"
            "Worst drift %1 px across %2 measured frames (warn ≥ %3 px, "
            "fail ≥ %4 px)."
            "</span>")
            .arg(worstDrift, 0, 'f', 2)
            .arg(validCount)
            .arg(warnDriftPx, 0, 'f', 1)
            .arg(maxDriftPx, 0, 'f', 1);
        bgColor = "#f39c12";   // amber
    }
    else
    {
        verdictText = tr(
            "Motion check: <b>PASS</b><br>"
            "<span style='font-weight: normal; font-size: 10px;'>"
            "Worst drift %1 px across %2 measured frames (well under "
            "%3 px threshold)."
            "</span>")
            .arg(worstDrift, 0, 'f', 2)
            .arg(validCount)
            .arg(maxDriftPx, 0, 'f', 1);
        bgColor = "#27ae60";   // green
    }

    motionVerdictLabel->setText(verdictText);
    motionVerdictLabel->setStyleSheet(
        QString("QLabel { padding: 8px; border-radius: 4px; "
                "background-color: %1; color: white; font-weight: bold; }")
            .arg(bgColor));
    motionVerdictLabel->show();
}

// ============================================================================
// onExportCsv
// ============================================================================

void UVBFVBlankDialog::onExportCsv()
{
    if (capturedFrames.isEmpty())
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Timing CSV"), QString(),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Failed"),
            tr("Could not open file for writing:\n%1").arg(path));
        return;
    }

    QTextStream out(&f);

    // Header
    out << "# commanded_exposure_us=" << capturedCommandedExposure_us
        << ",commanded_analog_gain=" << QString::number(capturedCommandedAnalogGain, 'f', 1)
        << ",predict_vblank=" << (capturedPredictVBlank ? "true" : "false")
        << ",kernel_strobe=" << (capturedKernelStrobe ? "true" : "false")
        << "\n";
    out << "session_id,frame,role,leds_on,sensor_ts_ns,sensor_ts_rel_us,"
           "callback_delta_us,vblank_estimate_us,rolling_shutter_us,frame_dur_us,"
           "in_vblank\n";

    const qint64 baseTs_ns             = capturedFrames[0].sensorTs_ns;
    const double summaryFramePeriod_us = capturedRollingShutter_us
                                         + capturedVBlankEstimate_us;

    for (int i = 0; i < capturedFrames.size(); ++i) {
        const VBlankFrameTiming& fr = capturedFrames[i];
        const double relTs_us  = (fr.sensorTs_ns - baseTs_ns) / 1000.0;
        const double upperBound = fr.frameDur_us > 0.0
                                  ? fr.frameDur_us : summaryFramePeriod_us;
        const bool inVBlank = (fr.callbackDelta_us >= capturedRollingShutter_us &&
                               fr.callbackDelta_us <  upperBound);

        out << sessionId << ","
            << i << ","
            << fr.role << ","
            << (fr.ledsOn ? "1" : "0") << ","
            << fr.sensorTs_ns << ","
            << QString::number(relTs_us,                  'f', 1) << ","
            << QString::number(fr.callbackDelta_us,       'f', 1) << ","
            << QString::number(capturedVBlankEstimate_us, 'f', 1) << ","
            << QString::number(capturedRollingShutter_us, 'f', 1) << ","
            << QString::number(upperBound,                'f', 1) << ","
            << (inVBlank ? "1" : "0") << "\n";
    }

    f.close();
}

// ============================================================================
// onSaveCompleteResults — save everything to a single folder
// ============================================================================

void UVBFVBlankDialog::onSaveCompleteResults()
{
    if (capturedFrames.isEmpty()) {
        QMessageBox::information(this, tr("No Data"),
            tr("No capture data to save."));
        return;
    }

    // Ask user for a parent directory
    const QString parentDir = QFileDialog::getExistingDirectory(
        this, tr("Select Folder for Results"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (parentDir.isEmpty())
        return;

    // Create a subfolder named by session ID
    const QString folderName = sessionId;
    QDir parent(parentDir);
    if (!parent.mkpath(folderName)) {
        QMessageBox::critical(this, tr("Folder Error"),
            tr("Could not create folder:\n%1/%2").arg(parentDir, folderName));
        return;
    }
    const QString outDir = parent.filePath(folderName);

    int savedDng = 0;
    int savedPng = 0;
    QStringList errors;

    // ── Save DNG + PNG for each frame ────────────────────────────────────────
    for (auto it = tempFilePaths.constBegin(); it != tempFilePaths.constEnd(); ++it)
    {
        const QString& role     = it.key();
        const QString& tempPath = it.value();

        QByteArray payload = readFrameFromTempFile(tempPath);
        if (payload.isEmpty()) {
            errors.append(tr("%1: failed to read temp file").arg(role));
            continue;
        }

        size_t headerLen = 0;
        sanuwave::RawImageInfo info;
        const uint8_t* bytes =
            reinterpret_cast<const uint8_t*>(payload.constData());

        if (!sanuwave::RawBayerDecoder::parseHeader(
                bytes, payload.size(), info, headerLen)) {
            errors.append(tr("%1: failed to parse raw header").arg(role));
            continue;
        }

        // Use stored info from server if available (has exposure metadata)
        const sanuwave::RawImageInfo& stored = frameInfos.value(role);
        if (stored.width > 0) info = stored;

        // ── DNG ──────────────────────────────────────────────────────────
        {
            const QString dngPath =
                QDir(outDir).filePath(QString("%1_%2.dng").arg(sessionId, role));
            sanuwave::RawImageData raw = sanuwave::DngExporter::buildFromCapture(
                bytes + headerLen,
                static_cast<size_t>(payload.size()) - headerLen,
                info,
                capturedCamera.toStdString());
            QString errMsg;
            if (sanuwave::DngExporter::writeDng(dngPath, raw, errMsg))
                ++savedDng;
            else
                errors.append(tr("%1: DNG write failed — %2").arg(role, errMsg));
        }

        // ── PNG (full resolution) ────────────────────────────────────────
        {
            const QString pngPath =
                QDir(outDir).filePath(QString("%1_%2.png").arg(sessionId, role));
            std::vector<uint8_t> rgb = sanuwave::RawBayerDecoder::decode(
                bytes + headerLen,
                static_cast<size_t>(payload.size()) - headerLen,
                info);
            if (rgb.empty()) {
                errors.append(tr("%1: raw decode failed").arg(role));
            } else {
                QImage img = QImage(rgb.data(), info.width, info.height,
                                    info.width * 3, QImage::Format_RGB888).copy();
                if (img.save(pngPath, "PNG"))
                    ++savedPng;
                else
                    errors.append(tr("%1: PNG save failed").arg(role));
            }
        }
    }

    // ── Save timing chart as PNG ─────────────────────────────────────────────
    bool chartSaved = false;
    if (timingChart) {
        QPixmap chartPx(timingChart->size());
        chartPx.fill(Qt::white);
        timingChart->render(&chartPx);
        const QString chartPath =
            QDir(outDir).filePath(QString("%1_timing_chart.png").arg(sessionId));
        chartSaved = chartPx.save(chartPath, "PNG");
        if (!chartSaved)
            errors.append(tr("Timing chart PNG save failed"));
    }

    // ── Save timing CSV ──────────────────────────────────────────────────────
    bool csvSaved = false;
    {
        const QString csvPath =
            QDir(outDir).filePath(QString("%1_timing.csv").arg(sessionId));
        QFile csvFile(csvPath);
        if (csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&csvFile);
            out << "# commanded_exposure_us=" << capturedCommandedExposure_us
                << ",commanded_analog_gain=" << QString::number(capturedCommandedAnalogGain, 'f', 1)
                << ",predict_vblank=" << (capturedPredictVBlank ? "true" : "false")
                << ",kernel_strobe=" << (capturedKernelStrobe ? "true" : "false")
                << "\n";
            out << "session_id,frame,role,leds_on,sensor_ts_ns,sensor_ts_rel_us,"
                   "callback_delta_us,vblank_estimate_us,rolling_shutter_us,"
                   "frame_dur_us,in_vblank\n";

            const qint64 baseTs_ns = capturedFrames[0].sensorTs_ns;
            const double summaryFramePeriod_us =
                capturedRollingShutter_us + capturedVBlankEstimate_us;

            for (int i = 0; i < capturedFrames.size(); ++i) {
                const VBlankFrameTiming& fr = capturedFrames[i];
                const double relTs_us  = (fr.sensorTs_ns - baseTs_ns) / 1000.0;
                const double upperBound = fr.frameDur_us > 0.0
                                          ? fr.frameDur_us : summaryFramePeriod_us;
                const bool inVBlank =
                    (fr.callbackDelta_us >= capturedRollingShutter_us &&
                     fr.callbackDelta_us <  upperBound);

                out << sessionId << ","
                    << i << ","
                    << fr.role << ","
                    << (fr.ledsOn ? "1" : "0") << ","
                    << fr.sensorTs_ns << ","
                    << QString::number(relTs_us,                  'f', 1) << ","
                    << QString::number(fr.callbackDelta_us,       'f', 1) << ","
                    << QString::number(capturedVBlankEstimate_us, 'f', 1) << ","
                    << QString::number(capturedRollingShutter_us, 'f', 1) << ","
                    << QString::number(upperBound,                'f', 1) << ","
                    << (inVBlank ? "1" : "0") << "\n";
            }
            csvFile.close();
            csvSaved = true;
        } else {
            errors.append(tr("CSV write failed"));
        }
    }

    // ── Summary message ──────────────────────────────────────────────────────
    QString msg = tr("Saved to: %1\n\n"
                     "DNG files: %2\n"
                     "PNG files: %3\n"
                     "Timing chart: %4\n"
                     "Timing CSV: %5")
        .arg(outDir)
        .arg(savedDng)
        .arg(savedPng)
        .arg(chartSaved ? tr("yes") : tr("no"))
        .arg(csvSaved   ? tr("yes") : tr("no"));

    if (!errors.isEmpty())
        msg += tr("\n\nWarnings:\n• ") + errors.join(tr("\n• "));

    QMessageBox::information(this, tr("Save Complete"), msg);
}

// ============================================================================
// Temp file helpers
// ============================================================================

// static
QString UVBFVBlankDialog::tempFilePathForRole(const QString& sessionId,
                                               const QString& role)
{
    QString safeRole = role;
    safeRole.replace('/', '_').replace('\\', '_').replace(' ', '_');
    return QDir::tempPath() +
           QString("/sanuwave_vblank_%1_%2.raw").arg(sessionId, safeRole);
}

// static
bool UVBFVBlankDialog::writeFrameToTempFile(const QString&    path,
                                             const QByteArray& payload)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const qint64 written = f.write(payload);
    f.close();
    return written == static_cast<qint64>(payload.size());
}

// static
QByteArray UVBFVBlankDialog::readFrameFromTempFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray data = f.readAll();
    f.close();
    return data;
}
