// src/rgb_stream_worker.cpp
#include "rgb_stream_worker.h"
#include "camera_base.h"
#include "ijpeg_encoder.h"
#include "logger.h"
#include <chrono>
#include <opencv2/core.hpp>
#include "vblank_strobe_controller.h"
namespace sanuwave
{

void rgbStreamWorker(CameraBase* camera, StreamContext ctx)
{
    uint64_t frameCount = 0;
    auto statsStart = std::chrono::steady_clock::now();
    std::string label = ctx.modality + " stream";

    double totalGetFrame_ms  = 0;
    double totalEncode_ms    = 0;
    double totalCallback_ms  = 0;

    while (ctx.running)
    {
        auto t0 = std::chrono::steady_clock::now();

        FrameMetadata metadata;
        cv::Mat frame = camera->getFrame(metadata);

        auto t1 = std::chrono::steady_clock::now();

        if (frame.empty())
            continue;

        // Notify strobe controller before encoding so the timestamp
        // is posted as early as possible after frame delivery.
        if (ctx.strobe)
            ctx.strobe->onFrame(metadata);

        std::vector<uint8_t> encoded = ctx.encoder->encode(frame, ctx.quality);
        auto t2 = std::chrono::steady_clock::now();

        if (encoded.empty())
            continue;

        if (ctx.callback)
        {
            auto now = std::chrono::system_clock::now();
            uint64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now.time_since_epoch())
                              .count();
            ctx.callback(encoded, ctx.modality, ctx.format,
                         ctx.width, ctx.height, ts);
        }
        auto t3 = std::chrono::steady_clock::now();

        totalGetFrame_ms  += std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalEncode_ms    += std::chrono::duration<double, std::milli>(t2 - t1).count();
        totalCallback_ms  += std::chrono::duration<double, std::milli>(t3 - t2).count();

        frameCount++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - statsStart).count();
        if (elapsed >= 5000)
        {
            double fps = frameCount * 1000.0 / elapsed;
            LOG_INFO << label << ": " << fps << " fps (" << frameCount << " frames)"
                     << "  getFrame=" << (totalGetFrame_ms  / frameCount) << "ms"
                     << "  encode="   << (totalEncode_ms    / frameCount) << "ms"
                     << "  callback=" << (totalCallback_ms  / frameCount) << "ms"
                     << "  total="    << ((totalGetFrame_ms + totalEncode_ms + totalCallback_ms)
                                         / frameCount) << "ms"
                     << std::endl;
            statsStart       = now;
            frameCount       = 0;
            totalGetFrame_ms = 0;
            totalEncode_ms   = 0;
            totalCallback_ms = 0;
        }
    }

    LOG_DEBUG << label << " thread exiting" << std::endl;
}

} // namespace sanuwave
