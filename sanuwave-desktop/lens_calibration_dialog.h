// client/include/lens_calibration_dialog.h
#pragma once

#include <QDialog>
#include <QImage>
#include <QTimer>

class QLabel;
class QSlider;
class QDoubleSpinBox;
class QTableWidget;
class QPushButton;
class QScrollArea;
class QComboBox;
class ServerConnection;

class LensCalibrationDialog : public QDialog
{
    Q_OBJECT

public:
    // camera: "imx708" or "imx219" — determines slider range
    explicit LensCalibrationDialog(ServerConnection* conn,
                                   const QString& camera,
                                   QWidget* parent = nullptr);
    ~LensCalibrationDialog() override = default;

public slots:
    // Connected to ServerConnection's frame signal while dialog is open
    void onStreamFrame(const QImage& image);

    // Connected to ServerConnection's distance data signal
    void onDistanceData(float distanceMm);

    // Called by MainWindow when stream stops
    void onStreamStopped();

signals:
    // Emitted when user moves the slider — MainWindow/ServerConnection sends to server
    void lensPositionChangeRequested(float position, const QString& camera);

private slots:
    void onSliderChanged(int value);
    void onSpinBoxChanged(double value);
    void onAddRow();
    void onDeleteSelected();
    void onSave();
    void onScaleChanged(int index);

private:
    void buildUi();
    void setSliderRange(const QString& camera);
    void updateVideoLabel(const QImage& image);
    void sendLensPosition(float position);

    // Converts slider int (x100) to float position
    float sliderToPosition(int v) const { return v / 100.0f; }
    int   positionToSlider(float p) const { return static_cast<int>(p * 100.0f); }

    ServerConnection* conn;
    QString           camera;

    // Video
    QLabel*      videoLabel    = nullptr;
    QScrollArea* scrollArea    = nullptr;
    float        videoScale    = 0.0f; // 0 = fit-to-window
    QComboBox*   scaleCombo    = nullptr;

    // ToF
    QLabel*      tofLabel      = nullptr;
    float        lastDistanceMm = 0.0f;

    // Focus
    QSlider*       slider      = nullptr;
    QDoubleSpinBox* spinBox    = nullptr;
    float          maxPosition = 15.0f;

    // Debounce
    QTimer*        debounceTimer = nullptr;
    float          pendingPosition = 0.0f;

    // Table
    QTableWidget*  table       = nullptr;
    QPushButton*   addBtn      = nullptr;
    QPushButton*   deleteBtn   = nullptr;
    QPushButton*   saveBtn     = nullptr;

    // Last received frame (for scale changes)
    QImage         lastFrame;
    bool           streamActive = true;
};
