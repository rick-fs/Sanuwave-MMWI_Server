// imageviewerwindow.cpp
#include "imageviewerwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QImageWriter>
#include <QDockWidget>
#include <QCheckBox>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QTransform>
#include <QSettings>
#include <QFormLayout>
#include <QPushButton>
#include "logger.h"
#include "dng_exporter.h" 

ImageViewerWindow::ImageViewerWindow(QWidget* parent)
    : QMainWindow(parent)
    , zoomFactor(-1.0)
    , testModeActive(false)
    , testInstructionGroup(nullptr)
    , testInstructionLabel(nullptr)
{
    setupUI();
    setWindowTitle("Image Viewer");
    resize(1200, 900);
    setupOverlayControls();
}

ImageViewerWindow::~ImageViewerWindow()
{
}

void ImageViewerWindow::setupUI()
{
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Test instruction overlay at top
    testInstructionGroup = new QGroupBox("Test Mode");
    testInstructionGroup->setStyleSheet(
        "QGroupBox {"
        "  background-color: #FFF3E0;"
        "  border: 2px solid #FF9800;"
        "  border-radius: 8px;"
        "  margin-top: 10px;"
        "  padding: 10px;"
        "}"
        "QGroupBox::title {"
        "  color: #E65100;"
        "  font-weight: bold;"
        "  subcontrol-origin: margin;"
        "  left: 10px;"
        "  padding: 0 5px;"
        "}"
    );
    
    QVBoxLayout* testLayout = new QVBoxLayout(testInstructionGroup);
    testInstructionLabel = new QLabel("Waiting for test...");
    testInstructionLabel->setWordWrap(true);
    testInstructionLabel->setAlignment(Qt::AlignCenter);
    testInstructionLabel->setStyleSheet(
        "font-size: 18px;"
        "font-weight: bold;"
        "color: #E65100;"
        "padding: 15px;"
        "min-height: 50px;"
    );
    testLayout->addWidget(testInstructionLabel);
    testInstructionGroup->hide();
    mainLayout->addWidget(testInstructionGroup);
    
    infoLabel = new QLabel("No image loaded");
    infoLabel->setStyleSheet("QLabel { padding: 8px; background-color: #34495e; color: white; font-weight: bold; }");
    infoLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(infoLabel);
    
    scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(false);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setStyleSheet("QScrollArea { background-color: #2c3e50; }");
    
    imageWidget = new ImageDisplayWidget();
    imageWidget->setStyleSheet("ImageDisplayWidget { background-color: #2c3e50; }");
    
    scrollArea->setWidget(imageWidget);
    mainLayout->addWidget(scrollArea);
    
    toolbar = new QToolBar("Image Tools");
    toolbar->setMovable(false);
    addToolBar(Qt::TopToolBarArea, toolbar);
    
    zoomInAction = new QAction("+ Zoom In", this);
    zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAction, &QAction::triggered, this, &ImageViewerWindow::onZoomIn);
    toolbar->addAction(zoomInAction);
    
    zoomOutAction = new QAction("- Zoom Out", this);
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction, &QAction::triggered, this, &ImageViewerWindow::onZoomOut);
    toolbar->addAction(zoomOutAction);
    
    zoomFitAction = new QAction("Fit to Window", this);
    connect(zoomFitAction, &QAction::triggered, this, &ImageViewerWindow::onZoomFit);
    toolbar->addAction(zoomFitAction);
    
    zoomActualAction = new QAction("1:1 Actual Size", this);
    connect(zoomActualAction, &QAction::triggered, this, &ImageViewerWindow::onZoomActual);
    toolbar->addAction(zoomActualAction);
    
    toolbar->addSeparator();
    
    toolbar->addWidget(new QLabel(" Zoom: "));
    zoomSlider = new QSlider(Qt::Horizontal);
    zoomSlider->setRange(10, 500);
    zoomSlider->setValue(100);
    zoomSlider->setFixedWidth(200);
    zoomSlider->setTickPosition(QSlider::TicksBelow);
    zoomSlider->setTickInterval(50);
    connect(zoomSlider, &QSlider::valueChanged, this, &ImageViewerWindow::onZoomChanged);
    toolbar->addWidget(zoomSlider);
    
    zoomSpinBox = new QSpinBox();
    zoomSpinBox->setRange(10, 500);
    zoomSpinBox->setValue(100);
    zoomSpinBox->setSuffix("%");
    connect(zoomSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), [this](int value) {
        zoomSlider->setValue(value);
    });
    toolbar->addWidget(zoomSpinBox);
    
    toolbar->addSeparator();
    
    saveAction = new QAction("Save Image", this);
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &ImageViewerWindow::onSaveImage);
    toolbar->addAction(saveAction);
    
    saveDngAction = new QAction("Save as DNG...", this);
    saveDngAction->setEnabled(false);
    saveDngAction->setToolTip("No RAW data available");
    connect(saveDngAction, &QAction::triggered, this, &ImageViewerWindow::onSaveAsDng);
    toolbar->addAction(saveDngAction);


    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);
    
    QMenu* fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveDngAction);
    fileMenu->addSeparator();
    QAction* closeAction = fileMenu->addAction("&Close Window");
    closeAction->setShortcut(QKeySequence::Close);
    connect(closeAction, &QAction::triggered, this, &QMainWindow::close);
    
    QMenu* viewMenu = menuBar->addMenu("&View");
    viewMenu->addAction(zoomInAction);
    viewMenu->addAction(zoomOutAction);
    viewMenu->addAction(zoomFitAction);
    viewMenu->addAction(zoomActualAction);
    
    statusBar()->showMessage("Ready");

}

// ============================================================================
// TEST INSTRUCTION METHODS
// ============================================================================
void ImageViewerWindow::setTestInstruction(const QString &instruction)
{
    if (testInstructionLabel)
        testInstructionLabel->setText(instruction);
    
    if (!testModeActive)
        setTestMode(true);
}

void ImageViewerWindow::clearTestInstruction()
{
    if (testInstructionLabel)
        testInstructionLabel->setText("Waiting for test...");
}

void ImageViewerWindow::setTestMode(bool enabled)
{
    testModeActive = enabled;
    if (testInstructionGroup)
        testInstructionGroup->setVisible(enabled);
    
    if (enabled) {
        setWindowTitle("Image Viewer [TEST MODE]");
        raise();
        activateWindow();
    } else {
        setWindowTitle("Image Viewer");
        clearTestInstruction();
    }
}

// ============================================================================
// IMAGE METHODS
// ============================================================================
void ImageViewerWindow::setImage(const QPixmap& pixmap, const QString& info)
{
    bool isFirstImage = originalImage.isNull();
    
    originalPixmap = pixmap;
    originalImage = pixmap.toImage();
    
    if (originalImage.format() != QImage::Format_RGB32 && 
        originalImage.format() != QImage::Format_ARGB32)
    {
        originalImage = originalImage.convertToFormat(QImage::Format_RGB32);
    }
    originalImage = applyRotation(originalImage);
    imageInfo = info;
    infoLabel->setText(info);
    
    if (!originalImage.isNull())
    {
        // Only reset zoom on first image load
        if (isFirstImage)
        {
            zoomFactor = -1.0;
            zoomSlider->setValue(100);
            zoomSpinBox->setValue(100);
        }
        
        updateImageDisplay();
        statusBar()->showMessage(QString("Image loaded: %1x%2")
            .arg(originalImage.width()).arg(originalImage.height()));
    }
}
void ImageViewerWindow::updateStreamFrame(const QImage& image, const QString& info)
{
    if (image.isNull())
        return;
    
    originalImage = applyRotation(image);    
    static QString lastInfo;
    if (info != lastInfo)
    {
        infoLabel->setText(info);
        imageInfo = info;
        lastInfo = info;
    }
    
    QImage displayImage;
    
    if (zoomFactor == 1.0)
    {
        displayImage = originalImage;
    }
    else if (zoomFactor == -1.0)
    {
        QSize viewportSize = scrollArea->viewport()->size();
        displayImage = originalImage.scaled(viewportSize, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    else
    {
        int newWidth = int(originalImage.width() * zoomFactor);
        int newHeight = int(originalImage.height() * zoomFactor);
        displayImage = originalImage.scaled(newWidth, newHeight, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    
    imageWidget->setImage(displayImage);
    
    static int frameCount = 0;
    if (++frameCount % 30 == 0)
    {
        statusBar()->showMessage(QString("Streaming: %1x%2 @ %3%")
            .arg(originalImage.width())
            .arg(originalImage.height())
            .arg(zoomFactor == -1.0 ? "Fit" : QString::number(int(zoomFactor * 100))));
    }
}

void ImageViewerWindow::updateDisplay()
{
    if (rgbLayer.isNull())
        return;
    LOG_TRACE << "updateDisplay: overlayEnabled=" << overlayEnabled 
             << " thermalLayer.isNull=" << thermalLayer.isNull() << std::endl;
    
    QImage displayImage;
    
    if (overlayEnabled && !thermalLayer.isNull())
    {
        displayImage = compositeFrames();
    }
    else
    {
        displayImage = rgbLayer;
    }
    
    originalImage = displayImage;
    updateImageDisplay();
    LOG_TRACE << "updateDisplay complete" << std::endl;
}

QImage ImageViewerWindow::compositeFrames()
{
    if (rgbLayer.isNull())
        return QImage();
    
    QImage result = rgbLayer.convertToFormat(QImage::Format_ARGB32);
    
    if (thermalLayer.isNull() || !overlayEnabled)
        return result;
    
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    
    QPointF thermalCenter(thermalLayer.width() / 2.0, thermalLayer.height() / 2.0);
    
    // Calculate base scale to fit thermal over RGB frame.
    // Use the smaller axis ratio so the thermal fully covers the RGB.
    double baseScaleX = static_cast<double>(result.width()) / thermalLayer.width();
    double baseScaleY = static_cast<double>(result.height()) / thermalLayer.height();
    double baseScale = qMin(baseScaleX, baseScaleY);
    
    // thermalScale (from UI) is a multiplier on top of the base fit
    double effectiveScale = baseScale * thermalScale;
    
    QTransform transform;
    double targetX = result.width() / 2.0 + thermalOffset.x();
    double targetY = result.height() / 2.0 + thermalOffset.y();
    transform.translate(targetX, targetY);
    transform.rotate(thermalRotation);
    transform.scale(effectiveScale, effectiveScale);
    transform.translate(-thermalCenter.x(), -thermalCenter.y());
    
    painter.setTransform(transform);
    painter.setOpacity(overlayOpacity);
    painter.drawImage(0, 0, thermalLayer);
    
    painter.end();
    
    return result;
}

void ImageViewerWindow::saveOverlaySettings()
{
    QSettings settings("Sanuwave", "SanuwaveClient");
    settings.beginGroup("ThermalOverlay");
    settings.setValue("enabled", overlayEnabled);
    settings.setValue("opacity", overlayOpacity);
    settings.setValue("offsetX", thermalOffset.x());
    settings.setValue("offsetY", thermalOffset.y());
    settings.setValue("scale", thermalScale);
    settings.setValue("rotation", thermalRotation);
    settings.endGroup();
    
    statusBar()->showMessage("Overlay settings saved", 3000);
}

void ImageViewerWindow::loadOverlaySettings()
{
    QSettings settings("Sanuwave", "SanuwaveClient");
    settings.beginGroup("ThermalOverlay");
    
    overlayEnabled = settings.value("enabled", true).toBool();
    overlayOpacity = settings.value("opacity", 0.5).toDouble();
    thermalOffset.setX(settings.value("offsetX", 0.0).toDouble());
    thermalOffset.setY(settings.value("offsetY", 0.0).toDouble());
    thermalScale = settings.value("scale", 1.0).toDouble();
    thermalRotation = settings.value("rotation", 0.0).toDouble();
    
    settings.endGroup();
    
    if (overlayEnabledCheckBox) overlayEnabledCheckBox->setChecked(overlayEnabled);
    if (opacitySlider) opacitySlider->setValue(static_cast<int>(overlayOpacity * 100));
    if (offsetXSpinBox) offsetXSpinBox->setValue(thermalOffset.x());
    if (offsetYSpinBox) offsetYSpinBox->setValue(thermalOffset.y());
    if (scaleSpinBox) scaleSpinBox->setValue(thermalScale);
    if (rotationSpinBox) rotationSpinBox->setValue(thermalRotation);
}

void ImageViewerWindow::setupOverlayControls()
{
    overlayDock = new QDockWidget("Thermal Overlay", this);
    overlayDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    
    QWidget* container = new QWidget();
    QFormLayout* layout = new QFormLayout(container);
    
    overlayEnabledCheckBox = new QCheckBox();
    overlayEnabledCheckBox->setChecked(overlayEnabled);
    connect(overlayEnabledCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        overlayEnabled = checked;
        if (!rgbLayer.isNull())
            updateDisplay();
    });
    layout->addRow("Enable Overlay:", overlayEnabledCheckBox);
    
    opacitySlider = new QSlider(Qt::Horizontal);
    opacitySlider->setRange(0, 100);
    opacitySlider->setValue(static_cast<int>(overlayOpacity * 100));
    connect(opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        overlayOpacity = value / 100.0;
        if (!rgbLayer.isNull() && overlayEnabled)
            updateDisplay();
    });
    layout->addRow("Opacity:", opacitySlider);
    
    offsetXSpinBox = new QDoubleSpinBox();
    offsetXSpinBox->setRange(-1000, 1000);
    offsetXSpinBox->setValue(thermalOffset.x());
    offsetXSpinBox->setSuffix(" px");
    connect(offsetXSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            this, [this](double val) {
        thermalOffset.setX(val);
        if (!rgbLayer.isNull() && overlayEnabled)
            updateDisplay();
    });
    layout->addRow("Offset X:", offsetXSpinBox);
    
    offsetYSpinBox = new QDoubleSpinBox();
    offsetYSpinBox->setRange(-1000, 1000);
    offsetYSpinBox->setValue(thermalOffset.y());
    offsetYSpinBox->setSuffix(" px");
    connect(offsetYSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            this, [this](double val) {
        thermalOffset.setY(val);
        if (!rgbLayer.isNull() && overlayEnabled)
            updateDisplay();
    });
    layout->addRow("Offset Y:", offsetYSpinBox);
    
    scaleSpinBox = new QDoubleSpinBox();
    scaleSpinBox->setRange(0.1, 10.0);
    scaleSpinBox->setSingleStep(0.1);
    scaleSpinBox->setValue(thermalScale);
    connect(scaleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            this, [this](double val) {
        thermalScale = val;
        if (!rgbLayer.isNull() && overlayEnabled)
            updateDisplay();
    });
    layout->addRow("Scale:", scaleSpinBox);
    
    rotationSpinBox = new QDoubleSpinBox();
    rotationSpinBox->setRange(-180, 180);
    rotationSpinBox->setSingleStep(1.0);
    rotationSpinBox->setValue(thermalRotation);
    rotationSpinBox->setSuffix("°");
    connect(rotationSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            this, [this](double val) {
        thermalRotation = val;
        if (!rgbLayer.isNull() && overlayEnabled)
            updateDisplay();
    });
    layout->addRow("Rotation:", rotationSpinBox);
    
    QPushButton* saveButton = new QPushButton("Save Settings");
    connect(saveButton, &QPushButton::clicked, this, &ImageViewerWindow::saveOverlaySettings);
    layout->addRow(saveButton);
    
    overlayDock->setWidget(container);
    addDockWidget(Qt::RightDockWidgetArea, overlayDock);
    
    loadOverlaySettings();
}

void ImageViewerWindow::setDualFrame(const QImage& rgb, const QImage& thermal)
{
    LOG_TRACE << "setDualFrame: rgb=" << rgb.width() << "x" << rgb.height()
             << " thermal=" << thermal.width() << "x" << thermal.height() << std::endl;
    rgbLayer = rgb;
    rgbLayer = applyRotation(rgb);
    thermalLayer = thermal;

    updateDisplay();
    LOG_TRACE << "setDualFrame complete" << std::endl;
}

void ImageViewerWindow::updateImageDisplay()
{
    if (originalImage.isNull())
        return;
    
    QImage displayImage;
    Qt::TransformationMode mode = (originalImage.width() > 2000) 
        ? Qt::FastTransformation 
        : Qt::SmoothTransformation;
    if (zoomFactor == -1.0)
    {
        QSize viewportSize = scrollArea->viewport()->size();
        displayImage = originalImage.scaled(viewportSize, Qt::KeepAspectRatio, mode);
    }
    else if (zoomFactor == 1.0)
    {
        displayImage = originalImage;
    }
    else
    {
        int newWidth = int(originalImage.width() * zoomFactor);
        int newHeight = int(originalImage.height() * zoomFactor);
        displayImage = originalImage.scaled(newWidth, newHeight, Qt::KeepAspectRatio, mode);
    }
    
    imageWidget->setImage(displayImage);
    
    statusBar()->showMessage(QString("Zoom: %1% | Display: %2x%3 | Original: %4x%5")
        .arg(zoomFactor == -1.0 ? "Fit" : QString::number(int(zoomFactor * 100)))
        .arg(displayImage.width())
        .arg(displayImage.height())
        .arg(originalImage.width())
        .arg(originalImage.height()));
}

void ImageViewerWindow::onZoomIn()
{
    if (zoomFactor == -1.0)
    {
        zoomFactor = 1.0;
    }
    
    zoomFactor = qMin(zoomFactor + 0.25, 5.0);
    zoomSlider->setValue(int(zoomFactor * 100));
    zoomSpinBox->setValue(int(zoomFactor * 100));
    updateImageDisplay();
}

void ImageViewerWindow::onZoomOut()
{
    if (zoomFactor == -1.0)
    {
        zoomFactor = 1.0;
    }
    
    zoomFactor = qMax(zoomFactor - 0.25, 0.1);
    zoomSlider->setValue(int(zoomFactor * 100));
    zoomSpinBox->setValue(int(zoomFactor * 100));
    updateImageDisplay();
}

void ImageViewerWindow::onZoomFit()
{
    zoomFactor = -1.0;
    updateImageDisplay();
}

void ImageViewerWindow::onZoomActual()
{
    zoomFactor = 1.0;
    zoomSlider->setValue(100);
    zoomSpinBox->setValue(100);
    updateImageDisplay();
}

void ImageViewerWindow::onZoomChanged(int value)
{
    zoomFactor = value / 100.0;
    zoomSpinBox->blockSignals(true);
    zoomSpinBox->setValue(value);
    zoomSpinBox->blockSignals(false);
    updateImageDisplay();
}

void ImageViewerWindow::onSaveImage()
{
    if (originalImage.isNull())
    {
        QMessageBox::warning(this, "No Image", "No image to save.");
        return;
    }
    
    QString fileName = QFileDialog::getSaveFileName(this, 
        "Save Image", 
        "", 
        "PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)");
    
    if (!fileName.isEmpty())
    {
        if (!originalImage.save(fileName))
        {
            QMessageBox::critical(this, "Save Failed", "Failed to save image: " + fileName);
        }
        else
        {
            statusBar()->showMessage("Image saved: " + fileName, 3000);
        }
    }
}


void ImageViewerWindow::setRawData(const sanuwave::RawImageData& rawData)
{
    this->rawData = rawData;

    if (saveDngAction)
    {
        saveDngAction->setEnabled(this->rawData.isValid());
        if (this->rawData.isValid())
        {
            saveDngAction->setToolTip(
                QString("Save %1×%2 %3-bit RAW as DNG (%4)")
                    .arg(this->rawData.width)
                    .arg(this->rawData.height)
                    .arg(this->rawData.bitsPerSample)
                    .arg(QString::fromStdString(this->rawData.cameraModel)));
        }
        else
        {
            saveDngAction->setToolTip("No RAW data available");
        }
    }

    if (this->rawData.isValid())
    {
        statusBar()->showMessage(
            QString("[RAW] %1×%2 %3-bit %4")
                .arg(this->rawData.width)
                .arg(this->rawData.height)
                .arg(this->rawData.bitsPerSample)
                .arg(QString::fromStdString(this->rawData.cameraModel)));
    }
}

void ImageViewerWindow::clearRawData()
{
    rawData.clear();

    if (saveDngAction)
    {
        saveDngAction->setEnabled(false);
        saveDngAction->setToolTip("No RAW data available");
    }
}

void ImageViewerWindow::onSaveAsDng()
{
    if (!rawData.isValid())
    {
        QMessageBox::warning(this, "No RAW Data",
                             "No RAW Bayer data is available for DNG export.\n"
                             "Capture an image with Raw Bayer Mode enabled first.");
        return;
    }

    QString defaultName = QString("capture_%1x%2_%3.dng")
                              .arg(rawData.width)
                              .arg(rawData.height)
                              .arg(QString::fromStdString(rawData.cameraModel).replace(" ", "_"));

    QString fileName = QFileDialog::getSaveFileName(this,
                                                     "Save RAW as DNG",
                                                     defaultName,
                                                     "DNG Files (*.dng);;All Files (*)");

    if (fileName.isEmpty())
        return;

    if (!fileName.toLower().endsWith(".dng"))
        fileName += ".dng";

    QString errorMsg;
    if (sanuwave::DngExporter::writeDng(fileName, rawData, errorMsg))
    {
        statusBar()->showMessage("DNG saved: " + fileName, 5000);
    }
    else
    {
        QMessageBox::critical(this, "Save Failed",
                              "Failed to save DNG file:\n" + errorMsg);
    }
}

void ImageViewerWindow::setRotation180(bool enabled)
{
    rotation180 = enabled;
}

QImage ImageViewerWindow::applyRotation(const QImage& image) const
{
    if (!rotation180 || image.isNull())
        return image;
    return image.transformed(QTransform().rotate(180));
}