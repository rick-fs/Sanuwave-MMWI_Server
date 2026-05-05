#include "imagedisplaywidget.h"

ImageDisplayWidget::ImageDisplayWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(100, 100);
}

void ImageDisplayWidget::setImage(const QImage& image)
{
    displayImage = image;
    setFixedSize(image.size());
    update();
}

void ImageDisplayWidget::clear()
{
    displayImage = QImage();
    update();
}

QSize ImageDisplayWidget::sizeHint() const
{
    if (!displayImage.isNull())
        return displayImage.size();
    return QSize(640, 480);
}

void ImageDisplayWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    
    if (!displayImage.isNull())
    {
        painter.drawImage(0, 0, displayImage);
    }
    else
    {
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "No Image");
    }
}
