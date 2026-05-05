// Copyright 2026 Sanuwave Medical LLC.
//
// Portions of this code were generated with Claude.ai and
// reviewed and edited.

#include "calibration_viewer_dialog.h"
#include "sensor_calibration.h"
#include <QHeaderView>
#include <QScrollArea>
#include <QGroupBox>
#include <QGridLayout>
#include <QFont>

CalibrationViewerDialog::CalibrationViewerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("RPi Sensor Calibration Data");
    setMinimumSize(600, 500);
    resize(700, 600);
    
    setupUI();
    refreshData();
}

void CalibrationViewerDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Status label at top
    statusLabel = new QLabel();
    statusLabel->setStyleSheet("QLabel { padding: 5px; background-color: #ecf0f1; border-radius: 3px; }");
    mainLayout->addWidget(statusLabel);
    
    // Tab widget for each sensor
    tabWidget = new QTabWidget();
    mainLayout->addWidget(tabWidget);
    
    // Button row
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    refreshButton = new QPushButton("Refresh");
    connect(refreshButton, &QPushButton::clicked, this, &CalibrationViewerDialog::refreshData);
    buttonLayout->addWidget(refreshButton);
    
    buttonLayout->addStretch();
    
    QPushButton* closeButton = new QPushButton("Close");
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeButton);
    
    mainLayout->addLayout(buttonLayout);
}

QWidget* CalibrationViewerDialog::createSensorTab(const QString& sensorModel)
{
    QWidget* tab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(tab);
    
    QTreeWidget* tree = new QTreeWidget();
    tree->setObjectName(sensorModel + "_tree");
    tree->setHeaderLabels({"Property", "Value"});
    tree->setAlternatingRowColors(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree->setIndentation(20);
    
    // Use monospace font for values
    QFont monoFont("Courier New", 9);
    monoFont.setStyleHint(QFont::Monospace);
    tree->setFont(monoFont);
    
    layout->addWidget(tree);
    
    return tab;
}

void CalibrationViewerDialog::refreshData()
{
    // Clear existing tabs
    while (tabWidget->count() > 0) {
        QWidget* w = tabWidget->widget(0);
        tabWidget->removeTab(0);
        delete w;
    }
    
    auto& store = sanuwave::SensorCalibrationStore::instance();
    
    QStringList sensors = {"imx708", "imx219"};
    int loadedCount = 0;
    
    for (const QString& sensor : sensors) {
        if (store.hasCalibration(sensor.toStdString())) {
            QWidget* tab = createSensorTab(sensor);
            QString displayName = (sensor == "imx708") ? "IMX708 (RGB)" : "IMX219 (Arducam)";
            tabWidget->addTab(tab, displayName);
            
            QTreeWidget* tree = tab->findChild<QTreeWidget*>(sensor + "_tree");
            if (tree) {
                populateSensorTree(tree, sensor);
            }
            loadedCount++;
        }
    }
    
    if (loadedCount == 0) {
        statusLabel->setText("⚠ No calibration data loaded. Using hardcoded defaults.");
        statusLabel->setStyleSheet("QLabel { padding: 5px; background-color: #f39c12; color: white; border-radius: 3px; }");
        
        // Still show tabs with default data
        for (const QString& sensor : sensors) {
            QWidget* tab = createSensorTab(sensor);
            QString displayName = (sensor == "imx708") ? "IMX708 (defaults)" : "IMX219 (defaults)";
            tabWidget->addTab(tab, displayName);
            
            QTreeWidget* tree = tab->findChild<QTreeWidget*>(sensor + "_tree");
            if (tree) {
                populateSensorTree(tree, sensor);
            }
        }
    } else {
        statusLabel->setText(QString("✓ Loaded calibration for %1 sensor(s)").arg(loadedCount));
        statusLabel->setStyleSheet("QLabel { padding: 5px; background-color: #27ae60; color: white; border-radius: 3px; }");
    }
}

void CalibrationViewerDialog::populateSensorTree(QTreeWidget* tree, const QString& sensorModel)
{
    tree->clear();
    
    auto& store = sanuwave::SensorCalibrationStore::instance();
    auto calOpt = store.getCalibration(sensorModel.toStdString());
    
    if (!calOpt.has_value()) {
        QTreeWidgetItem* item = new QTreeWidgetItem(tree);
        item->setText(0, "No data available");
        return;
    }
    
    const auto& cal = *calOpt;
    
    // Basic Info
    QTreeWidgetItem* basicGroup = new QTreeWidgetItem(tree);
    basicGroup->setText(0, "Basic Info");
    basicGroup->setExpanded(true);
    basicGroup->setFirstColumnSpanned(false);
    QFont boldFont = basicGroup->font(0);
    boldFont.setBold(true);
    basicGroup->setFont(0, boldFont);
    
    auto addItem = [](QTreeWidgetItem* parent, const QString& name, const QString& value) {
        QTreeWidgetItem* item = new QTreeWidgetItem(parent);
        item->setText(0, name);
        item->setText(1, value);
    };
    
    addItem(basicGroup, "Sensor Model", QString::fromStdString(cal.sensorModel));
    addItem(basicGroup, "Camera Model", QString::fromStdString(cal.cameraModel));
    addItem(basicGroup, "Bits Per Sample", QString::number(cal.bitsPerSample));
    addItem(basicGroup, "Black Level", QString::number(cal.blackLevel));
    addItem(basicGroup, "White Level", QString::number(cal.whiteLevel));
    
    // CFA Pattern
    QString cfaStr;
    const char* colors[] = {"R", "G", "B"};
    for (int i = 0; i < 4; ++i) {
        if (i > 0) cfaStr += ", ";
        cfaStr += colors[cal.cfaPattern[i]];
    }
    addItem(basicGroup, "CFA Pattern", cfaStr);
    
    // Neutral Point
    addItem(basicGroup, "Neutral R", QString::number(cal.neutralR, 'f', 4));
    addItem(basicGroup, "Neutral B", QString::number(cal.neutralB, 'f', 4));
    
    // D65 Color Calibration
    if (cal.d65.valid) {
        QTreeWidgetItem* d65Group = new QTreeWidgetItem(tree);
        d65Group->setText(0, "D65 Color Calibration (~6500K)");
        d65Group->setExpanded(true);
        d65Group->setFont(0, boldFont);
        
        addItem(d65Group, "Color Temperature", QString::number(cal.d65.colorTemp) + " K");
        addItem(d65Group, "Valid", "Yes");
        
        // Color Matrix (XYZ -> Camera)
        QTreeWidgetItem* matrixItem = new QTreeWidgetItem(d65Group);
        matrixItem->setText(0, "Color Matrix (XYZ → Camera)");
        matrixItem->setExpanded(true);
        
        for (int row = 0; row < 3; ++row) {
            QTreeWidgetItem* rowItem = new QTreeWidgetItem(matrixItem);
            rowItem->setText(0, QString("Row %1").arg(row + 1));
            QString rowStr;
            for (int col = 0; col < 3; ++col) {
                if (col > 0) rowStr += "  ";
                double val = cal.d65.colorMatrix[row * 3 + col];
                rowStr += QString("%1").arg(val, 8, 'f', 4);
            }
            rowItem->setText(1, rowStr);
        }
        
        // CCM if available
        bool hasCcm = false;
        for (int i = 0; i < 9; ++i) {
            if (cal.d65.ccm[i] != 0) { hasCcm = true; break; }
        }
        
        if (hasCcm) {
            QTreeWidgetItem* ccmItem = new QTreeWidgetItem(d65Group);
            ccmItem->setText(0, "CCM (Camera → sRGB)");
            ccmItem->setExpanded(false);
            
            for (int row = 0; row < 3; ++row) {
                QTreeWidgetItem* rowItem = new QTreeWidgetItem(ccmItem);
                rowItem->setText(0, QString("Row %1").arg(row + 1));
                QString rowStr;
                for (int col = 0; col < 3; ++col) {
                    if (col > 0) rowStr += "  ";
                    double val = cal.d65.ccm[row * 3 + col];
                    rowStr += QString("%1").arg(val, 8, 'f', 4);
                }
                rowItem->setText(1, rowStr);
            }
        }
    }
    
    // TL84 Calibration (if available)
    if (cal.tl84.valid) {
        QTreeWidgetItem* tl84Group = new QTreeWidgetItem(tree);
        tl84Group->setText(0, "TL84 Color Calibration (~4000K)");
        tl84Group->setExpanded(false);
        tl84Group->setFont(0, boldFont);
        
        addItem(tl84Group, "Color Temperature", QString::number(cal.tl84.colorTemp) + " K");
        
        QTreeWidgetItem* matrixItem = new QTreeWidgetItem(tl84Group);
        matrixItem->setText(0, "Color Matrix");
        for (int row = 0; row < 3; ++row) {
            QTreeWidgetItem* rowItem = new QTreeWidgetItem(matrixItem);
            rowItem->setText(0, QString("Row %1").arg(row + 1));
            QString rowStr;
            for (int col = 0; col < 3; ++col) {
                if (col > 0) rowStr += "  ";
                double val = cal.tl84.colorMatrix[row * 3 + col];
                rowStr += QString("%1").arg(val, 8, 'f', 4);
            }
            rowItem->setText(1, rowStr);
        }
    }
    
    // Incandescent Calibration (if available)
    if (cal.incandescent.valid) {
        QTreeWidgetItem* incGroup = new QTreeWidgetItem(tree);
        incGroup->setText(0, "Incandescent Color Calibration (~2850K)");
        incGroup->setExpanded(false);
        incGroup->setFont(0, boldFont);
        
        addItem(incGroup, "Color Temperature", QString::number(cal.incandescent.colorTemp) + " K");
        
        QTreeWidgetItem* matrixItem = new QTreeWidgetItem(incGroup);
        matrixItem->setText(0, "Color Matrix");
        for (int row = 0; row < 3; ++row) {
            QTreeWidgetItem* rowItem = new QTreeWidgetItem(matrixItem);
            rowItem->setText(0, QString("Row %1").arg(row + 1));
            QString rowStr;
            for (int col = 0; col < 3; ++col) {
                if (col > 0) rowStr += "  ";
                double val = cal.incandescent.colorMatrix[row * 3 + col];
                rowStr += QString("%1").arg(val, 8, 'f', 4);
            }
            rowItem->setText(1, rowStr);
        }
    }
    
    tree->expandAll();
}

void CalibrationViewerDialog::addMatrixItem(QTreeWidgetItem* parent, const QString& name,
                                             const double* matrix, int rows, int cols)
{
    QTreeWidgetItem* item = new QTreeWidgetItem(parent);
    item->setText(0, name);
    
    for (int r = 0; r < rows; ++r) {
        QTreeWidgetItem* rowItem = new QTreeWidgetItem(item);
        rowItem->setText(0, QString("Row %1").arg(r + 1));
        QString rowStr;
        for (int c = 0; c < cols; ++c) {
            if (c > 0) rowStr += "  ";
            rowStr += QString::number(matrix[r * cols + c], 'f', 4);
        }
        rowItem->setText(1, rowStr);
    }
}

void CalibrationViewerDialog::addArrayItem(QTreeWidgetItem* parent, const QString& name,
                                            const uint8_t* arr, int count)
{
    QTreeWidgetItem* item = new QTreeWidgetItem(parent);
    item->setText(0, name);
    QString str;
    for (int i = 0; i < count; ++i) {
        if (i > 0) str += ", ";
        str += QString::number(arr[i]);
    }
    item->setText(1, str);
}
