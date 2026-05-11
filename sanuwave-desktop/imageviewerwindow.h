// imageviewerwindow.h
#ifndef IMAGEVIEWERWINDOW_H
#define IMAGEVIEWERWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPixmap>
#include <QImage>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QSlider>
#include <QSpinBox>
#include <QDockWidget>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPointF>
#include <QGroupBox>
#include "image_decoding.h"
#include "imagedisplaywidget.h"
#include "dng_exporter.h"

class ImageViewerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ImageViewerWindow(QWidget* parent = nullptr);
    ~ImageViewerWindow();
    
    void setImage(const QPixmap& pixmap, const QString& info);
    void setDualFrame(const QImage& rgb, const QImage& thermal);
    void updateStreamFrame(const QImage& image, const QString& info);

    // Motion overlay badge. Floats over the top-right of the scroll-area
    // viewport (pinned to visible area, not the scrolled image). Unknown
    // hides the badge entirely; the MainWindow drives state transitions.
    enum class MotionBadge { Unknown, Still, Moving };
    void setMotionState(MotionBadge state);

    // RAW data storage for DNG export
    void setRawData(const sanuwave::RawImageData& rawData);
    void clearRawData();
    bool hasRawData() const { return rawData.isValid(); }
    void setRotation180(bool enabled);
public slots:
    // Test instruction display
    void setTestInstruction(const QString &instruction);
    void clearTestInstruction();
    void setTestMode(bool enabled);

private slots:
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onZoomActual();
    void onZoomChanged(int value);
    void onSaveImage();
    void onSaveAsDng();

protected:
    // Watches scrollArea->viewport() for resize events so the motion badge
    // can be repositioned to the top-right corner of the visible area.
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void setupUI();
    void updateImageDisplay();
    QImage applyRotation(const QImage& image) const;
    void   positionMotionBadge();

    // UI Components
    QScrollArea* scrollArea;
    ImageDisplayWidget* imageWidget;
    QLabel* infoLabel;

    // Motion overlay badge - parented to scrollArea->viewport() so it
    // stays pinned to the visible area regardless of scroll position.
    QLabel*     motionBadge_      = nullptr;
    MotionBadge motionBadgeState_ = MotionBadge::Unknown;

    // Toolbar
    QToolBar* toolbar;
    QAction* zoomInAction;
    QAction* zoomOutAction;
    QAction* zoomFitAction;
    QAction* zoomActualAction;
    QAction* saveAction;
    QAction* saveDngAction = nullptr;
    QSlider* zoomSlider;
    QSpinBox* zoomSpinBox;
    
    // Image data
    QPixmap originalPixmap;
    QImage originalImage;
    QString imageInfo;
    double zoomFactor;
    bool rotation180 = false;
    QImage rgbLayer;
    QImage thermalLayer;
    bool overlayEnabled = true;
    double overlayOpacity = 0.5;
    QPointF thermalOffset{0.0, 0.0};
    double thermalScale = 1.0;
    double thermalRotation = 0.0;
    
    // Overlay controls
    QDockWidget* overlayDock = nullptr;
    QCheckBox* overlayEnabledCheckBox = nullptr;
    QSlider* opacitySlider = nullptr;
    QDoubleSpinBox* offsetXSpinBox = nullptr;
    QDoubleSpinBox* offsetYSpinBox = nullptr;
    QDoubleSpinBox* scaleSpinBox = nullptr;
    QDoubleSpinBox* rotationSpinBox = nullptr;

    // Test instruction overlay
    QGroupBox* testInstructionGroup;
    QLabel* testInstructionLabel;
    bool testModeActive;

    // RAW data for DNG export
    sanuwave::RawImageData rawData;

    void setupOverlayControls();
    void updateDisplay();
    QImage compositeFrames();
    void saveOverlaySettings();
    void loadOverlaySettings();
};

#endif // IMAGEVIEWERWINDOW_H
