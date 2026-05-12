#ifndef ZOOM_IMAGE_WIDGET_
#define ZOOM_IMAGE_WIDGET_
#include <QScrollArea>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QWheelEvent>

class ZoomImageWidget : public QScrollArea
{
    Q_OBJECT
public:
    explicit ZoomImageWidget(QWidget* parent = nullptr);
    void setPixmap(const QPixmap& px);
    void clearImage();

protected:
    void wheelEvent(QWheelEvent* e) override;

private:
    QLabel*  imageLabel = nullptr;
    QPixmap  source;
    double   zoomFactor = 1.0;

    void applyZoom();
};

#endif
