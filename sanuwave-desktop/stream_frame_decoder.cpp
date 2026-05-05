// stream_frame_decoder.cpp
#include "stream_frame_decoder.h"
#include "image_decoding.h"
#include "logger.h"
#include "protocol_constants.h"

namespace StreamFormat = sanuwave::protocol::StreamFormat;

StreamFrameDecoder::StreamFrameDecoder(QObject *parent)
    : QObject(parent)
{
}

StreamFrameDecoder::~StreamFrameDecoder()
{
    stop();
}

void StreamFrameDecoder::start()
{
    if (running)
        return;

    running = true;
    decodedCount = 0;
    droppedCount = 0;
    statsStart = std::chrono::steady_clock::now();

    workerThread = std::thread([this]() { decoderLoop(); });
}

void StreamFrameDecoder::stop()
{
    if (!running)
        return;

    running = false;

    {
        std::lock_guard<std::mutex> lock(mutex);
        condition.notify_one();
    }

    if (workerThread.joinable())
        workerThread.join();

    LOG_TRACE << "StreamFrameDecoder stopped: decoded=" << decodedCount
             << " dropped=" << droppedCount << std::endl;
}

void StreamFrameDecoder::submitFrame(const QByteArray &jpegData,
                                      const StreamFrameInfo &info)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (hasPending)
        droppedCount++;

    pendingData = jpegData;
    pendingInfo = info;
    hasPending = true;

    condition.notify_one();
}

void StreamFrameDecoder::decoderLoop()
{
    LOG_INFO << "StreamFrameDecoder thread running" << std::endl;

    while (running)
    {
        QByteArray data;
        StreamFrameInfo info;

        // Wait for a frame
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [this]() {
                return hasPending || !running;
            });

            if (!running)
                break;

            data = std::move(pendingData);
            info = pendingInfo;
            hasPending = false;
        }

        // Decode outside the lock
        QImage image;

        if (info.format == StreamFormat::JPEG)
        {
            image = sanuwave::ImageDecoding::decodeJpegToImageWithTurboJpeg(data);
        }
        else if (info.format == StreamFormat::RAW)
        {
            QImage temp(reinterpret_cast<const uchar *>(data.constData()),
                        info.width, info.height,
                        info.width * 3, QImage::Format_RGB888);
            image = temp.copy();
        }

        if (image.isNull())
        {
            LOG_ERROR << "StreamFrameDecoder: decode failed" << std::endl;
            continue;
        }

        decodedCount++;

        // Stats every 5 seconds
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - statsStart).count();
        if (elapsed >= 5000)
        {
            double fps = decodedCount * 1000.0 / elapsed;
            LOG_INFO << "Decoder: " << fps << " fps displayed"
                     << "  dropped=" << droppedCount << std::endl;
            decodedCount = 0;
            droppedCount = 0;
            statsStart = now;
        }

        // Deliver to UI thread via signal
        emit frameDecoded(image, info);
    }

    LOG_INFO << "StreamFrameDecoder thread exiting" << std::endl;
}
