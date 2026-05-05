// imu_config_dialog.h
//
// Configure-only dialog for the LSM6DS3TR-C IMU.
//
// Three tabs:
//   - Sensor:   ODR / FS / FIFO / BDU / coordinate frame
//   - Events:   tap / free-fall / wake-on-motion thresholds
//   - Advanced: interrupt routing + soft-reset button
//
// The dialog produces a QJsonObject in the shape ImuParam expects, ready to
// be paired with Command::IMU_CONFIGURE and sent via
// MainWindow::sendCommand. Settings persist in QSettings under
// "Sanuwave/SanuwaveClient/Imu/...".

#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtWidgets/QDialog>

class QComboBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QTabWidget;
class ServerConnection;  // existing client class

namespace sanuwave_imu_client {

// Frame presets — must match the AxisMap layout the server expects.
// (The server-side mapping lives in Lsm6ds3trc::CoordinateFrame.)
struct FramePreset {
    const char* name;
    int8_t      xSrc, xSign;
    int8_t      ySrc, ySign;
    int8_t      zSrc, zSign;
};

extern const FramePreset kFramePresets[];
extern const int          kFramePresetCount;

}  // namespace sanuwave_imu_client


class ImuConfigDialog : public QDialog {
    Q_OBJECT
public:
    // existing  — last imu_configure payload sent (or empty for first use).
    // connection — borrowed pointer used to send debug commands (soft reset).
    //              May be nullptr; soft-reset button is then a no-op.
    explicit ImuConfigDialog(const QJsonObject& existing,
                             ServerConnection*  connection,
                             QWidget*           parent = nullptr);
    ~ImuConfigDialog() override;

    QJsonObject resultConfig() const { return result; }

private slots:
    void onApply();
    void onSoftResetClicked();
    void onFramePresetChanged(int index);

private:
    void buildUi();
    QWidget* buildSensorTab();
    QWidget* buildEventsTab();
    QWidget* buildAdvancedTab();

    void loadFromJson(const QJsonObject& src);
    void loadFromSettings();
    void saveToSettings() const;
    QJsonObject toJson() const;

    QTabWidget* tabs = nullptr;

    // Sensor tab.
    QComboBox*  accelOdrCombo    = nullptr;
    QComboBox*  gyroOdrCombo     = nullptr;
    QComboBox*  accelFsCombo     = nullptr;
    QComboBox*  gyroFsCombo      = nullptr;
    QComboBox*  fifoModeCombo    = nullptr;
    QSpinBox*   fifoWatermark    = nullptr;
    QCheckBox*  bduEnable        = nullptr;
    QComboBox*  framePresetCombo = nullptr;
    QLabel*     frameSummary     = nullptr;

    // Events tab.
    QCheckBox*  tapEnable        = nullptr;
    QCheckBox*  tapAxisX         = nullptr;
    QCheckBox*  tapAxisY         = nullptr;
    QCheckBox*  tapAxisZ         = nullptr;
    QCheckBox*  tapDouble        = nullptr;
    QSpinBox*   tapThreshold     = nullptr;
    QSpinBox*   tapShock         = nullptr;
    QSpinBox*   tapQuiet         = nullptr;
    QSpinBox*   tapDuration      = nullptr;
    QCheckBox*  ffEnable         = nullptr;
    QSpinBox*   ffThreshold      = nullptr;
    QSpinBox*   ffDuration       = nullptr;
    QCheckBox*  wakeEnable       = nullptr;
    QSpinBox*   wakeThreshold    = nullptr;
    QSpinBox*   wakeDuration     = nullptr;

    // Advanced tab.
    QCheckBox*   int1Watermark    = nullptr;
    QCheckBox*   int1Overrun      = nullptr;
    QCheckBox*   int1DataReady    = nullptr;
    QCheckBox*   int2FreeFall     = nullptr;
    QCheckBox*   int2SingleTap    = nullptr;
    QCheckBox*   int2DoubleTap    = nullptr;
    QPushButton* softResetButton  = nullptr;

    ServerConnection* connection = nullptr;
    mutable QSettings settings;
    QJsonObject       result;
};
