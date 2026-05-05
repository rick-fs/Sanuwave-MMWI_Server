// client/src/lens_calibration_dialog.cpp
#include "lens_calibration_dialog.h"
#include "server_connection.h"
#include "protocol_constants.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

using namespace sanuwave::protocol;

// ─────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────

LensCalibrationDialog::LensCalibrationDialog(ServerConnection* conn,
                                             const QString& camera,
                                             QWidget* parent)
    : QDialog(parent), conn(conn), camera(camera)
{
    setWindowTitle(tr("Lens Calibration — %1").arg(camera.toUpper()));
    setMinimumSize(900, 700);
    setSizeGripEnabled(true);

    setSliderRange(camera);
    buildUi();

    // Debounce timer — sends lens position 150 ms after last slider move
    debounceTimer = new QTimer(this);
    debounceTimer->setSingleShot(true);
    connect(debounceTimer, &QTimer::timeout, this, [this]() {
        sendLensPosition(pendingPosition);
    });
}

// ─────────────────────────────────────────────────────────────
// UI Construction
// ─────────────────────────────────────────────────────────────

void LensCalibrationDialog::buildUi()
{
    auto* root = new QHBoxLayout(this);
    root->setSpacing(8);

    // ── Left column: video + ToF ──────────────────────────────
    auto* leftCol = new QVBoxLayout;
    leftCol->setSpacing(6);

    // Scale selector
    auto* scaleRow = new QHBoxLayout;
    scaleRow->addWidget(new QLabel(tr("Scale:")));
    scaleCombo = new QComboBox;
    scaleCombo->addItem(tr("Fit to window"), 0.0f);
    scaleCombo->addItem(tr("25%"),  0.25f);
    scaleCombo->addItem(tr("50%"),  0.50f);
    scaleCombo->addItem(tr("75%"),  0.75f);
    scaleCombo->addItem(tr("100%"), 1.00f);
    scaleCombo->addItem(tr("150%"), 1.50f);
    scaleCombo->addItem(tr("200%"), 2.00f);
    scaleRow->addWidget(scaleCombo);
    scaleRow->addStretch();
    leftCol->addLayout(scaleRow);

    // Video area
    videoLabel = new QLabel;
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setMinimumSize(480, 360);
    videoLabel->setStyleSheet("background: #111; color: #888;");
    videoLabel->setText(tr("Waiting for stream…"));

    scrollArea = new QScrollArea;
    scrollArea->setWidget(videoLabel);
    scrollArea->setWidgetResizable(true);   // default: fit-to-window
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    leftCol->addWidget(scrollArea, 1);

    // ToF display
    auto* tofGroup = new QGroupBox(tr("Distance (ToF)"));
    auto* tofLayout = new QHBoxLayout(tofGroup);
    tofLabel = new QLabel(tr("— mm"));
    tofLabel->setAlignment(Qt::AlignCenter);
    QFont f = tofLabel->font();
    f.setPointSize(20);
    f.setBold(true);
    tofLabel->setFont(f);
    tofLayout->addWidget(tofLabel);
    leftCol->addWidget(tofGroup);

    root->addLayout(leftCol, 3);

    // ── Right column: focus + table ───────────────────────────
    auto* rightCol = new QVBoxLayout;
    rightCol->setSpacing(8);

    // Focus control
    auto* focusGroup = new QGroupBox(tr("Lens Position"));
    auto* focusLayout = new QVBoxLayout(focusGroup);

    // Slider
    slider = new QSlider(Qt::Horizontal);
    slider->setMinimum(0);
    slider->setMaximum(positionToSlider(maxPosition));
    slider->setValue(0);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(positionToSlider(maxPosition) / 10);
    focusLayout->addWidget(slider);

    // SpinBox + range label row
    auto* spinRow = new QHBoxLayout;
    spinBox = new QDoubleSpinBox;
    spinBox->setRange(0.0, static_cast<double>(maxPosition));
    spinBox->setSingleStep(0.1);
    spinBox->setDecimals(2);
    spinBox->setValue(0.0);
    spinRow->addWidget(new QLabel(tr("0.0")));
    spinRow->addStretch();
    spinRow->addWidget(spinBox);
    spinRow->addStretch();
    spinRow->addWidget(new QLabel(QString::number(maxPosition, 'f', 1)));
    focusLayout->addLayout(spinRow);

    rightCol->addWidget(focusGroup);

    // Calibration table
    auto* tableGroup = new QGroupBox(tr("Calibration Points"));
    auto* tableLayout = new QVBoxLayout(tableGroup);

    table = new QTableWidget(0, 2);
    table->setHorizontalHeaderLabels({tr("Distance (mm)"), tr("Lens Position")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableLayout->addWidget(table, 1);

    // Table buttons
    auto* tableBtnRow = new QHBoxLayout;
    addBtn    = new QPushButton(tr("+ Add Point"));
    deleteBtn = new QPushButton(tr("Delete Selected"));
    saveBtn   = new QPushButton(tr("Save…"));
    tableBtnRow->addWidget(addBtn);
    tableBtnRow->addWidget(deleteBtn);
    tableBtnRow->addStretch();
    tableBtnRow->addWidget(saveBtn);
    tableLayout->addLayout(tableBtnRow);

    rightCol->addWidget(tableGroup, 1);
    root->addLayout(rightCol, 2);

    // ── Connections ───────────────────────────────────────────
    connect(scaleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LensCalibrationDialog::onScaleChanged);

    connect(slider, &QSlider::valueChanged,
            this, &LensCalibrationDialog::onSliderChanged);

    connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &LensCalibrationDialog::onSpinBoxChanged);

    connect(addBtn,    &QPushButton::clicked, this, &LensCalibrationDialog::onAddRow);
    connect(deleteBtn, &QPushButton::clicked, this, &LensCalibrationDialog::onDeleteSelected);
    connect(saveBtn,   &QPushButton::clicked, this, &LensCalibrationDialog::onSave);
}

void LensCalibrationDialog::setSliderRange(const QString& cam)
{
    if (cam == Camera::IMX219)
        maxPosition = 32.0f;
    else
        maxPosition = 15.0f;   // IMX708 default
}

// ─────────────────────────────────────────────────────────────
// Incoming data slots
// ─────────────────────────────────────────────────────────────

void LensCalibrationDialog::onStreamFrame(const QImage& image)
{
    if (image.isNull())
        return;

    streamActive = true;
    lastFrame = image;
    updateVideoLabel(image);
}

void LensCalibrationDialog::onDistanceData(float distanceMm)
{
    lastDistanceMm = distanceMm;
    tofLabel->setText(tr("%1 mm  (%2 cm)")
        .arg(static_cast<int>(distanceMm))
        .arg(distanceMm / 10.0f, 0, 'f', 1));
}

void LensCalibrationDialog::onStreamStopped()
{
    streamActive = false;
    lastFrame = QImage();
    videoLabel->setPixmap(QPixmap());
    videoLabel->setText(tr("Stream stopped"));
}

// ─────────────────────────────────────────────────────────────
// Video scaling
// ─────────────────────────────────────────────────────────────

void LensCalibrationDialog::updateVideoLabel(const QImage& image)
{
    if (image.isNull())
        return;

    if (videoScale == 0.0f)
    {
        // Fit to scroll area viewport
        QSize viewport = scrollArea->viewport()->size();
        QPixmap px = QPixmap::fromImage(image).scaled(
            viewport, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        videoLabel->setPixmap(px);
        videoLabel->resize(viewport);
        scrollArea->setWidgetResizable(true);
    }
    else
    {
        QSize scaled = image.size() * videoScale;
        QPixmap px = QPixmap::fromImage(image).scaled(
            scaled, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        videoLabel->setPixmap(px);
        videoLabel->resize(scaled);
        scrollArea->setWidgetResizable(false);
    }
}

void LensCalibrationDialog::onScaleChanged(int index)
{
    videoScale = scaleCombo->itemData(index).toFloat();
    if (!lastFrame.isNull())
        updateVideoLabel(lastFrame);
}

// ─────────────────────────────────────────────────────────────
// Focus slider / spinbox — keep in sync, debounce send
// ─────────────────────────────────────────────────────────────

void LensCalibrationDialog::onSliderChanged(int value)
{
    float pos = sliderToPosition(value);

    // Update spinbox without triggering its own signal
    QSignalBlocker blocker(spinBox);
    spinBox->setValue(static_cast<double>(pos));

    pendingPosition = pos;
    debounceTimer->start(150);
}

void LensCalibrationDialog::onSpinBoxChanged(double value)
{
    float pos = static_cast<float>(value);

    // Update slider without triggering its own signal
    QSignalBlocker blocker(slider);
    slider->setValue(positionToSlider(pos));

    pendingPosition = pos;
    debounceTimer->start(150);
}

void LensCalibrationDialog::sendLensPosition(float position)
{
    emit lensPositionChangeRequested(position, camera);
}

// ─────────────────────────────────────────────────────────────
// Calibration table
// ─────────────────────────────────────────────────────────────

void LensCalibrationDialog::onAddRow()
{
    float pos = sliderToPosition(slider->value());

    int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(
        QString::number(static_cast<int>(lastDistanceMm))));
    table->setItem(row, 1, new QTableWidgetItem(
        QString::number(static_cast<double>(pos), 'f', 2)));

    table->selectRow(row);
}

void LensCalibrationDialog::onDeleteSelected()
{
    int row = table->currentRow();
    if (row >= 0)
        table->removeRow(row);
}

void LensCalibrationDialog::onSave()
{
    if (table->rowCount() == 0)
    {
        QMessageBox::information(this, tr("Nothing to save"),
            tr("Add at least one calibration point before saving."));
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save Calibration"),
        QString("lens_calibration_%1.json").arg(camera),
        tr("JSON files (*.json)"));

    if (path.isEmpty())
        return;

    QJsonArray points;
    for (int r = 0; r < table->rowCount(); ++r)
    {
        QJsonObject pt;
        pt["distance_mm"]   = table->item(r, 0)->text().toDouble();
        pt["lens_position"] = table->item(r, 1)->text().toDouble();
        points.append(pt);
    }

    QJsonObject root;
    root["camera"]            = camera;
    root["calibration_points"] = points;

    QJsonDocument doc(root);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, tr("Save failed"),
            tr("Could not write to %1").arg(path));
        return;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();

    QMessageBox::information(this, tr("Saved"),
        tr("Calibration saved to:\n%1").arg(path));
}
