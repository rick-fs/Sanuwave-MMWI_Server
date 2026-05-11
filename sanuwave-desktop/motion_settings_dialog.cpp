// motion_settings_dialog.cpp
//
// Copyright 2026 Sanuwave Medical LLC.
#include "motion_settings_dialog.h"
#include "protocol_constants.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
// Defaults must match MainWindow's startup defaults so "Restore defaults"
// here and "fresh QSettings" produce the same behaviour.
constexpr double DEF_ENTER  = 1.5;
constexpr double DEF_EXIT   = 0.5;
constexpr double DEF_CONF   = 0.05;
constexpr int    DEF_DECAY  = 1000;
constexpr int    DEF_ROI    = 512;
}

MotionSettingsDialog::MotionSettingsDialog(QSettings* settings, QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
{
    setWindowTitle(tr("Motion Settings"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    // Non-modal: operator can leave this open while tuning against a live
    // stream and the chart on the main window.
    setModal(false);

    buildUi();
    loadFromSettings();
}

void MotionSettingsDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);

    // ----------------- Display (live) -----------------
    auto* liveGroup  = new QGroupBox(tr("Display (live)"), this);
    auto* liveForm   = new QFormLayout(liveGroup);

    enterSpin_ = new QDoubleSpinBox(this);
    enterSpin_->setRange(0.1, 10.0);
    enterSpin_->setSingleStep(0.1);
    enterSpin_->setDecimals(2);
    enterSpin_->setSuffix(tr(" px"));
    enterSpin_->setToolTip(tr("trans_px must reach this value to flip the "
                              "badge from STILL to MOVING."));
    liveForm->addRow(tr("Enter Moving:"), enterSpin_);

    exitSpin_ = new QDoubleSpinBox(this);
    exitSpin_->setRange(0.05, 5.0);
    exitSpin_->setSingleStep(0.05);
    exitSpin_->setDecimals(2);
    exitSpin_->setSuffix(tr(" px"));
    exitSpin_->setToolTip(tr("trans_px must drop to this value or below to "
                             "flip the badge back from MOVING to STILL. The "
                             "gap between Exit and Enter is hysteresis."));
    liveForm->addRow(tr("Exit Moving:"), exitSpin_);

    confSpin_ = new QDoubleSpinBox(this);
    confSpin_->setRange(0.0, 0.5);
    confSpin_->setSingleStep(0.01);
    confSpin_->setDecimals(3);
    confSpin_->setToolTip(tr("Phase-correlation confidence below this is "
                             "treated as no measurement. Lower this if the "
                             "indicator goes blank too often; raise it to "
                             "filter noise."));
    liveForm->addRow(tr("Confidence floor:"), confSpin_);

    decaySpin_ = new QSpinBox(this);
    decaySpin_->setRange(200, 5000);
    decaySpin_->setSingleStep(100);
    decaySpin_->setSuffix(tr(" ms"));
    decaySpin_->setToolTip(tr("Clear the indicator if no rgb frame arrives "
                              "within this time. Higher values keep the "
                              "last reading visible longer after a stall."));
    liveForm->addRow(tr("Display decay:"), decaySpin_);

    root->addWidget(liveGroup);

    // ----------------- Measurement (next stream) -----------------
    auto* svrGroup = new QGroupBox(tr("Measurement (next stream)"), this);
    auto* svrForm  = new QFormLayout(svrGroup);

    roiSpin_ = new QSpinBox(this);
    roiSpin_->setRange(64, 2048);
    roiSpin_->setSingleStep(64);
    roiSpin_->setSuffix(tr(" px"));
    roiSpin_->setToolTip(tr("Side length of the centred square ROI fed to "
                            "phase correlation. Larger captures more scene "
                            "at higher cost; smaller is faster but more "
                            "sensitive to noise. Powers of two are best."));
    svrForm->addRow(tr("ROI size:"), roiSpin_);

    refCombo_ = new QComboBox(this);
    refCombo_->addItem(tr("Previous frame (rolling)"),
                       sanuwave::protocol::MotionReference::PREVIOUS);
    refCombo_->addItem(tr("First frame (anchor)"),
                       sanuwave::protocol::MotionReference::ANCHOR);
    refCombo_->setToolTip(tr("'Previous frame' reports motion-per-frame "
                             "(useful for STILL/MOVING). 'First frame' "
                             "reports cumulative drift since the stream "
                             "started."));
    svrForm->addRow(tr("Reference:"), refCombo_);

    root->addWidget(svrGroup);

    // Hint label spans the bottom; reminds the operator that ROI/ref need
    // a stream restart to take effect.
    hintLabel_ = new QLabel(
        tr("ROI size and reference are read by the server at stream start. "
           "Stop and restart the stream for them to take effect."),
        this);
    hintLabel_->setWordWrap(true);
    hintLabel_->setStyleSheet("QLabel { color: #555; font-style: italic; }");
    root->addWidget(hintLabel_);

    // ----------------- Buttons -----------------
    auto* btnRow = new QHBoxLayout();
    defaultsBtn_ = new QPushButton(tr("Restore defaults"), this);
    closeBtn_    = new QPushButton(tr("Close"), this);
    btnRow->addWidget(defaultsBtn_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);
    root->addLayout(btnRow);

    // ----------------- Connections -----------------
    connect(enterSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MotionSettingsDialog::onEnterChanged);
    connect(exitSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MotionSettingsDialog::onExitChanged);
    connect(confSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MotionSettingsDialog::onConfChanged);
    connect(decaySpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MotionSettingsDialog::onDecayChanged);
    connect(roiSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MotionSettingsDialog::onRoiChanged);
    connect(refCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MotionSettingsDialog::onReferenceChanged);
    connect(defaultsBtn_, &QPushButton::clicked,
            this, &MotionSettingsDialog::restoreDefaults);
    connect(closeBtn_, &QPushButton::clicked,
            this, &QDialog::close);
}

void MotionSettingsDialog::loadFromSettings()
{
    suppressSignals_ = true;

    enterSpin_->setValue(settings_->value("motion/enterMovingPx", DEF_ENTER).toDouble());
    exitSpin_ ->setValue(settings_->value("motion/exitMovingPx",  DEF_EXIT ).toDouble());
    confSpin_ ->setValue(settings_->value("motion/confFloor",     DEF_CONF ).toDouble());
    decaySpin_->setValue(settings_->value("motion/decayMs",       DEF_DECAY).toInt());
    roiSpin_  ->setValue(settings_->value("motion/roiSize",       DEF_ROI  ).toInt());

    const QString ref =
        settings_->value("motion/reference",
                         sanuwave::protocol::MotionReference::PREVIOUS).toString();
    int idx = refCombo_->findData(ref);
    if (idx < 0) idx = 0;
    refCombo_->setCurrentIndex(idx);

    suppressSignals_ = false;
}

void MotionSettingsDialog::applyAndPersist()
{
    // Single point of truth: spinbox values -> QSettings. Called after
    // every successful change. Cheap; QSettings caches.
    settings_->setValue("motion/enterMovingPx", enterSpin_->value());
    settings_->setValue("motion/exitMovingPx",  exitSpin_->value());
    settings_->setValue("motion/confFloor",     confSpin_->value());
    settings_->setValue("motion/decayMs",       decaySpin_->value());
    settings_->setValue("motion/roiSize",       roiSpin_->value());
    settings_->setValue("motion/reference",     refCombo_->currentData().toString());
}

void MotionSettingsDialog::restoreDefaults()
{
    suppressSignals_ = true;
    enterSpin_->setValue(DEF_ENTER);
    exitSpin_ ->setValue(DEF_EXIT);
    confSpin_ ->setValue(DEF_CONF);
    decaySpin_->setValue(DEF_DECAY);
    roiSpin_  ->setValue(DEF_ROI);
    refCombo_ ->setCurrentIndex(0);   // PREVIOUS
    suppressSignals_ = false;

    applyAndPersist();
    // Re-emit so MainWindow updates its in-memory state and the chart's
    // threshold lines.
    emit enterMovingChanged(DEF_ENTER);
    emit exitMovingChanged (DEF_EXIT);
    emit confFloorChanged  (DEF_CONF);
    emit decayMsChanged    (DEF_DECAY);
    emit roiSizeChanged    (DEF_ROI);
    emit referenceChanged  (sanuwave::protocol::MotionReference::PREVIOUS);
}

// ---------------------------------------------------------------------------
// Slot handlers. Each enforces invariants (exit <= enter), persists, and
// re-emits a targeted signal MainWindow listens to.
// ---------------------------------------------------------------------------
void MotionSettingsDialog::onEnterChanged(double v)
{
    if (suppressSignals_) return;

    // Invariant: exit <= enter. Lowering enter below exit drags exit down.
    if (v < exitSpin_->value())
    {
        suppressSignals_ = true;
        exitSpin_->setValue(v);
        suppressSignals_ = false;
        applyAndPersist();
        emit exitMovingChanged(v);
    }
    applyAndPersist();
    emit enterMovingChanged(v);
}

void MotionSettingsDialog::onExitChanged(double v)
{
    if (suppressSignals_) return;

    // Invariant: exit <= enter. Raising exit above enter drags enter up.
    if (v > enterSpin_->value())
    {
        suppressSignals_ = true;
        enterSpin_->setValue(v);
        suppressSignals_ = false;
        applyAndPersist();
        emit enterMovingChanged(v);
    }
    applyAndPersist();
    emit exitMovingChanged(v);
}

void MotionSettingsDialog::onConfChanged(double v)
{
    if (suppressSignals_) return;
    applyAndPersist();
    emit confFloorChanged(v);
}

void MotionSettingsDialog::onDecayChanged(int v)
{
    if (suppressSignals_) return;
    applyAndPersist();
    emit decayMsChanged(v);
}

void MotionSettingsDialog::onRoiChanged(int v)
{
    if (suppressSignals_) return;
    applyAndPersist();
    emit roiSizeChanged(v);
}

void MotionSettingsDialog::onReferenceChanged(int /*index*/)
{
    if (suppressSignals_) return;
    applyAndPersist();
    emit referenceChanged(refCombo_->currentData().toString());
}
