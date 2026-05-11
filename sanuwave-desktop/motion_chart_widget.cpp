// motion_chart_widget.cpp
//
// Copyright 2026 Sanuwave Medical LLC.
#include "motion_chart_widget.h"

#include <QDateTime>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>

#include <algorithm>
#include <cmath>

namespace {
// Layout constants. Left margin holds y-axis tick labels; bottom margin
// holds x-axis time-since-now labels.
constexpr int LEFT_MARGIN   = 44;
constexpr int RIGHT_MARGIN  = 8;
constexpr int TOP_MARGIN    = 8;
constexpr int BOTTOM_MARGIN = 22;
constexpr int REPAINT_HZ    = 10;
}

MotionChartWidget::MotionChartWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);

    // Repaint at fixed 10 Hz regardless of inbound frame rate. Higher than
    // ~20 Hz buys nothing the eye can see; lower than ~5 Hz looks choppy.
    repaintTimer_ = new QTimer(this);
    repaintTimer_->setInterval(1000 / REPAINT_HZ);
    connect(repaintTimer_, &QTimer::timeout, this, [this]() {
        trimOld();
        update();
    });
    repaintTimer_->start();
}

void MotionChartWidget::addSample(qint64 ts_ms, double value)
{
    if (!std::isfinite(value))
        return;
    samples_.emplace_back(static_cast<double>(ts_ms), value);
    // Bound the deque defensively in case the trim timer is starved.
    constexpr size_t HARD_CAP = 4096;
    while (samples_.size() > HARD_CAP)
        samples_.pop_front();
}

void MotionChartWidget::clear()
{
    samples_.clear();
    update();
}

void MotionChartWidget::setThresholds(double enterMoving, double exitMoving)
{
    enterMoving_ = enterMoving;
    exitMoving_  = exitMoving;
    update();
}

void MotionChartWidget::setWindowMs(int ms)
{
    if (ms > 0)
        windowMs_ = ms;
}

void MotionChartWidget::trimOld()
{
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - windowMs_;
    while (!samples_.empty() &&
           static_cast<qint64>(samples_.front().x()) < cutoff)
    {
        samples_.pop_front();
    }
}

void MotionChartWidget::paintEvent(QPaintEvent* /*ev*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // -- background --
    p.fillRect(rect(), QColor(0x1c, 0x26, 0x33));   // dark slate

    const int W = width();
    const int H = height();
    const QRect plotArea(LEFT_MARGIN, TOP_MARGIN,
                         W - LEFT_MARGIN - RIGHT_MARGIN,
                         H - TOP_MARGIN - BOTTOM_MARGIN);
    if (plotArea.width() <= 0 || plotArea.height() <= 0)
        return;

    // -- y range (auto-expand above baseline, never shrink below) --
    double yMax = yBaseline_;
    for (const QPointF& s : samples_)
        if (s.y() > yMax) yMax = s.y();
    yMax *= 1.10;   // headroom so the trace does not peg at the top
    if (yMax < 0.001) yMax = yBaseline_;

    // -- plot rect frame --
    p.setPen(QPen(QColor(0x4a, 0x5a, 0x6f), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plotArea);

    // -- grid + y tick labels (4 horizontal divisions) --
    const QColor gridColor(0x33, 0x44, 0x5a);
    QPen gridPen(gridColor, 1, Qt::DotLine);
    p.setFont(QFont(p.font().family(), 8));
    const QColor labelColor(0xae, 0xc2, 0xd6);

    constexpr int Y_DIVS = 4;
    for (int i = 0; i <= Y_DIVS; ++i)
    {
        double yVal = yMax * i / Y_DIVS;
        int yPx = plotArea.bottom() -
                  static_cast<int>(plotArea.height() * yVal / yMax);
        if (i > 0 && i < Y_DIVS) {
            p.setPen(gridPen);
            p.drawLine(plotArea.left() + 1, yPx, plotArea.right() - 1, yPx);
        }
        p.setPen(labelColor);
        const QString lbl = QString::number(yVal, 'f', yVal < 1.0 ? 2 : 1);
        QRect lblRect(0, yPx - 8, LEFT_MARGIN - 4, 16);
        p.drawText(lblRect, Qt::AlignRight | Qt::AlignVCenter, lbl);
    }

    // y-axis title (rotated)
    p.save();
    p.translate(11, plotArea.center().y());
    p.rotate(-90);
    p.setPen(labelColor);
    p.drawText(QRect(-40, -8, 80, 16), Qt::AlignCenter, "px");
    p.restore();

    // -- x tick labels (-30s, -20s, -10s, now) --
    constexpr int X_DIVS = 3;
    for (int i = 0; i <= X_DIVS; ++i)
    {
        int xPx = plotArea.left() + plotArea.width() * i / X_DIVS;
        if (i > 0 && i < X_DIVS) {
            p.setPen(gridPen);
            p.drawLine(xPx, plotArea.top() + 1, xPx, plotArea.bottom() - 1);
        }
        int secondsBack = (windowMs_ / 1000) * (X_DIVS - i) / X_DIVS;
        QString lbl = (i == X_DIVS) ? QString("now")
                                    : QString("-%1s").arg(secondsBack);
        p.setPen(labelColor);
        QRect lblRect(xPx - 30, plotArea.bottom() + 4, 60, 14);
        p.drawText(lblRect, Qt::AlignCenter, lbl);
    }

    // -- threshold lines --
    auto drawThreshold = [&](double yVal, const QColor& c) {
        if (yVal <= 0 || yVal > yMax) return;
        int yPx = plotArea.bottom() -
                  static_cast<int>(plotArea.height() * yVal / yMax);
        QPen pen(c, 1, Qt::DashLine);
        p.setPen(pen);
        p.drawLine(plotArea.left() + 1, yPx, plotArea.right() - 1, yPx);
    };
    drawThreshold(exitMoving_,  QColor(0x6e, 0xc0, 0x7a));   // green (still)
    drawThreshold(enterMoving_, QColor(0xe4, 0x57, 0x4a));   // red   (moving)

    // -- data trace --
    if (samples_.size() >= 1)
    {
        const qint64 now    = QDateTime::currentMSecsSinceEpoch();
        const qint64 oldest = now - windowMs_;

        auto tsToPx = [&](double ts) -> double {
            double frac = (ts - static_cast<double>(oldest)) /
                          static_cast<double>(windowMs_);
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;
            return plotArea.left() + frac * plotArea.width();
        };
        auto valToPx = [&](double v) -> double {
            double frac = v / yMax;
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;
            return plotArea.bottom() - frac * plotArea.height();
        };

        p.setPen(QPen(QColor(0x6f, 0xd1, 0xff), 1.5));   // cyan trace
        p.setBrush(Qt::NoBrush);

        QPolygonF poly;
        poly.reserve(static_cast<int>(samples_.size()));
        for (const QPointF& s : samples_)
            poly << QPointF(tsToPx(s.x()), valToPx(s.y()));

        // Clip to plot area so a stray point near the edge does not bleed
        // into the margins.
        p.setClipRect(plotArea);
        if (poly.size() == 1)
            p.drawPoint(poly.first());
        else
            p.drawPolyline(poly);
        p.setClipping(false);
    }
}
