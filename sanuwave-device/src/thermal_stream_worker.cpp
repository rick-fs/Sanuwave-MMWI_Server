// src/thermal_stream_worker.cpp
#include "thermal_stream_worker.h"
#include "thermal_camera.h"
#include "ijpeg_encoder.h"
#include "logger.h"
#include "stream_frame_meta.h"
#include <chrono>
#include <thread>
#include <opencv2/core.hpp>

namespace sanuwave
{

void thermalStreamWorker(ThermalCamera* camera, StreamContext ctx, int captureScale)
{
    uint64_t frameCount = 0;
    auto statsStart = std::chrono::steady_clock::now();
    std::string label = ctx.modality + " stream";

    double totalCapture_ms = 0;
    double totalEncode_ms = 0;
    double totalCallback_ms = 0;

    while (ctx.running)
    {
        auto t0 = std::chrono::steady_clock::now();
        cv::Mat frame = camera->captureThermalVisualization(captureScale);
        auto t1 = std::chrono::steady_clock::now();

        if (frame.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

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

            StreamFrameMeta meta;
            meta.modality     = ctx.modality;
            meta.format       = ctx.format;
            meta.width        = frame.cols;   // actual frame size, not ctx
            meta.height       = frame.rows;
            meta.timestamp_ms = ts;
            // motion left default (valid==false): thermal does not
            // measure motion. ctx.motion.enabled is also false on this
            // path (see command_handler start*Stream).

            ctx.callback(encoded, meta);
        }
        auto t3 = std::chrono::steady_clock::now();

        totalCapture_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalEncode_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();
        totalCallback_ms += std::chrono::duration<double, std::milli>(t3 - t2).count();

        frameCount++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - statsStart).count();
        if (elapsed >= 5000)
        {
            double fps = frameCount * 1000.0 / elapsed;
            LOG_INFO << label << ": " << fps << " fps (" << frameCount << " frames)"
                     << "  capture=" << (totalCapture_ms / frameCount) << "ms"
                     << "  encode=" << (totalEncode_ms / frameCount) << "ms"
                     << "  callback=" << (totalCallback_ms / frameCount) << "ms"
                     << "  total=" << ((totalCapture_ms + totalEncode_ms + totalCallback_ms) / frameCount) << "ms"
                     << std::endl;
            statsStart = now;
            frameCount = 0;
            totalCapture_ms = 0;
            totalEncode_ms = 0;
            totalCallback_ms = 0;
        }
    }

    LOG_DEBUG << label << " thread exiting" << std::endl;
}

} // namespace sanuwave
