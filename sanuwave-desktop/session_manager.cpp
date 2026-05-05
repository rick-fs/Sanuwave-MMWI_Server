// Copyright 2026 Sanuwave Medical LLC.
//
// session_manager.cpp

#include "session_manager.h"
#include "logger.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>

SessionManager::SessionManager(QObject* parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// beginSession()
// ---------------------------------------------------------------------------
QString SessionManager::beginSession(const QStringList& cameraIds,
                                     const QString& captureMode)
{
    // End any previous session cleanly
    if (!sessionId.isEmpty())
        endSession();

    // Generate session ID: YYYYMMDD_HHMMSS_XXXX
    QDateTime now = QDateTime::currentDateTime();
    QString datePart = now.toString("yyyyMMdd_HHmmss");
    QString hexSuffix = QString("%1")
        .arg(static_cast<uint16_t>(now.time().msecsSinceStartOfDay() & 0xFFFF),
             4, 16, QChar('0')).toUpper();
    sessionId          = QString("%1_%2").arg(datePart, hexSuffix);
    sessionCameraIds   = cameraIds;
    sessionCaptureMode = captureMode;
    sessionStartTime   = now;
    frameCounters.clear();

    // Create session directory
    if (outputFolder.isEmpty())
    {
        LOG_WARNING << "SessionManager: no output folder set, files will not be saved"
                    << std::endl;
        sessionDir.clear();
        return sessionId;
    }

    sessionDir = QDir(outputFolder).filePath("sessions/" + sessionId);
    QDir dir;
    if (!dir.mkpath(sessionDir))
    {
        QString msg = QString("Failed to create session directory: %1").arg(sessionDir);
        LOG_ERROR << msg.toStdString() << std::endl;
        emit sessionError(msg);
        sessionDir.clear();
        return sessionId;
    }

    writeSessionJson();

    LOG_INFO << "Session started: " << sessionId.toStdString()
             << " -> " << sessionDir.toStdString() << std::endl;

    emit sessionStarted(sessionId, sessionDir);
    return sessionId;
}

// ---------------------------------------------------------------------------
// endSession()
// ---------------------------------------------------------------------------
void SessionManager::endSession()
{
    if (sessionId.isEmpty())
        return;

    // Rewrite session.json with final frame counts
    writeSessionJson();

    LOG_INFO << "Session ended: " << sessionId.toStdString() << std::endl;

    sessionId.clear();
    sessionDir.clear();
    sessionCameraIds.clear();
    frameCounters.clear();
}

// ---------------------------------------------------------------------------
// finalizeFrame()
// ---------------------------------------------------------------------------
QString SessionManager::finalizeFrame(const CaptureResultInfo& info,
                                       const QByteArray& jpegData)
{
    if (sessionDir.isEmpty())
        return {};

    // Assign frame index for this camera
    uint32_t idx = frameCounters.value(info.cameraId, 0);
    frameCounters[info.cameraId] = idx + 1;

    // Build stem: sessionId__cameraId__f0000
    QString stem = buildFrameStem(info, idx);

    // Write JPEG
    QString jpegPath = QDir(sessionDir).filePath(stem + ".jpg");
    QFile jpegFile(jpegPath);
    if (!jpegFile.open(QIODevice::WriteOnly))
    {
        QString msg = QString("Failed to write JPEG: %1").arg(jpegPath);
        LOG_ERROR << msg.toStdString() << std::endl;
        emit sessionError(msg);
        return {};
    }
    jpegFile.write(jpegData);
    jpegFile.close();

    // Write sidecar JSON
    QString sidecarPath = QDir(sessionDir).filePath(stem + ".json");
    QJsonDocument doc(buildFrameSidecar(info, idx));
    QFile sidecarFile(sidecarPath);
    if (sidecarFile.open(QIODevice::WriteOnly))
    {
        sidecarFile.write(doc.toJson(QJsonDocument::Indented));
        sidecarFile.close();
    }
    else
    {
        LOG_WARNING << "Failed to write sidecar: " << sidecarPath.toStdString() << std::endl;
    }

    LOG_INFO << "Frame saved: " << stem.toStdString() << std::endl;
    emit frameSaved(jpegPath, sidecarPath);
    return jpegPath;
}

// ---------------------------------------------------------------------------
// buildFrameStem()
// ---------------------------------------------------------------------------
QString SessionManager::buildFrameStem(const CaptureResultInfo& info,
                                        uint32_t frameIndex) const
{
    return QString("%1__%2__f%3")
        .arg(sessionId)
        .arg(info.cameraId)
        .arg(frameIndex, 4, 10, QChar('0'));
}

// ---------------------------------------------------------------------------
// buildFrameSidecar()
// ---------------------------------------------------------------------------
QJsonObject SessionManager::buildFrameSidecar(const CaptureResultInfo& info,
                                               uint32_t frameIndex) const
{
    QJsonObject obj;

    // ── Session identity ──────────────────────────────────────────────────
    obj["session_id"]           = sessionId;
    obj["frame_index"]          = static_cast<int>(frameIndex);
    obj["camera_id"]            = info.cameraId;
    obj["modality"]             = info.modality;
    obj["capture_timestamp_us"] = static_cast<qint64>(info.captureTimestamp_us);
    obj["wall_time"]            = QDateTime::currentDateTime().toString(Qt::ISODate);

    // ── Geometry ─────────────────────────────────────────────────────────
    obj["width"]  = info.width;
    obj["height"] = info.height;

    // ── Exposure / gain ──────────────────────────────────────────────────
    obj["actual_exposure_us"] = info.actualExposure_us;
    obj["actual_gain"]        = static_cast<double>(info.actualGain);
    obj["ae_active"]          = info.aeActive;

    // ── Focus ────────────────────────────────────────────────────────────
    obj["af_active"]     = info.afActive;
    obj["lens_position"] = static_cast<double>(info.lensPosition);

    // ── Sensor timing ────────────────────────────────────────────────────
    if (info.timing.valid) {
        QJsonObject t;
        t["line_time_us"]       = info.timing.lineTime_us;
        t["rolling_shutter_us"] = info.timing.rollingShutter_us;
        t["frame_time_us"]      = info.timing.frameTime_us;
        t["hblank"]             = info.timing.hblank;
        t["vblank"]             = info.timing.vblank;
        t["pixel_rate_hz"]      = static_cast<qint64>(info.timing.pixelRate);
        t["active_width"]       = info.timing.activeWidth;
        t["active_height"]      = info.timing.activeHeight;
        obj["sensor_timing"]    = t;
    }

    // ── LED state ────────────────────────────────────────────────────────
    QJsonObject ledGpio;
    QJsonArray  ledDrivers;

    for (const LedStateInfo& led : info.leds) {
        if (led.ledId == "gpio_trigger") {
            ledGpio["mode"]               = led.ledGpioMode;
            ledGpio["active"]             = led.active;
            ledGpio["pre_frame_delay_ms"] = led.preFrameDelay_ms;
            ledGpio["post_capture_off"]   = led.postCaptureOff;
        } else {
            QJsonObject l;
            l["led_id"]   = led.ledId;
            l["active"]   = led.active;
            l["drive_ma"] = static_cast<double>(led.drive_ma);
            if (!led.ledGpioMode.isEmpty())
                l["gpio_mode"] = led.ledGpioMode;
            ledDrivers.append(l);
        }
    }

    obj["led_gpio"]    = ledGpio;
    obj["led_drivers"] = ledDrivers;

    // ── ToF distance sensor ──────────────────────────────────────────────
    QJsonObject tof;
    tof["valid"] = info.tofValid;
    if (info.tofValid) {
        tof["distance_mm"] = info.tofDistance_mm;
        tof["signal_kcps"] = info.tofSignal_kcps;
        tof["range_status"] = info.tofStatus;
    }
    obj["tof"] = tof;

    return obj;
}
// ---------------------------------------------------------------------------
// writeSessionJson()
// ---------------------------------------------------------------------------
bool SessionManager::writeSessionJson() const
{
    if (sessionDir.isEmpty())
        return false;

    QJsonObject obj;
    obj["session_id"]    = sessionId;
    obj["start_time"]    = sessionStartTime.toString(Qt::ISODate);
    obj["capture_mode"]  = sessionCaptureMode;

    QJsonArray cameras;
    for (const QString& cam : sessionCameraIds)
        cameras.append(cam);
    obj["cameras"] = cameras;

    // Current frame counts per camera
    QJsonObject counts;
    for (auto it = frameCounters.constBegin(); it != frameCounters.constEnd(); ++it)
        counts[it.key()] = static_cast<int>(it.value());
    obj["frame_counts"] = counts;

    QString path = QDir(sessionDir).filePath("session.json");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
    {
        LOG_WARNING << "Failed to write session.json: " << path.toStdString() << std::endl;
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}
