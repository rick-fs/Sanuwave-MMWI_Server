// channel_histogram_widget.cpp
#include "channel_histogram_widget.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <algorithm>
#include <numeric>

ChannelHistogramWidget::ChannelHistogramWidget(const QString& name,
                                                 const QColor& col,
                                                 QWidget* parent)
    : QWidget(parent)
    , channelName(name)
    , color(col)
{
    setMinimumSize(minimumSizeHint());
}

void ChannelHistogramWidget::setData(const std::vector<uint16_t>& pixelValues,
                                      uint32_t bitsPerPixel)
{
    bins.fill(0);
    numBins = std::min(1 << static_cast<int>(bitsPerPixel), MAX_BINS);
    totalPixels = static_cast<uint32_t>(pixelValues.size());
    hasData = !pixelValues.empty();
    maxRawValue = (1u << bitsPerPixel) - 1;

    if (!hasData) {
        update();
        return;
    }

    // Scale values into bins — handles case where value range exceeds bin count
    // e.g. 16-bit data (0–65535) mapped into 1024 bins
    for (uint16_t val : pixelValues) {
        int bin = static_cast<int>(static_cast<uint64_t>(val) * (numBins - 1) / maxRawValue);
        bin = std::clamp(bin, 0, numBins - 1);
        bins[bin]++;
    }

    // Find max bin for scaling (ignore first and last bin for better dynamic range)
    maxBinCount = 0;
    for (int i = 1; i < numBins - 1; ++i) {
        maxBinCount = std::max(maxBinCount, bins[i]);
    }
    if (maxBinCount == 0)
        maxBinCount = *std::max_element(bins.begin(), bins.begin() + numBins);

    // Compute stats on raw values (not binned)
    minVal = pixelValues.front();
    maxVal = pixelValues.front();
    double sum = 0;
    saturatedCount = 0;
    blackCount = 0;
    uint16_t ceiling = static_cast<uint16_t>(maxRawValue);

    for (uint16_t val : pixelValues) {
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
        sum += val;
        if (val >= ceiling) saturatedCount++;
        if (val == 0) blackCount++;
    }

    mean = static_cast<float>(sum / totalPixels);

    double variance = 0;
    for (uint16_t val : pixelValues) {
        double diff = val - mean;
        variance += diff * diff;
    }
    stddev = static_cast<float>(std::sqrt(variance / totalPixels));

    update();
}

void ChannelHistogramWidget::clear()
{
    hasData = false;
    bins.fill(0);
    update();
}

void ChannelHistogramWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect r = rect();
    int margin = 4;
    int statsHeight = 42;

    // Background
    p.fillRect(r, QColor(20, 20, 30));

    // Channel label
    p.setPen(color);
    p.setFont(QFont("Sans", 10, QFont::Bold));
    p.drawText(margin, 14, channelName);

    if (!hasData || maxBinCount == 0) {
        p.setPen(QColor(100, 100, 100));
        p.drawText(r, Qt::AlignCenter, "No data");
        return;
    }

    // Histogram area
    QRect histRect(margin, 20, r.width() - 2 * margin,
                   r.height() - 20 - statsHeight - margin);

    // Draw histogram using log scale
    float barWidth = static_cast<float>(histRect.width()) / numBins;
    QColor fillColor = color;
    fillColor.setAlpha(180);
    QColor lineColor = color;
    lineColor.setAlpha(220);

    float logMax = std::log(static_cast<float>(maxBinCount) + 1.0f);

    QPainterPath path;
    path.moveTo(histRect.left(), histRect.bottom());

    for (int i = 0; i < numBins; ++i) {
        float logVal = std::log(static_cast<float>(bins[i]) + 1.0f);
        float h = (logVal / logMax) * histRect.height();
        float x = histRect.left() + i * barWidth;
        path.lineTo(x, histRect.bottom() - h);
    }
    path.lineTo(histRect.right(), histRect.bottom());
    path.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(fillColor);
    p.drawPath(path);

    p.setPen(QPen(lineColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Stats text
    p.setPen(QColor(200, 200, 200));
    p.setFont(QFont("Monospace", 8));
    int ty = histRect.bottom() + 12;
    p.drawText(margin, ty,
               QString("min=%1  max=%2  mean=%3")
                   .arg(minVal).arg(maxVal).arg(mean, 0, 'f', 1));
    p.drawText(margin, ty + 14,
               QString("std=%1  sat=%2  blk=%3")
                   .arg(stddev, 0, 'f', 1)
                   .arg(saturatedCount)
                   .arg(blackCount));

    // Border
    p.setPen(QPen(QColor(60, 60, 80), 1));
    p.drawRect(histRect);
}
