// imu_config_dialog.cpp

#include "imu_config_dialog.h"
#include "server_connection.h"
#include "protocol_constants.h"

#include <QtCore/QJsonObject>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>

namespace P = sanuwave::protocol;

namespace sanuwave_imu_client {

// Six common mounting orientations. The first is the chip-native frame.
// The others are sensible candidates for handheld-device builds; in any
// case, the operator can verify by tilting the device after Start and
// watching the live readout.
const FramePreset kFramePresets[] = {
    { "Chip native (no remap)",             0, +1,  1, +1,  2, +1 },
    { "Device front +Z, right +X, down +Y", 0, +1,  1, -1,  2, +1 },
    { "Device front +X, up +Z, right +Y",   2, +1,  1, +1,  0, +1 },
    { "Device front +Y, up +Z, right +X",   0, +1,  2, +1,  1, +1 },
    { "Inverted Z (board upside down)",     0, +1,  1, +1,  2, -1 },
    { "Rotated 90\u00b0 about Z",           1, +1,  0, -1,  2, +1 },
};
const int kFramePresetCount = sizeof(kFramePresets) / sizeof(kFramePresets[0]);

}  // namespace sanuwave_imu_client


ImuConfigDialog::ImuConfigDialog(const QJsonObject& existing,
                                 ServerConnection*  conn,
                                 QWidget*           parent)
    : QDialog(parent),
      connection(conn),
      settings("Sanuwave", "SanuwaveClient")
{
    setWindowTitle(tr("IMU Configuration"));
    setModal(false);
    resize(560, 580);

    buildUi();

    if (!existing.isEmpty()) loadFromJson(existing);
    else                     loadFromSettings();
    onFramePresetChanged(framePresetCombo->currentIndex());
}

ImuConfigDialog::~ImuConfigDialog() = default;


// ---------------------------------------------------------------------------
// UI build
// ---------------------------------------------------------------------------

void ImuConfigDialog::buildUi()
{
    QVBoxLayout *outer = new QVBoxLayout(this);

    tabs = new QTabWidget(this);
    tabs->addTab(buildSensorTab(),   tr("Sensor"));
    tabs->addTab(buildEventsTab(),   tr("Events"));
    tabs->addTab(buildAdvancedTab(), tr("Advanced"));
    outer->addWidget(tabs, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    QPushButton *applyBtn  = buttons->button(QDialogButtonBox::Apply);
    QPushButton *cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    applyBtn->setDefault(true);
    connect(applyBtn,  &QPushButton::clicked, this, &ImuConfigDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    outer->addWidget(buttons);
}

QWidget *ImuConfigDialog::buildSensorTab()
{
    QWidget *page    = new QWidget();
    QVBoxLayout *lay = new QVBoxLayout(page);

    // -- Sample rates / full scale -----------------------------------------
    QGroupBox *group   = new QGroupBox(tr("Sample rates and full scale"), page);
    QFormLayout *form  = new QFormLayout(group);

    accelOdrCombo = new QComboBox();
    accelOdrCombo->addItem("12.5 Hz",  0x10);
    accelOdrCombo->addItem("26 Hz",    0x20);
    accelOdrCombo->addItem("52 Hz",    0x30);
    accelOdrCombo->addItem("104 Hz",   0x40);
    accelOdrCombo->addItem("208 Hz",   0x50);
    accelOdrCombo->addItem("416 Hz",   0x60);
    accelOdrCombo->addItem("833 Hz",   0x70);
    accelOdrCombo->addItem("1.66 kHz", 0x80);
    accelOdrCombo->addItem("3.33 kHz", 0x90);
    accelOdrCombo->addItem("6.66 kHz", 0xA0);
    accelOdrCombo->setCurrentIndex(5);

    gyroOdrCombo = new QComboBox();
    gyroOdrCombo->addItem("12.5 Hz",  0x10);
    gyroOdrCombo->addItem("26 Hz",    0x20);
    gyroOdrCombo->addItem("52 Hz",    0x30);
    gyroOdrCombo->addItem("104 Hz",   0x40);
    gyroOdrCombo->addItem("208 Hz",   0x50);
    gyroOdrCombo->addItem("416 Hz",   0x60);
    gyroOdrCombo->addItem("833 Hz",   0x70);
    gyroOdrCombo->addItem("1.66 kHz", 0x80);
    gyroOdrCombo->setCurrentIndex(5);

    accelFsCombo = new QComboBox();
    accelFsCombo->addItem("\u00b12 g",  0x00);
    accelFsCombo->addItem("\u00b14 g",  0x08);
    accelFsCombo->addItem("\u00b18 g",  0x0C);
    accelFsCombo->addItem("\u00b116 g", 0x04);
    accelFsCombo->setCurrentIndex(1);

    gyroFsCombo = new QComboBox();
    gyroFsCombo->addItem("\u00b1125 dps",  0x02);
    gyroFsCombo->addItem("\u00b1245 dps",  0x00);
    gyroFsCombo->addItem("\u00b1500 dps",  0x04);
    gyroFsCombo->addItem("\u00b11000 dps", 0x08);
    gyroFsCombo->addItem("\u00b12000 dps", 0x0C);
    gyroFsCombo->setCurrentIndex(2);

    form->addRow(tr("Accel ODR:"), accelOdrCombo);
    form->addRow(tr("Gyro ODR:"),  gyroOdrCombo);
    form->addRow(tr("Accel FS:"),  accelFsCombo);
    form->addRow(tr("Gyro FS:"),   gyroFsCombo);
    lay->addWidget(group);

    // -- FIFO --------------------------------------------------------------
    QGroupBox *fifoGroup  = new QGroupBox(tr("FIFO"), page);
    QFormLayout *fifoForm = new QFormLayout(fifoGroup);
    fifoModeCombo = new QComboBox();
    fifoModeCombo->addItem(tr("Continuous (overwrite)"), 0x06);
    fifoModeCombo->addItem(tr("Stop on full"),           0x01);
    fifoModeCombo->addItem(tr("Bypass (no FIFO)"),       0x00);
    fifoWatermark = new QSpinBox();
    fifoWatermark->setRange(0, 2047);
    fifoWatermark->setValue(1024);
    fifoWatermark->setSuffix(tr(" words"));
    bduEnable = new QCheckBox(tr("Block Data Update (recommended on)"));
    bduEnable->setChecked(true);
    fifoForm->addRow(tr("Mode:"),      fifoModeCombo);
    fifoForm->addRow(tr("Watermark:"), fifoWatermark);
    fifoForm->addRow(bduEnable);
    lay->addWidget(fifoGroup);

    // -- Coordinate frame --------------------------------------------------
    QGroupBox *frameGroup   = new QGroupBox(
        tr("Coordinate Frame (locks at start)"), page);
    QFormLayout *frameForm  = new QFormLayout(frameGroup);
    framePresetCombo = new QComboBox();
    for (int i = 0; i < sanuwave_imu_client::kFramePresetCount; ++i) {
        framePresetCombo->addItem(
            QString::fromLatin1(sanuwave_imu_client::kFramePresets[i].name), i);
    }
    frameSummary = new QLabel();
    frameSummary->setStyleSheet("font-family: monospace;");
    frameForm->addRow(tr("Preset:"),  framePresetCombo);
    frameForm->addRow(tr("Mapping:"), frameSummary);
    connect(framePresetCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImuConfigDialog::onFramePresetChanged);
    lay->addWidget(frameGroup);

    lay->addStretch(1);
    return page;
}

QWidget *ImuConfigDialog::buildEventsTab()
{
    QWidget *page    = new QWidget();
    QVBoxLayout *lay = new QVBoxLayout(page);

    // -- Tap ---------------------------------------------------------------
    QGroupBox *tapGroup  = new QGroupBox(tr("Tap detection"), page);
    QFormLayout *tapForm = new QFormLayout(tapGroup);
    tapEnable = new QCheckBox(tr("Enabled"));
    tapAxisX  = new QCheckBox(tr("X"));
    tapAxisY  = new QCheckBox(tr("Y"));
    tapAxisZ  = new QCheckBox(tr("Z"));
    tapDouble = new QCheckBox(tr("Detect double tap"));
    tapEnable->setChecked(true);
    tapAxisX->setChecked(true);
    tapAxisY->setChecked(true);
    tapAxisZ->setChecked(true);
    tapDouble->setChecked(true);

    tapThreshold = new QSpinBox();  tapThreshold->setRange(0, 31); tapThreshold->setValue(8);
    tapShock     = new QSpinBox();  tapShock->setRange(0, 3);      tapShock->setValue(2);
    tapQuiet     = new QSpinBox();  tapQuiet->setRange(0, 3);      tapQuiet->setValue(1);
    tapDuration  = new QSpinBox();  tapDuration->setRange(0, 15);  tapDuration->setValue(3);

    QHBoxLayout *axes = new QHBoxLayout();
    axes->addWidget(tapAxisX);
    axes->addWidget(tapAxisY);
    axes->addWidget(tapAxisZ);
    axes->addStretch(1);

    tapForm->addRow(tapEnable);
    tapForm->addRow(tr("Axes:"), axes);
    tapForm->addRow(tapDouble);
    tapForm->addRow(tr("Threshold (0\u201331):"),       tapThreshold);
    tapForm->addRow(tr("Shock (0\u20133):"),             tapShock);
    tapForm->addRow(tr("Quiet (0\u20133):"),             tapQuiet);
    tapForm->addRow(tr("Double-tap window (0\u201315):"), tapDuration);
    lay->addWidget(tapGroup);

    // -- Free-fall ---------------------------------------------------------
    QGroupBox *ffGroup  = new QGroupBox(tr("Free-fall"), page);
    QFormLayout *ffForm = new QFormLayout(ffGroup);
    ffEnable    = new QCheckBox(tr("Enabled"));   ffEnable->setChecked(true);
    ffThreshold = new QSpinBox();  ffThreshold->setRange(0, 7);  ffThreshold->setValue(3);
    ffDuration  = new QSpinBox();  ffDuration->setRange(0, 63);  ffDuration->setValue(6);
    ffForm->addRow(ffEnable);
    ffForm->addRow(tr("Threshold (0\u20137):"),  ffThreshold);
    ffForm->addRow(tr("Duration (0\u201363):"),  ffDuration);
    lay->addWidget(ffGroup);

    // -- Wake-on-motion ----------------------------------------------------
    QGroupBox *wakeGroup  = new QGroupBox(tr("Wake on motion"), page);
    QFormLayout *wakeForm = new QFormLayout(wakeGroup);
    wakeEnable    = new QCheckBox(tr("Enabled"));
    wakeThreshold = new QSpinBox(); wakeThreshold->setRange(0, 63); wakeThreshold->setValue(2);
    wakeDuration  = new QSpinBox(); wakeDuration->setRange(0, 3);
    wakeForm->addRow(wakeEnable);
    wakeForm->addRow(tr("Threshold (0\u201363):"),     wakeThreshold);
    wakeForm->addRow(tr("Sleep duration (0\u20133):"), wakeDuration);
    lay->addWidget(wakeGroup);

    lay->addStretch(1);
    return page;
}

QWidget *ImuConfigDialog::buildAdvancedTab()
{
    QWidget *page    = new QWidget();
    QVBoxLayout *lay = new QVBoxLayout(page);

    QGroupBox *intGroup  = new QGroupBox(tr("Interrupt routing"), page);
    QVBoxLayout *intLay  = new QVBoxLayout(intGroup);
    int1Watermark = new QCheckBox(tr("INT1 \u2190 FIFO watermark")); int1Watermark->setChecked(true);
    int1Overrun   = new QCheckBox(tr("INT1 \u2190 FIFO overrun"));   int1Overrun->setChecked(true);
    int1DataReady = new QCheckBox(tr("INT1 \u2190 Data ready (every sample \u2014 high rate!)"));
    int2FreeFall  = new QCheckBox(tr("INT2 \u2190 Free-fall"));      int2FreeFall->setChecked(true);
    int2SingleTap = new QCheckBox(tr("INT2 \u2190 Single tap"));     int2SingleTap->setChecked(true);
    int2DoubleTap = new QCheckBox(tr("INT2 \u2190 Double tap"));     int2DoubleTap->setChecked(true);
    intLay->addWidget(int1Watermark);
    intLay->addWidget(int1Overrun);
    intLay->addWidget(int1DataReady);
    intLay->addWidget(int2FreeFall);
    intLay->addWidget(int2SingleTap);
    intLay->addWidget(int2DoubleTap);
    lay->addWidget(intGroup);

    QGroupBox *debugGroup = new QGroupBox(tr("Debug"), page);
    QHBoxLayout *debugLay = new QHBoxLayout(debugGroup);
    softResetButton = new QPushButton(tr("Soft reset"));
    debugLay->addWidget(softResetButton);
    debugLay->addStretch(1);
    connect(softResetButton, &QPushButton::clicked,
            this, &ImuConfigDialog::onSoftResetClicked);
    lay->addWidget(debugGroup);

    QLabel *note = new QLabel(tr(
        "Note: register-level read/write is available via imu_read_reg / "
        "imu_write_reg commands but not exposed in this dialog. For bench "
        "debugging, use i2cget/i2cset over SSH (see docs/imu_bringup.md)."));
    note->setWordWrap(true);
    note->setStyleSheet("color: #666;");
    lay->addWidget(note);

    lay->addStretch(1);
    return page;
}


// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ImuConfigDialog::onFramePresetChanged(int index)
{
    if (index < 0 || index >= sanuwave_imu_client::kFramePresetCount) {
        frameSummary->setText({});
        return;
    }
    const sanuwave_imu_client::FramePreset &p = sanuwave_imu_client::kFramePresets[index];
    const char *axisName[] = { "X", "Y", "Z" };
    frameSummary->setText(QString("X = %1%2,  Y = %3%4,  Z = %5%6")
        .arg(p.xSign < 0 ? "-" : "+").arg(axisName[p.xSrc])
        .arg(p.ySign < 0 ? "-" : "+").arg(axisName[p.ySrc])
        .arg(p.zSign < 0 ? "-" : "+").arg(axisName[p.zSrc]));
}

void ImuConfigDialog::onSoftResetClicked()
{
    if (!connection) return;
    QJsonObject cmd;
    cmd[P::Param::COMMAND] = P::Command::IMU_SOFT_RESET;
    connection->sendCommand(cmd);
}

void ImuConfigDialog::onApply()
{
    result = toJson();
    saveToSettings();
    accept();
}


// ---------------------------------------------------------------------------
// JSON build / parse / persist
// ---------------------------------------------------------------------------

QJsonObject ImuConfigDialog::toJson() const
{
    QJsonObject cfg;

    cfg[P::ImuParam::ACCEL_ODR] = accelOdrCombo->currentData().toInt();
    cfg[P::ImuParam::GYRO_ODR]  = gyroOdrCombo->currentData().toInt();
    cfg[P::ImuParam::ACCEL_FS]  = accelFsCombo->currentData().toInt();
    cfg[P::ImuParam::GYRO_FS]   = gyroFsCombo->currentData().toInt();

    cfg[P::ImuParam::FIFO_MODE]         = fifoModeCombo->currentData().toInt();
    cfg[P::ImuParam::FIFO_WATERMARK]    = fifoWatermark->value();
    cfg[P::ImuParam::BLOCK_DATA_UPDATE] = bduEnable->isChecked();

    cfg[P::ImuParam::TAP_ENABLED]   = tapEnable->isChecked();
    cfg[P::ImuParam::TAP_AXIS_X]    = tapAxisX->isChecked();
    cfg[P::ImuParam::TAP_AXIS_Y]    = tapAxisY->isChecked();
    cfg[P::ImuParam::TAP_AXIS_Z]    = tapAxisZ->isChecked();
    cfg[P::ImuParam::TAP_DOUBLE]    = tapDouble->isChecked();
    cfg[P::ImuParam::TAP_THRESHOLD] = tapThreshold->value();
    cfg[P::ImuParam::TAP_SHOCK]     = tapShock->value();
    cfg[P::ImuParam::TAP_QUIET]     = tapQuiet->value();
    cfg[P::ImuParam::TAP_DURATION]  = tapDuration->value();

    cfg[P::ImuParam::FREE_FALL_ENABLED]   = ffEnable->isChecked();
    cfg[P::ImuParam::FREE_FALL_THRESHOLD] = ffThreshold->value();
    cfg[P::ImuParam::FREE_FALL_DURATION]  = ffDuration->value();

    cfg[P::ImuParam::WAKE_ENABLED]   = wakeEnable->isChecked();
    cfg[P::ImuParam::WAKE_THRESHOLD] = wakeThreshold->value();
    cfg[P::ImuParam::WAKE_DURATION]  = wakeDuration->value();

    cfg[P::ImuParam::INT1_FIFO_WATERMARK] = int1Watermark->isChecked();
    cfg[P::ImuParam::INT1_FIFO_OVERRUN]   = int1Overrun->isChecked();
    cfg[P::ImuParam::INT1_DATA_READY]     = int1DataReady->isChecked();
    cfg[P::ImuParam::INT2_FREE_FALL]      = int2FreeFall->isChecked();
    cfg[P::ImuParam::INT2_SINGLE_TAP]     = int2SingleTap->isChecked();
    cfg[P::ImuParam::INT2_DOUBLE_TAP]     = int2DoubleTap->isChecked();

    const int idx = framePresetCombo->currentData().toInt();
    if (idx >= 0 && idx < sanuwave_imu_client::kFramePresetCount) {
        const sanuwave_imu_client::FramePreset &p = sanuwave_imu_client::kFramePresets[idx];
        const char *axisName[] = { "X", "Y", "Z" };
        cfg[P::ImuParam::FRAME_X_SOURCE] = QString::fromLatin1(axisName[p.xSrc]);
        cfg[P::ImuParam::FRAME_X_SIGN]   = p.xSign;
        cfg[P::ImuParam::FRAME_Y_SOURCE] = QString::fromLatin1(axisName[p.ySrc]);
        cfg[P::ImuParam::FRAME_Y_SIGN]   = p.ySign;
        cfg[P::ImuParam::FRAME_Z_SOURCE] = QString::fromLatin1(axisName[p.zSrc]);
        cfg[P::ImuParam::FRAME_Z_SIGN]   = p.zSign;
    }
    return cfg;
}

void ImuConfigDialog::loadFromJson(const QJsonObject& src)
{
    auto setComboByData = [](QComboBox *c, int data) {
        const int idx = c->findData(data);
        if (idx >= 0) c->setCurrentIndex(idx);
    };

    if (src.contains(P::ImuParam::ACCEL_ODR))
        setComboByData(accelOdrCombo, src.value(P::ImuParam::ACCEL_ODR).toInt());
    if (src.contains(P::ImuParam::GYRO_ODR))
        setComboByData(gyroOdrCombo,  src.value(P::ImuParam::GYRO_ODR).toInt());
    if (src.contains(P::ImuParam::ACCEL_FS))
        setComboByData(accelFsCombo,  src.value(P::ImuParam::ACCEL_FS).toInt());
    if (src.contains(P::ImuParam::GYRO_FS))
        setComboByData(gyroFsCombo,   src.value(P::ImuParam::GYRO_FS).toInt());
    if (src.contains(P::ImuParam::FIFO_MODE))
        setComboByData(fifoModeCombo, src.value(P::ImuParam::FIFO_MODE).toInt());

    if (src.contains(P::ImuParam::FIFO_WATERMARK))
        fifoWatermark->setValue(src.value(P::ImuParam::FIFO_WATERMARK).toInt(1024));
    if (src.contains(P::ImuParam::BLOCK_DATA_UPDATE))
        bduEnable->setChecked(src.value(P::ImuParam::BLOCK_DATA_UPDATE).toBool(true));

    tapEnable->setChecked(src.value(P::ImuParam::TAP_ENABLED).toBool(tapEnable->isChecked()));
    tapAxisX->setChecked (src.value(P::ImuParam::TAP_AXIS_X).toBool(tapAxisX->isChecked()));
    tapAxisY->setChecked (src.value(P::ImuParam::TAP_AXIS_Y).toBool(tapAxisY->isChecked()));
    tapAxisZ->setChecked (src.value(P::ImuParam::TAP_AXIS_Z).toBool(tapAxisZ->isChecked()));
    tapDouble->setChecked(src.value(P::ImuParam::TAP_DOUBLE).toBool(tapDouble->isChecked()));
    if (src.contains(P::ImuParam::TAP_THRESHOLD)) tapThreshold->setValue(src.value(P::ImuParam::TAP_THRESHOLD).toInt());
    if (src.contains(P::ImuParam::TAP_SHOCK))     tapShock->setValue(src.value(P::ImuParam::TAP_SHOCK).toInt());
    if (src.contains(P::ImuParam::TAP_QUIET))     tapQuiet->setValue(src.value(P::ImuParam::TAP_QUIET).toInt());
    if (src.contains(P::ImuParam::TAP_DURATION))  tapDuration->setValue(src.value(P::ImuParam::TAP_DURATION).toInt());

    ffEnable->setChecked(src.value(P::ImuParam::FREE_FALL_ENABLED).toBool(ffEnable->isChecked()));
    if (src.contains(P::ImuParam::FREE_FALL_THRESHOLD)) ffThreshold->setValue(src.value(P::ImuParam::FREE_FALL_THRESHOLD).toInt());
    if (src.contains(P::ImuParam::FREE_FALL_DURATION))  ffDuration->setValue(src.value(P::ImuParam::FREE_FALL_DURATION).toInt());

    wakeEnable->setChecked(src.value(P::ImuParam::WAKE_ENABLED).toBool(wakeEnable->isChecked()));
    if (src.contains(P::ImuParam::WAKE_THRESHOLD)) wakeThreshold->setValue(src.value(P::ImuParam::WAKE_THRESHOLD).toInt());
    if (src.contains(P::ImuParam::WAKE_DURATION))  wakeDuration->setValue(src.value(P::ImuParam::WAKE_DURATION).toInt());

    int1Watermark->setChecked(src.value(P::ImuParam::INT1_FIFO_WATERMARK).toBool(int1Watermark->isChecked()));
    int1Overrun->setChecked  (src.value(P::ImuParam::INT1_FIFO_OVERRUN).toBool(int1Overrun->isChecked()));
    int1DataReady->setChecked(src.value(P::ImuParam::INT1_DATA_READY).toBool(int1DataReady->isChecked()));
    int2FreeFall->setChecked (src.value(P::ImuParam::INT2_FREE_FALL).toBool(int2FreeFall->isChecked()));
    int2SingleTap->setChecked(src.value(P::ImuParam::INT2_SINGLE_TAP).toBool(int2SingleTap->isChecked()));
    int2DoubleTap->setChecked(src.value(P::ImuParam::INT2_DOUBLE_TAP).toBool(int2DoubleTap->isChecked()));
}

void ImuConfigDialog::loadFromSettings()
{
    settings.beginGroup("Imu");
    accelOdrCombo->setCurrentIndex(settings.value("accelOdrIdx",  5).toInt());
    gyroOdrCombo->setCurrentIndex (settings.value("gyroOdrIdx",   5).toInt());
    accelFsCombo->setCurrentIndex (settings.value("accelFsIdx",   1).toInt());
    gyroFsCombo->setCurrentIndex  (settings.value("gyroFsIdx",    2).toInt());
    fifoModeCombo->setCurrentIndex(settings.value("fifoModeIdx",  0).toInt());
    fifoWatermark->setValue       (settings.value("fifoWatermark", 1024).toInt());
    bduEnable->setChecked         (settings.value("bdu", true).toBool());
    framePresetCombo->setCurrentIndex(settings.value("framePresetIdx", 0).toInt());

    tapEnable->setChecked  (settings.value("tap/enabled",  true).toBool());
    tapAxisX->setChecked   (settings.value("tap/x",        true).toBool());
    tapAxisY->setChecked   (settings.value("tap/y",        true).toBool());
    tapAxisZ->setChecked   (settings.value("tap/z",        true).toBool());
    tapDouble->setChecked  (settings.value("tap/double",   true).toBool());
    tapThreshold->setValue (settings.value("tap/threshold", 8).toInt());
    tapShock->setValue     (settings.value("tap/shock",     2).toInt());
    tapQuiet->setValue     (settings.value("tap/quiet",     1).toInt());
    tapDuration->setValue  (settings.value("tap/duration",  3).toInt());

    ffEnable->setChecked   (settings.value("ff/enabled",   true).toBool());
    ffThreshold->setValue  (settings.value("ff/threshold",  3).toInt());
    ffDuration->setValue   (settings.value("ff/duration",   6).toInt());

    wakeEnable->setChecked   (settings.value("wake/enabled",   false).toBool());
    wakeThreshold->setValue  (settings.value("wake/threshold", 2).toInt());
    wakeDuration->setValue   (settings.value("wake/duration",  0).toInt());

    int1Watermark->setChecked(settings.value("int1/watermark", true).toBool());
    int1Overrun->setChecked  (settings.value("int1/overrun",   true).toBool());
    int1DataReady->setChecked(settings.value("int1/dataReady", false).toBool());
    int2FreeFall->setChecked (settings.value("int2/freeFall",  true).toBool());
    int2SingleTap->setChecked(settings.value("int2/singleTap", true).toBool());
    int2DoubleTap->setChecked(settings.value("int2/doubleTap", true).toBool());
    settings.endGroup();
}

void ImuConfigDialog::saveToSettings() const
{
    settings.beginGroup("Imu");
    settings.setValue("accelOdrIdx",   accelOdrCombo->currentIndex());
    settings.setValue("gyroOdrIdx",    gyroOdrCombo->currentIndex());
    settings.setValue("accelFsIdx",    accelFsCombo->currentIndex());
    settings.setValue("gyroFsIdx",     gyroFsCombo->currentIndex());
    settings.setValue("fifoModeIdx",   fifoModeCombo->currentIndex());
    settings.setValue("fifoWatermark", fifoWatermark->value());
    settings.setValue("bdu",           bduEnable->isChecked());
    settings.setValue("framePresetIdx", framePresetCombo->currentIndex());

    settings.setValue("tap/enabled",   tapEnable->isChecked());
    settings.setValue("tap/x",         tapAxisX->isChecked());
    settings.setValue("tap/y",         tapAxisY->isChecked());
    settings.setValue("tap/z",         tapAxisZ->isChecked());
    settings.setValue("tap/double",    tapDouble->isChecked());
    settings.setValue("tap/threshold", tapThreshold->value());
    settings.setValue("tap/shock",     tapShock->value());
    settings.setValue("tap/quiet",     tapQuiet->value());
    settings.setValue("tap/duration",  tapDuration->value());

    settings.setValue("ff/enabled",   ffEnable->isChecked());
    settings.setValue("ff/threshold", ffThreshold->value());
    settings.setValue("ff/duration",  ffDuration->value());

    settings.setValue("wake/enabled",   wakeEnable->isChecked());
    settings.setValue("wake/threshold", wakeThreshold->value());
    settings.setValue("wake/duration",  wakeDuration->value());

    settings.setValue("int1/watermark", int1Watermark->isChecked());
    settings.setValue("int1/overrun",   int1Overrun->isChecked());
    settings.setValue("int1/dataReady", int1DataReady->isChecked());
    settings.setValue("int2/freeFall",  int2FreeFall->isChecked());
    settings.setValue("int2/singleTap", int2SingleTap->isChecked());
    settings.setValue("int2/doubleTap", int2DoubleTap->isChecked());
    settings.endGroup();
}
