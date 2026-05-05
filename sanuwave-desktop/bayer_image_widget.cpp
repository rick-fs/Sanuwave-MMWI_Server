// bayer_image_widget.cpp
#include "bayer_image_widget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

BayerImageWidget::BayerImageWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void BayerImageWidget::setRawData(const uint8_t* data, uint32_t width,
                                   uint32_t height, uint32_t bpp,
                                   const QString& pattern)
{
    imgWidth = width;
    imgHeight = height;
    bitsPerPixel = bpp;
    bayerPattern = pattern;
    showDemosaic = false;

    // Unpack to 16-bit array
    size_t totalPixels = static_cast<size_t>(width) * height;
    pixels.resize(totalPixels);

    // Assume unpacked 16-bit little-endian storage
    for (size_t i = 0; i < totalPixels; ++i) {
        size_t byteIdx = i * 2;
        pixels[i] = static_cast<uint16_t>(data[byteIdx])
                   | (static_cast<uint16_t>(data[byteIdx + 1]) << 8);
    }

    dirty = true;
    setMinimumSize(static_cast<int>(width * zoom),
                   static_cast<int>(height * zoom));
    update();
}

void BayerImageWidget::setDemosaicImage(const QImage& image)
{
    demosaicImage = image;
    showDemosaic = true;
    setMinimumSize(static_cast<int>(image.width() * zoom),
                   static_cast<int>(image.height() * zoom));
    update();
}

void BayerImageWidget::setRenderMode(RenderMode mode)
{
    if (renderMode != mode) {
        renderMode = mode;
        dirty = true;
        update();
    }
}

void BayerImageWidget::setZoom(float z)
{
    zoom = std::clamp(z, 0.1f, 8.0f);
    int w = showDemosaic ? demosaicImage.width() : static_cast<int>(imgWidth);
    int h = showDemosaic ? demosaicImage.height() : static_cast<int>(imgHeight);
    setMinimumSize(static_cast<int>(w * zoom),
                   static_cast<int>(h * zoom));
    update();
}

QSize BayerImageWidget::sizeHint() const
{
    int w = showDemosaic ? demosaicImage.width() : static_cast<int>(imgWidth);
    int h = showDemosaic ? demosaicImage.height() : static_cast<int>(imgHeight);
    return QSize(static_cast<int>(w * zoom),
                 static_cast<int>(h * zoom));
}

// ============================================================================
// Rendering
// ============================================================================

void BayerImageWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom < 1.0f);

    if (showDemosaic && !demosaicImage.isNull())
    {
        QRect dest(0, 0, static_cast<int>(demosaicImage.width() * zoom),
                         static_cast<int>(demosaicImage.height() * zoom));
        painter.drawImage(dest, demosaicImage);
        return;
    }

    if (pixels.empty()) return;

    if (dirty) {
        renderedImage = (renderMode == RenderMode::FalseColor)
                        ? renderBayerFalseColor()
                        : renderBayerGrayscale();
        dirty = false;
    }

    QRect dest(0, 0, static_cast<int>(imgWidth * zoom),
                     static_cast<int>(imgHeight * zoom));
    painter.drawImage(dest, renderedImage);
}

QImage BayerImageWidget::renderBayerFalseColor()
{
    QImage img(imgWidth, imgHeight, QImage::Format_RGB32);
    uint16_t maxVal = (1 << bitsPerPixel) - 1;

    for (uint32_t y = 0; y < imgHeight; ++y) {
        auto* scanline = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (uint32_t x = 0; x < imgWidth; ++x) {
            uint16_t raw = pixels[y * imgWidth + x];
            uint8_t scaled = static_cast<uint8_t>((raw * 255) / maxVal);
            int ch = getChannelAtPixel(x, y);

            switch (ch) {
            case 0: scanline[x] = qRgb(scaled, 0, 0);      break; // R
            case 1:                                                  // Gr
            case 2: scanline[x] = qRgb(0, scaled, 0);      break; // Gb
            case 3: scanline[x] = qRgb(0, 0, scaled);      break; // B
            }
        }
    }
    return img;
}

QImage BayerImageWidget::renderBayerGrayscale()
{
    QImage img(imgWidth, imgHeight, QImage::Format_Grayscale8);
    uint16_t maxVal = (1 << bitsPerPixel) - 1;

    for (uint32_t y = 0; y < imgHeight; ++y) {
        auto* scanline = img.scanLine(y);
        for (uint32_t x = 0; x < imgWidth; ++x) {
            uint16_t raw = pixels[y * imgWidth + x];
            scanline[x] = static_cast<uint8_t>((raw * 255) / maxVal);
        }
    }
    return img;
}

// ============================================================================
// Pixel Inspection
// ============================================================================

void BayerImageWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (showDemosaic) return;  // Pixel inspection only in mosaic mode

    QPoint px = widgetToPixel(event->pos());
    if (px.x() >= 0 && px.x() < static_cast<int>(imgWidth) &&
        px.y() >= 0 && px.y() < static_cast<int>(imgHeight))
    {
        uint16_t val = getPixelValue(px.x(), px.y());
        int ch = getChannelAtPixel(px.x(), px.y());
        emit pixelHovered(px.x(), px.y(), val, ch);
    }
}

void BayerImageWidget::wheelEvent(QWheelEvent* event)
{
    float delta = event->angleDelta().y() > 0 ? 1.25f : 0.8f;
    setZoom(zoom * delta);
    event->accept();
}

uint16_t BayerImageWidget::getPixelValue(int x, int y) const
{
    size_t idx = static_cast<size_t>(y) * imgWidth + x;
    return (idx < pixels.size()) ? pixels[idx] : 0;
}

int BayerImageWidget::getChannelAtPixel(int x, int y) const
{
    int bx = x % 2;
    int by = y % 2;

    if (bayerPattern == "RGGB") {
        if (by == 0) return (bx == 0) ? 0 : 1;   // R  Gr
        else         return (bx == 0) ? 2 : 3;    // Gb B
    } else if (bayerPattern == "BGGR") {
        if (by == 0) return (bx == 0) ? 3 : 2;    // B  Gb
        else         return (bx == 0) ? 1 : 0;    // Gr R
    } else if (bayerPattern == "GRBG") {
        if (by == 0) return (bx == 0) ? 1 : 0;    // Gr R
        else         return (bx == 0) ? 3 : 2;    // B  Gb
    } else if (bayerPattern == "GBRG") {
        if (by == 0) return (bx == 0) ? 2 : 3;    // Gb B
        else         return (bx == 0) ? 0 : 1;    // R  Gr
    }
    return -1;
}

QPoint BayerImageWidget::widgetToPixel(const QPoint& widgetPos) const
{
    return QPoint(static_cast<int>(widgetPos.x() / zoom),
                  static_cast<int>(widgetPos.y() / zoom));
}
