#ifndef STREAM_FRAME_DECODER_H
#define STREAM_FRAME_DECODER_H

#include <QObject>
#include <QImage>
#include <QByteArray>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "server_connection.h"  // for StreamFrameInfo

class StreamFrameDecoder : public QObject
{
    Q_OBJECT

public:
    explicit StreamFrameDecoder(QObject *parent = nullptr);
    ~StreamFrameDecoder();

    void start();
    void stop();

    /// Submit a frame for decoding. If a previous frame hasn't been
    /// decoded yet, it is silently replaced (dropped).
    void submitFrame(const QByteArray &jpegData, const StreamFrameInfo &info);

signals:
    /// Emitted on the decoder thread — connect with Qt::QueuedConnection
    /// so the slot runs on the UI thread.
    void frameDecoded(const QImage &image, const StreamFrameInfo &info);

private:
    void decoderLoop();

    std::thread workerThread;
    std::mutex mutex;
    std::condition_variable condition;

    // Latest pending frame (only one slot — always overwritten)
    QByteArray pendingData;
    StreamFrameInfo pendingInfo;
    bool hasPending = false;

    std::atomic<bool> running{false};

    // Stats
    uint64_t decodedCount = 0;
    uint64_t droppedCount = 0;
    std::chrono::steady_clock::time_point statsStart;
};

#endif // STREAM_FRAME_DECODER_H
