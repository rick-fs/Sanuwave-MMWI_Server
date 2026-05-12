


#include "zoom_image_widget.h"
namespace{
QBrush makeCheckerboard()
{
    QPixmap tile(16, 16);
    tile.fill(QColor(80, 80, 80));
    QPainter p(&tile);
    p.fillRect(0, 0, 8, 8, QColor(60, 60, 60));
    p.fillRect(8, 8, 8, 8, QColor(60, 60, 60));
    return QBrush(tile);
}

}

ZoomImageWidget::ZoomImageWidget(QWidget* parent)
    : QScrollArea(parent)
{
    setAlignment(Qt::AlignCenter);
    setWidgetResizable(false);

    viewport()->setAutoFillBackground(true);
    QPalette p = viewport()->palette();
    p.setBrush(QPalette::Window, makeCheckerboard());
    viewport()->setPalette(p);

    imageLabel = new QLabel;
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    imageLabel->setAutoFillBackground(false);
    setWidget(imageLabel);
}

void ZoomImageWidget::setPixmap(const QPixmap& px)
{
    source     = px;
    zoomFactor = 1.0;
    applyZoom();
}

void ZoomImageWidget::clearImage()
{
    source = {};
    imageLabel->clear();
    imageLabel->resize(0, 0);
}

void ZoomImageWidget::wheelEvent(QWheelEvent* e)
{
    if (source.isNull()) { QScrollArea::wheelEvent(e); return; }
    double delta = e->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
    zoomFactor   = std::clamp(zoomFactor * delta, 0.1, 10.0);
    applyZoom();
    e->accept();
}

void ZoomImageWidget::applyZoom()
{
    if (source.isNull()) return;
    int w = static_cast<int>(source.width()  * zoomFactor);
    int h = static_cast<int>(source.height() * zoomFactor);
    imageLabel->setPixmap(source.scaled(w, h, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
    imageLabel->resize(w, h);
}

