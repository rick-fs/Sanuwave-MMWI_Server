// motion_settings_dialog.h
//
// Non-modal settings dialog for the motion-measurement subsystem.
// Splits controls into two visual groups:
//
//   "Display (live)"           - client-side hysteresis, confidence floor,
//                                decay. Changes apply on the next frame.
//   "Measurement (next stream)" - server-side ROI size and reference mode.
//                                Changes apply when the next start_stream
//                                is issued; the panel notes this explicitly.
//
// All values are persisted to QSettings under the [motion] group so they
// survive restart and feed startup defaults in MainWindow.
//
// Copyright 2026 Sanuwave Medical LLC.
#ifndef MOTION_SETTINGS_DIALOG_H
#define MOTION_SETTINGS_DIALOG_H

#include <QDialog>
#include <QSettings>

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;

class MotionSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    // settings is borrowed - the dialog reads/writes but does not own it.
    explicit MotionSettingsDialog(QSettings* settings, QWidget* parent = nullptr);

    // Push current widget values back into QSettings without closing the
    // dialog. Called automatically on every spinbox change so the live
    // signals below are always backed by persisted state.
    void applyAndPersist();

    // Restore the four compile-time defaults to the widgets (and persist).
    void restoreDefaults();

signals:
    // Emitted whenever a "live" widget changes value. MainWindow uses
    // these to update its hysteresis state machine and push the new
    // thresholds to the chart's reference lines.
    void enterMovingChanged(double px);
    void exitMovingChanged(double px);
    void confFloorChanged(double v);
    void decayMsChanged(int ms);

    // Emitted on changes to server-side parameters. MainWindow only needs
    // these for persistence; the values are read out of QSettings the next
    // time onStreamStart/onStreamStartDual build the command.
    void roiSizeChanged(int px);
    void referenceChanged(const QString& mode);

private slots:
    void onEnterChanged(double v);
    void onExitChanged(double v);
    void onConfChanged(double v);
    void onDecayChanged(int v);
    void onRoiChanged(int v);
    void onReferenceChanged(int index);

private:
    void buildUi();
    void loadFromSettings();

    QSettings* settings_;     // borrowed

    QDoubleSpinBox* enterSpin_   = nullptr;
    QDoubleSpinBox* exitSpin_    = nullptr;
    QDoubleSpinBox* confSpin_    = nullptr;
    QSpinBox*       decaySpin_   = nullptr;
    QSpinBox*       roiSpin_     = nullptr;
    QComboBox*      refCombo_    = nullptr;
    QPushButton*    defaultsBtn_ = nullptr;
    QPushButton*    closeBtn_    = nullptr;
    QLabel*         hintLabel_   = nullptr;

    bool suppressSignals_ = false;
};

#endif // MOTION_SETTINGS_DIALOG_H
