// server_connection.cpp
#include "server_connection.h"
#include "logger.h"
#include <QJsonDocument>
#include <QJsonArray>
#include "protocol_constants.h"

namespace
{
    SensorTimingInfo parseTimingFromJson(const QJsonObject &obj)
    {
        SensorTimingInfo info;
        info.hblank = obj["hblank"].toInt();
        info.vblank = obj["vblank"].toInt();
        info.pixelRate = static_cast<int64_t>(obj["pixel_rate"].toDouble());
        info.lineTime_us = obj["line_time_us"].toDouble();
        info.frameTime_us = obj["frame_time_us"].toDouble();
        info.rollingShutter_us = obj["rolling_shutter_us"].toDouble();
        info.activeWidth = obj["active_width"].toInt();
        info.activeHeight = obj["active_height"].toInt();
        info.valid = (info.lineTime_us > 0);
        return info;
    }

    CaptureResultInfo parseCaptureComplete(const QJsonObject& r)
    {
        CaptureResultInfo info;

        info.modality          = r["modality"].toString();
        info.width             = r["width"].toInt();
        info.height            = r["height"].toInt();
        info.actualExposure_us = r["actual_exposure_us"].toInt();
        info.actualGain        = static_cast<float>(r["actual_gain"].toDouble());
        info.frameDuration_us  = static_cast<int64_t>(r["frame_duration_us"].toDouble());

        // Timing (legacy inline fields)
        info.timing.hblank            = r["hblank"].toInt();
        info.timing.vblank            = r["vblank"].toInt();
        info.timing.lineTime_us       = r["line_time_us"].toDouble();
        info.timing.rollingShutter_us = r["rolling_shutter_us"].toDouble();
        info.timing.valid             = (info.timing.lineTime_us > 0);

        // Session identity
        info.sessionId           = r["session_id"].toString();
        info.frameIndex          = static_cast<uint32_t>(r["frame_index"].toInt());
        info.cameraId            = r["camera_id"].toString();
        info.captureTimestamp_us = static_cast<int64_t>(r["capture_timestamp_us"].toDouble());

        // Capture state
        info.aeActive     = r["ae_active"].toBool();
        info.afActive     = r["af_active"].toBool();
        info.lensPosition = static_cast<float>(r["lens_position"].toDouble());

        // LED state
        for (const QJsonValue& v : r["leds"].toArray())
        {
            QJsonObject obj = v.toObject();
            LedStateInfo led;
            led.ledId    = obj["led_id"].toString();
            led.active   = obj["active"].toBool();
            led.drive_ma = static_cast<float>(obj["drive_ma"].toDouble());
            led.ledGpioMode      = obj["led_gpio_mode"].toString();
            led.preFrameDelay_ms = obj["pre_frame_delay_ms"].toInt();
            led.postCaptureOff   = obj["post_capture_off"].toBool();
            info.leds.append(led);
        }

        return info;
    }
}

ServerConnection::ServerConnection(QObject *parent)
    : QObject(parent), m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected, this, &ServerConnection::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ServerConnection::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &ServerConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &ServerConnection::onSocketError);
}

ServerConnection::~ServerConnection()
{
    if (m_socket)
    {
        m_socket->disconnect();
        if (m_socket->state() == QAbstractSocket::ConnectedState)
        {
            m_socket->disconnectFromHost();
        }
    }
}

void ServerConnection::connectToServer(const QString &ip, quint16 port)
{
    LOG_INFO << "Connecting to " << ip.toStdString() << ":" << port << std::endl;
    m_socket->connectToHost(ip, port);
}

void ServerConnection::disconnectFromServer()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
    {
        m_socket->disconnectFromHost();
    }
}

bool ServerConnection::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QString ServerConnection::errorString() const
{
    return m_socket->errorString();
}

void ServerConnection::sendCommand(const QJsonObject &command)
{
    if (!isConnected())
    {
        emit errorOccurred("Not connected to server");
        return;
    }

    QJsonDocument doc(command);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    jsonData.append('\n');
    m_socket->write(jsonData);
}

void ServerConnection::sendDiagRawRequest(const QJsonObject &params)
{
    QJsonObject cmd;
    cmd["command"] = sanuwave::protocol::DiagCommand::RAW_CAPTURE;

    for (auto it = params.begin(); it != params.end(); ++it)
        cmd[it.key()] = it.value();

    sendCommand(cmd);
}

void ServerConnection::sendCameraParameter(const QString &camera, const QString &parameter,
                                           const QVariant &value, const QString &mode)
{
    QJsonObject cmd;
    cmd["command"] = "set_parameter";
    cmd["camera"] = camera;
    cmd["param"] = parameter;
    cmd["mode"] = mode;

    if (value.typeId() == QMetaType::Bool)
        cmd["value"] = value.toBool() ? "true" : "false";
    else if (value.typeId() == QMetaType::Int)
        cmd["value"] = QString::number(value.toInt());
    else if (value.typeId() == QMetaType::Double)
        cmd["value"] = QString::number(value.toDouble());
    else if (value.typeId() == QMetaType::QString)
        cmd["value"] = value.toString();

    sendCommand(cmd);
}

void ServerConnection::onConnected()
{
    LOG_INFO << "Connected to server" << std::endl;
    emit connected();
}

void ServerConnection::onDisconnected()
{
    LOG_INFO << "Disconnected from server" << std::endl;

    m_receiveBuffer.clear();
    m_receivingImage = false;
    m_receivingStreamFrame = false;
    m_receivingUVBFFrame = false;
    m_receivingDiagFrame = false;

    emit disconnected();
}

void ServerConnection::onReadyRead()
{
    m_receiveBuffer.append(m_socket->readAll());
    processReceiveBuffer();
}

// ---------------------------------------------------------------------------
// processReceiveBuffer
//
// Parses newline-delimited JSON from m_receiveBuffer.  When a JSON header
// announces a binary payload, the corresponding processXxxBuffer() helper is
// called immediately (bytes may already be present) and on every subsequent
// onReadyRead until the payload is complete.  Once complete, the helper emits
// the appropriate signal, clears its state, and we loop back to parse the next
// JSON header from whatever bytes remain.
// ---------------------------------------------------------------------------
void ServerConnection::processReceiveBuffer()
{
    while (!m_receiveBuffer.isEmpty())
    {
#ifdef DEBUG_BYTES
        LOG_INFO << "processReceiveBuffer: top of loop"
             << " receivingUVBF=" << m_receivingUVBFFrame
             << " bufferSize=" << m_receiveBuffer.size()
             << " expectedSize=" << m_expectedUVBFFrameSize << std::endl;
#endif
        // --- binary payload mode ---
        if (m_receivingUVBFFrame)
        {
            if (!processUVBFBuffer())
                return;  // still accumulating
            // completed — loop to parse next JSON header from remainder
            continue;
        }
        if (m_receivingStreamFrame)
        {
            if (!processStreamFrameBuffer())
                return;
            continue;
        }
        if (m_receivingDiagFrame)
        {
            if (!processDiagFrameBuffer())
                return;
            continue;
        }
        if (m_receivingImage)
        {
            if (!processImageBuffer())
                return;
            continue;
        }

        // --- JSON framing ---
        int newlinePos = m_receiveBuffer.indexOf('\n');
        if (newlinePos == -1)
            return;  // incomplete JSON line — wait for more data

        QByteArray jsonData = m_receiveBuffer.left(newlinePos);
        m_receiveBuffer.remove(0, newlinePos + 1);

        if (jsonData.trimmed().isEmpty())
            continue;

        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseErr);
        if (doc.isNull() || !doc.isObject())
        {
            // Diagnostic — temporary
            LOG_ERROR << "JSON parse failed: " << parseErr.errorString().toStdString()
                      << " at offset " << parseErr.offset
                      << ", payload size=" << jsonData.size()
                      << ", first 200 bytes: "
                      << jsonData.left(200).toStdString() << std::endl;
            continue;
        }

        // Diagnostic — temporary; log every successful type
        LOG_INFO << "RX type=" << doc.object()["type"].toString().toStdString()
                 << " payload=" << jsonData.size() << " bytes" << std::endl;

        processJsonResponse(doc.object());
        // Loop: if processJsonResponse set a receivingXxx flag, the binary
        // branch at the top of the loop will handle any bytes already present.
    }
}

// ---------------------------------------------------------------------------
// Binary payload helpers
//
// Each helper checks whether m_receiveBuffer has accumulated enough bytes for
// the expected payload.  If not, it returns false (caller should wait for more
// data).  If yes, it extracts exactly expectedSize bytes, advances the buffer,
// clears the receiving flag, emits the signal, and returns true so the caller
// can loop back to parse the next JSON header.
// ---------------------------------------------------------------------------

bool ServerConnection::processUVBFBuffer()
{
    if (m_receiveBuffer.size() < m_expectedUVBFFrameSize)
    {
        //emit uvbfFrameTransferProgress(m_currentUVBFFrameInfo.role,
        //                               m_receiveBuffer.size(),
         //                              m_expectedUVBFFrameSize);
        return false;
    }

    QByteArray payload = m_receiveBuffer.left(m_expectedUVBFFrameSize);
    m_receiveBuffer.remove(0, m_expectedUVBFFrameSize);

    // Clear the flag BEFORE emitting and BEFORE the recursive call,
    // so neither the emit nor processReceiveBuffer() re-enters this branch.
    m_receivingUVBFFrame = false;
    #ifdef DEBUG_BYTES
    LOG_INFO << "processUVBFBuffer: payload first 40: "
         << payload.left(40).toHex(' ').toStdString() << std::endl;
    LOG_INFO << "processUVBFBuffer: payload last 40: "
         << payload.right(40).toHex(' ').toStdString() << std::endl;
    LOG_INFO << "processUVBFBuffer: emitting complete role="
             << m_currentUVBFFrameInfo.role.toStdString()
             << " payload=" << m_expectedUVBFFrameSize << std::endl;
    #endif

                // Verify payload integrity
    int rawPos = payload.indexOf("RAW|");
    #ifdef DEBUG_BYTES
    LOG_INFO << "processUVBFBuffer: role=" << m_currentUVBFFrameInfo.role.toStdString()
            << " payloadSize=" << payload.size()
            << " RAW| at=" << rawPos << std::endl;
    #endif
    if (rawPos == 0) {
        // Find header end (7 pipes)
        int pipeCount = 0;
        int headerEnd = -1;
        for (int i = 0; i < qMin(payload.size(), 256); ++i) {
            if (payload[i] == '|' && ++pipeCount == 7) {
                headerEnd = i + 1;
                break;
            }
        }
        int expectedPixelBytes = m_currentUVBFFrameInfo.imageInfo.width *
                                m_currentUVBFFrameInfo.imageInfo.height * 2;
#ifdef DEBUG_BYTES
        int actualPixelBytes = payload.size() - headerEnd;

        LOG_INFO << "processUVBFBuffer: headerEnd=" << headerEnd
                << " expectedPixelBytes=" << expectedPixelBytes
                << " actualPixelBytes=" << actualPixelBytes
                << " match=" << (actualPixelBytes == expectedPixelBytes) << std::endl;
 #endif
    }
    emit uvbfFrameTransferProgress(m_currentUVBFFrameInfo.role,
                                   payload.size(), m_expectedUVBFFrameSize);
    emit uvbfFrameTransferComplete(m_currentUVBFFrameInfo, payload);

    return true;
}

bool ServerConnection::processStreamFrameBuffer()
{
    if (m_receiveBuffer.size() < m_expectedStreamFrameSize)
        return false;

    QByteArray payload = m_receiveBuffer.left(m_expectedStreamFrameSize);
    m_receiveBuffer.remove(0, m_expectedStreamFrameSize);

    m_receivingStreamFrame = false;

    if (m_currentFrameInfo.modality == sanuwave::protocol::Camera::IMX708_STILL ||
        m_currentFrameInfo.modality == sanuwave::protocol::Camera::IMX219_STILL)
        emit intervalFrameReceived(payload, m_currentFrameInfo);
    else
        emit streamFrameReceived(payload, m_currentFrameInfo);

    return true;
}

bool ServerConnection::processDiagFrameBuffer()
{
    if (m_receiveBuffer.size() < m_expectedDiagFrameSize)
        return false;

    QByteArray payload = m_receiveBuffer.left(m_expectedDiagFrameSize);
    m_receiveBuffer.remove(0, m_expectedDiagFrameSize);

    m_receivingDiagFrame = false;

    m_currentDiagFrame.pixelData.assign(
        reinterpret_cast<const uint8_t *>(payload.constData()),
        reinterpret_cast<const uint8_t *>(payload.constData()) + m_expectedDiagFrameSize);
    emit diagRawFrameReceived(m_currentDiagFrame);

    return true;
}

bool ServerConnection::processImageBuffer()
{
    if (m_receiveBuffer.size() < m_expectedImageSize)
        return false;

    QByteArray payload = m_receiveBuffer.left(m_expectedImageSize);
    m_receiveBuffer.remove(0, m_expectedImageSize);

    m_receivingImage = false;

    emit imageReceived(payload, m_imageModality);

    return true;
}

void ServerConnection::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString errorMsg = m_socket->errorString();
    LOG_ERROR << "Socket error: " << errorMsg.toStdString() << std::endl;
    emit errorOccurred(errorMsg);
}

void ServerConnection::processJsonResponse(const QJsonObject &response)
{
    QString type = response["type"].toString();

    if (type == "error")
    {
        QString message = response["message"].toString();
        LOG_ERROR << "Server error: " << message.toStdString() << std::endl;
        emit serverError(message);
    }
    else if (type == "image")
    {
        m_expectedImageSize = static_cast<int>(response["size"].toDouble());
        m_imageModality = response["modality"].toString();
        m_receivingImage = true;
    }
    else if (type == "stream_frame")
    {
        m_expectedStreamFrameSize = static_cast<int>(response["size"].toDouble());
        m_currentFrameInfo.modality = response["modality"].toString();
        m_currentFrameInfo.format = response["format"].toString();
        m_currentFrameInfo.width = response["width"].toInt();
        m_currentFrameInfo.height = response["height"].toInt();
        m_currentFrameInfo.timestamp = static_cast<uint64_t>(response["timestamp_ms"].toDouble());
        m_receivingStreamFrame = true;
    }
    else if (type == "capture_complete")
    {
        emit captureComplete(parseCaptureComplete(response));
        emit statusReceived("Capture complete", response);
    }
    else if (type == "sensor_timing")
    {
        QString camera = response["camera"].toString();
        SensorTimingInfo timing = parseTimingFromJson(response);
        emit sensorTimingReceived(camera, timing);
    }
    else if (type == "distance_data")
    {
        emit distanceDataReceived(response);
    }
    else if (type == sanuwave::protocol::ResponseType::IMU_DATA) {
        emit imuDataReceived(response);
    }
    else if (type == sanuwave::protocol::ResponseType::IMU_EVENT) {
        emit imuEventReceived(response);
    }
    else if (type == sanuwave::protocol::ResponseType::IMU_REG) {
        emit imuRegReceived(response);
    }
    else if (type == "sensor_temperature")
    {
        emit cameraTemperatureReceived(response);
    }
    else if (type == "uv_data")
    {
        emit uvDataReceived(response);
    }
    else if (type == "als_data")
    {
        emit alsDataReceived(response);
    }
    else if (type == "diag_raw_frame")
    {
        using namespace sanuwave::protocol;
        m_currentDiagFrame = DiagRawFrame{};

        m_currentDiagFrame.camera = response["camera"].toString().toStdString();
        m_currentDiagFrame.frameIndex = static_cast<uint8_t>(response["frame_index"].toInt());
        m_currentDiagFrame.frameCount = static_cast<uint8_t>(response["frame_count"].toInt());
        m_currentDiagFrame.width = static_cast<uint32_t>(response["width"].toInt());
        m_currentDiagFrame.height = static_cast<uint32_t>(response["height"].toInt());
        m_currentDiagFrame.bitsPerPixel = static_cast<uint32_t>(response["bits_per_pixel"].toInt());
        m_currentDiagFrame.sensorBitDepth = static_cast<uint32_t>(response["sensor_bit_depth"].toInt());
        m_currentDiagFrame.bayerPattern = static_cast<BayerPattern>(response["bayer_pattern"].toInt());
        m_currentDiagFrame.pixelFormat = response["pixel_format"].toString().toStdString();
        m_currentDiagFrame.dataSize = static_cast<uint32_t>(response["data_size"].toInt());

        m_currentDiagFrame.roiX = static_cast<uint32_t>(response["roi_x"].toInt(0));
        m_currentDiagFrame.roiY = static_cast<uint32_t>(response["roi_y"].toInt(0));
        m_currentDiagFrame.roiWidth = static_cast<uint32_t>(response["roi_width"].toInt(0));
        m_currentDiagFrame.roiHeight = static_cast<uint32_t>(response["roi_height"].toInt(0));

        QJsonObject meta = response["metadata"].toObject();
        auto &md = m_currentDiagFrame.metadata;
        md.actualExposureUs = static_cast<uint32_t>(meta["actual_exposure_us"].toInt());
        md.actualAnalogGain = static_cast<float>(meta["actual_analog_gain"].toDouble());
        md.actualDigitalGain = static_cast<float>(meta["actual_digital_gain"].toDouble());

        QJsonArray cg = meta["colour_gains"].toArray();
        if (cg.size() >= 2)
        {
            md.colourGains[0] = static_cast<float>(cg[0].toDouble());
            md.colourGains[1] = static_cast<float>(cg[1].toDouble());
        }

        md.colourTemperature = static_cast<uint32_t>(meta["colour_temperature"].toInt());
        md.sensorTimestampNs = static_cast<uint64_t>(meta["sensor_timestamp_ns"].toDouble());
        md.lensShadingApplied = meta["lens_shading_applied"].toBool(false);
        md.aeEnabled = meta["ae_enabled"].toBool(false);
        md.awbEnabled = meta["awb_enabled"].toBool(false);
        md.hblank = meta["hblank"].toInt(0);
        md.vblank = meta["vblank"].toInt(0);
        md.frameDurationUs = static_cast<int64_t>(meta["frame_duration_us"].toDouble());

        QJsonArray bl = meta["sensor_black_levels"].toArray();
        if (bl.size() >= 4)
        {
            for (int i = 0; i < 4; i++)
                md.sensorBlackLevels[i] = bl[i].toInt();
            md.blackLevelsValid = true;
        }

        if (meta.contains("fpa_temperature_k"))
        {
            md.fpaTemperatureK = static_cast<float>(meta["fpa_temperature_k"].toDouble());
            md.auxTemperatureK = static_cast<float>(meta["aux_temperature_k"].toDouble());
            md.agcEnabled = meta["agc_enabled"].toBool(false);
            md.radiometryEnabled = meta["radiometry_enabled"].toBool(false);
        }

        QJsonObject si = response["sensor_info"].toObject();
        m_currentDiagFrame.sensorInfo.name = si["name"].toString().toStdString();
        m_currentDiagFrame.sensorInfo.nativeBitDepth = static_cast<uint32_t>(si["native_bit_depth"].toInt());
        m_currentDiagFrame.sensorInfo.activeAreaWidth = static_cast<uint32_t>(si["active_area_width"].toInt());
        m_currentDiagFrame.sensorInfo.activeAreaHeight = static_cast<uint32_t>(si["active_area_height"].toInt());

        m_expectedDiagFrameSize = static_cast<int>(response["size"].toDouble());
        m_receivingDiagFrame = true;

        LOG_INFO << "Receiving diag raw frame: " << m_currentDiagFrame.width << "x"
                 << m_currentDiagFrame.height << " (" << m_expectedDiagFrameSize
                 << " bytes)" << std::endl;
    }
    else if (type == "diag_raw_error")
    {
        QString camera = response["camera"].toString();
        int errorCode = response["error_code"].toInt();
        QString message = response["error_message"].toString();

        LOG_WARNING << "Diagnostic error for " << camera.toStdString()
                    << ": " << message.toStdString() << std::endl;

        emit diagErrorReceived(camera, errorCode, message);
    }
    else if (type == sanuwave::protocol::ResponseType::UVBF_STARTED)
    {
        const bool isVBlank = (response.value("mode").toString() == "vblank");
        const int frameCount = response["frame_count"].toInt();
 
        QStringList roles;
        for (const QJsonValue& v : response["roles"].toArray())
            roles << v.toString();
 
        if (isVBlank) {
            emit vblankStarted(frameCount, roles);
        } else {
            // existing path
            emit uvbfStarted(response["session_id"].toString());
        }
    }
    else if (type == sanuwave::protocol::ResponseType::UVBF_FRAME_CAPTURED)
    {
        const bool isVBlank = (response.value("mode").toString() == "vblank");
        const QString role  = response[sanuwave::protocol::Param::FRAME_ROLE].toString();
 
        if (isVBlank) {
            emit vblankFrameCaptured(role);
        } else {
            emit uvbfFrameCaptured(role);
        }
    }
    else if (type == sanuwave::protocol::ResponseType::UVBF_ERROR)
    {
        emit uvbfError(response["stage"].toString(),
                       response["reason"].toString());
    }
    else if (type == sanuwave::protocol::ResponseType::UVBF_COMPLETE)
    {
        emit uvbfComplete(response["session_id"].toString());
    }
    else if (type == sanuwave::protocol::ResponseType::UVBF_VBLANK_COMPLETE)
    {
        emit vblankComplete(response);
    }
    else if (type == sanuwave::protocol::ResponseType::FRAME_TRANSFER)
    {
        using namespace sanuwave;

        m_currentUVBFFrameInfo = UVBFFrameInfo{};
        m_currentUVBFFrameInfo.role      = response[protocol::Param::FRAME_ROLE].toString();
        m_currentUVBFFrameInfo.sessionId = response[protocol::Param::SESSION_ID].toString();
        m_currentUVBFFrameInfo.camera    = response[protocol::Param::CAMERA].toString();

        RawImageInfo& img = m_currentUVBFFrameInfo.imageInfo;
        img.width       = response["width"].toInt();
        img.height      = response["height"].toInt();
        img.bitDepth    = response["bit_depth"].toInt(10);
        img.storageBits = 16;   // server always sends 16-bit container
        img.blackLevel  = response["black_level"].toInt(4096);
        img.pattern     = RawBayerDecoder::patternFromString(
                              response["bayer_pattern"].toString("BGGR").toStdString());
        img.exposureUs  = response["exposure_us"].toInt(0);
        m_currentUVBFFrameInfo.captureTimestamp_ms =
            static_cast<uint64_t>(response["capture_timestamp_ms"].toDouble());
        m_currentUVBFFrameInfo.ledOnTimestamp_ms =
            static_cast<uint64_t>(response["led_on_timestamp_ms"].toDouble());
        m_currentUVBFFrameInfo.ledOffTimestamp_ms =
            static_cast<uint64_t>(response["led_off_timestamp_ms"].toDouble());

        m_expectedUVBFFrameSize = static_cast<int>(response[protocol::Param::PAYLOAD_SIZE].toDouble());
        m_receivingUVBFFrame    = true;

        LOG_INFO << "processJsonResponse: frame_transfer role="
                 << m_currentUVBFFrameInfo.role.toStdString()
                 << " camera=" << m_currentUVBFFrameInfo.camera.toStdString()
                 << " " << img.width << "x" << img.height
                 << " expectedSize=" << m_expectedUVBFFrameSize << std::endl;

       // emit uvbfFrameTransferProgress(m_currentUVBFFrameInfo.role, 0, m_expectedUVBFFrameSize);
    }
    else if (type == "led_status")
    {
        emit ledStatusReceived(response);
    }
    else if (type == "status")
    {
        QString message = response["message"].toString();
        LOG_INFO << "Status: " << message.toStdString() << std::endl;

        if (response.contains("imx708_timing"))
        {
            SensorTimingInfo timing = parseTimingFromJson(response["imx708_timing"].toObject());
            emit sensorTimingReceived("imx708", timing);
        }
        if (response.contains("imx219_timing"))
        {
            SensorTimingInfo timing = parseTimingFromJson(response["imx219_timing"].toObject());
            emit sensorTimingReceived("imx219", timing);
        }
        emit statusReceived(message, response);
    }
}
