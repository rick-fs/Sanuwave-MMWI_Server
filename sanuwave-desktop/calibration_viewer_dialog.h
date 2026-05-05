// Copyright 2026 Sanuwave Medical LLC.
//
// Portions of this code were generated with Claude.ai and
// reviewed and edited.

#ifndef CALIBRATIONVIEWERDIALOG_H
#define CALIBRATIONVIEWERDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class CalibrationViewerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CalibrationViewerDialog(QWidget* parent = nullptr);
    
    /// Refresh the display from SensorCalibrationStore
    void refreshData();

private:
    void setupUI();
    QWidget* createSensorTab(const QString& sensorModel);
    void populateSensorTree(QTreeWidget* tree, const QString& sensorModel);
    void addMatrixItem(QTreeWidgetItem* parent, const QString& name, 
                       const double* matrix, int rows, int cols);
    void addArrayItem(QTreeWidgetItem* parent, const QString& name,
                      const uint8_t* arr, int count);
    
    QTabWidget* tabWidget;
    QLabel* statusLabel;
    QPushButton* refreshButton;
};

#endif // CALIBRATIONVIEWERDIALOG_H
