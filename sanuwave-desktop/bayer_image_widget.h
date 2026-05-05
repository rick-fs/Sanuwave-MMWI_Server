// bayer_image_widget.h
#ifndef BAYER_IMAGE_WIDGET_H
#define BAYER_IMAGE_WIDGET_H

#include <QWidget>
#include <QImage>
#include <vector>
#include <cstdint>

class BayerImageWidget : public QWidget
{
    Q_OBJECT

public:
    enum class RenderMode { FalseColor, Grayscale };

    explicit BayerImageWidget(QWidget* parent = nullptr);

    void setRawData(const uint8_t* data, uint32_t width, uint32_t height,
                    uint32_t bitsPerPixel, const QString& bayerPattern);
    void setDemosaicImage(const QImage& image);
    void setRenderMode(RenderMode mode);
    void setZoom(float zoom);

    QSize sizeHint() const override;

signals:
    void pixelHovered(int x, int y, uint16_t rawValue, int channel);
    void roiSelected(QRect roi);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QImage renderBayerFalseColor();
    QImage renderBayerGrayscale();

    uint16_t getPixelValue(int x, int y) const;
    int getChannelAtPixel(int x, int y) const;
    QPoint widgetToPixel(const QPoint& widgetPos) const;

    // Raw data (stored as unpacked 16-bit)
    std::vector<uint16_t> pixels;
    uint32_t imgWidth  = 0;
    uint32_t imgHeight = 0;
    uint32_t bitsPerPixel = 10;
    QString  bayerPattern = "RGGB";

    // Rendering
    RenderMode renderMode = RenderMode::FalseColor;
    float zoom = 1.0f;
    QImage renderedImage;
    QImage demosaicImage;
    bool showDemosaic = false;
    bool dirty = true;
};

#endif // BAYER_IMAGE_WIDGET_H
