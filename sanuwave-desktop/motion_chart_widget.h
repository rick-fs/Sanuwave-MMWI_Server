// motion_chart_widget.h
//
// Scrolling line plot of motion magnitude versus real time, intended as an
// ECG-style strip chart inside the streaming panel. Points older than the
// configured window (default 30 s) are discarded; the x axis represents
// time-since-now and scrolls left as new samples arrive. Y axis auto-
// expands above a fixed baseline when samples exceed it.
//
// Drawn with QPainter; no QtCharts / no third-party deps. Repaint is
// throttled by a QTimer so we do not redraw on every frame at full
// preview rate.
//
// Copyright 2026 Sanuwave Medical LLC.
#ifndef MOTION_CHART_WIDGET_H
#define MOTION_CHART_WIDGET_H

#include <QWidget>
#include <QPointF>
#include <QTimer>
#include <deque>

class MotionChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MotionChartWidget(QWidget* parent = nullptr);

    // Append a sample. ts_ms is the server-side wall-clock timestamp of
    // the frame; matching the frame's timestamp keeps the x axis honest
    // when the network stalls (gaps appear in the trace).
    void addSample(qint64 ts_ms, double value);

    // Drop all samples and repaint.
    void clear();

    // Threshold lines drawn for visual reference. Mirror the values used
    // by MainWindow's hysteresis state machine. Re-call to update.
    void setThresholds(double enterMoving, double exitMoving);

    // Visible time window in milliseconds. Default 30 000.
    void setWindowMs(int ms);

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    // Trim samples older than (now - windowMs_).
    void trimOld();

    std::deque<QPointF> samples_;   // x=ts_ms (qint64 cast to double), y=trans_px

    int     windowMs_      = 30000;
    double  yBaseline_     = 10.0;   // initial y max; auto-expands but never shrinks below this
    double  enterMoving_   = 1.5;
    double  exitMoving_    = 0.5;

    QTimer* repaintTimer_  = nullptr;
};

#endif // MOTION_CHART_WIDGET_H
