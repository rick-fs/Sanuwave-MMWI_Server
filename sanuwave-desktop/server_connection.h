// server_connection.h
#ifndef SERVER_CONNECTION_H
#define SERVER_CONNECTION_H

#include <QObject>
#include <QTcpSocket>
#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include "protocol_constants.h"
#include "diag_raw_frame.h"
#include "raw_bayer_decoding.h"  // RawImageInfo

struct StreamFrameInfo {
    QString format;
    QString modality;
    int width = 0;
    int height = 0;
    uint64_t timestamp = 0;
};

struct SensorTimingInfo {
    int32_t hblank = 0;
    int32_t vblank = 0;
    int64_t pixelRate = 0;
    double lineTime_us = 0.0;
    double frameTime_us = 0.0;
    double rollingShutter_us = 0.0;
    int activeWidth = 0;
    int activeHeight = 0;
    bool valid = false;
};

struct LedStateInfo {
    QString ledId;
    bool    active    = false;
    float   drive_ma  = 0.0f;
    QString ledGpioMode;          // "off" | "torch" | "strobe"
    int     preFrameDelay_ms  = 0;
    bool    postCaptureOff    = false;
};

struct CaptureResultInfo {
    QString  modality;
    int      width              = 0;
    int      height             = 0;
    int32_t  actualExposure_us  = 0;
    float    actualGain         = 0.0f;
    int64_t  frameDuration_us   = 0;
    SensorTimingInfo timing;
    QString  sessionId;
    uint32_t frameIndex         = 0;
    QString  cameraId;
    int64_t  captureTimestamp_us = 0;
    bool     aeActive           = false;
    bool     afActive           = false;
    float    lensPosition       = 0.0f;
    QList<LedStateInfo> leds;
    bool    tofValid            = false;
    int     tofDistance_mm      = 0;
    int     tofSignal_kcps      = 0;
    int     tofStatus           = 0;
};

// Metadata parsed from the frame_transfer JSON header.
// Passed with uvbfFrameTransferComplete so the dialog can write a proper DNG.
struct UVBFFrameInfo
{
    QString role;
    QString sessionId;
    QString camera;
    uint64_t captureTimestamp_ms = 0;
    uint64_t ledOnTimestamp_ms = 0;
    uint64_t ledOffTimestamp_ms = 0;
    sanuwave::RawImageInfo imageInfo; // width, height, bitDepth, storageBits,
                                      // blackLevel, pattern, exposureUs
};

class ServerConnection : public QObject
{
    Q_OBJECT
    
#ifdef ENABLE_TESTS
    friend void testNewlineBugFix();
#endif

public:
    explicit ServerConnection(QObject *parent = nullptr);
    ~ServerConnection();

    void connectToServer(const QString &ip, quint16 port);
    void disconnectFromServer();
    
    bool isConnected() const;
    QString errorString() const;

    void sendCommand(const QJsonObject &command);
    void sendCameraParameter(const QString &camera, const QString &parameter,
                            const QVariant &value, const QString &mode);
    void sendDiagRawRequest(const QJsonObject &params);

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);

    void imageReceived(const QByteArray &data, const QString &modality);
    void streamFrameReceived(const QByteArray &data, const StreamFrameInfo &info);
    void intervalFrameReceived(const QByteArray &data, const StreamFrameInfo &info);
    void cameraTemperatureReceived(const QJsonObject &data);
    void distanceDataReceived(const QJsonObject &data);
    void uvDataReceived(const QJsonObject &data);
    void alsDataReceived(const QJsonObject &data);
    void diagRawFrameReceived(const sanuwave::protocol::DiagRawFrame &frame);
    void diagErrorReceived(const QString &camera, int errorCode, const QString &message);

    void imuDataReceived  (const QJsonObject& data);
    void imuEventReceived (const QJsonObject& event);
    void imuRegReceived   (const QJsonObject& reg);

    void uvbfStarted(const QString& sessionId);
    void uvbfFrameCaptured(const QString& role);
    void uvbfError(const QString& stage, const QString& reason);
    void uvbfComplete(const QString& sessionId);


    void vblankStarted(int frameCount, QStringList roles);
    void vblankFrameCaptured(const QString& role);
    void vblankComplete(QJsonObject summary);

    void uvbfFrameTransferProgress(const QString& role, int bytesReceived, int totalBytes);

    // CHANGED: QByteArray passed by value (not const ref) so Qt can copy it
    // into the event queue when connected with Qt::QueuedConnection.
    void uvbfFrameTransferComplete(const UVBFFrameInfo& frameInfo,
                                   QByteArray rawPayload);

    void statusReceived(const QString &message, const QJsonObject &fullResponse);
    void ledStatusReceived(const QJsonObject& data);
    void serverError(const QString &message);

    void captureComplete(const CaptureResultInfo &info);
    void sensorTimingReceived(const QString &camera, const SensorTimingInfo &timing);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void processReceiveBuffer();
    void processJsonResponse(const QJsonObject &response);

    // Binary payload accumulators — each returns true when the frame is complete.
    // All operate directly on m_receiveBuffer; caller must not touch it afterward
    // if false is returned (still accumulating).
    bool processUVBFBuffer();
    bool processStreamFrameBuffer();
    bool processDiagFrameBuffer();
    bool processImageBuffer();

    QTcpSocket *m_socket;

    QByteArray m_receiveBuffer;

    bool m_receivingImage = false;
    int m_expectedImageSize = 0;
    QString m_imageModality;
    
    bool m_receivingStreamFrame = false;
    int m_expectedStreamFrameSize = 0;
    StreamFrameInfo m_currentFrameInfo;

    bool m_receivingDiagFrame = false;
    int m_expectedDiagFrameSize = 0;
    sanuwave::protocol::DiagRawFrame m_currentDiagFrame;

    bool           m_receivingUVBFFrame    = false;
    int            m_expectedUVBFFrameSize = 0;
    UVBFFrameInfo  m_currentUVBFFrameInfo;
};

#endif // SERVER_CONNECTION_H
