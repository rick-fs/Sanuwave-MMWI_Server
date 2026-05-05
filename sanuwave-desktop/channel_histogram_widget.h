// channel_histogram_widget.h
#ifndef CHANNEL_HISTOGRAM_WIDGET_H
#define CHANNEL_HISTOGRAM_WIDGET_H

#include <QWidget>
#include <QColor>
#include <vector>
#include <cstdint>
#include <array>

class ChannelHistogramWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChannelHistogramWidget(const QString& channelName,
                                     const QColor& color,
                                     QWidget* parent = nullptr);

    void setData(const std::vector<uint16_t>& pixelValues,
                 uint32_t bitsPerPixel);
    void clear();

    QSize sizeHint() const override { return QSize(250, 150); }
    QSize minimumSizeHint() const override { return QSize(150, 100); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void computeStats();

    QString channelName;
    QColor  color;

    // Histogram bins (up to 1024 for 10-bit)
    static constexpr int MAX_BINS = 1024;
    std::array<uint32_t, MAX_BINS> bins{};
    int      numBins = 0;
    uint32_t maxBinCount = 0;
    uint32_t maxRawValue = 1023;
    // Statistics
    uint16_t minVal = 0;
    uint16_t maxVal = 0;
    float    mean = 0;
    float    stddev = 0;
    uint32_t totalPixels = 0;
    uint32_t saturatedCount = 0;
    uint32_t blackCount = 0;
    bool     hasData = false;
};

#endif // CHANNEL_HISTOGRAM_WIDGET_H
