// src/stream_context.h
// Shared context passed to stream worker threads.

#ifndef STREAM_CONTEXT_H
#define STREAM_CONTEXT_H

#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace sanuwave
{

class IJpegEncoder;
class VBlankStrobeController;

// Callback signature matching CommandHandler::setStreamFrameCallback
using StreamFrameCallback = std::function<void(
    const std::vector<uint8_t>& data,
    const std::string& modality,
    const std::string& format,
    int width, int height,
    uint64_t timestamp)>;

struct StreamContext
{
    std::atomic<bool>&      running;
    int                     quality;
    int                     width;
    int                     height;
    std::string             modality;
    std::string             format;
    IJpegEncoder*           encoder;
    StreamFrameCallback     callback;

    // Optional VBlank strobe controller. nullptr = strobe disabled.
    VBlankStrobeController* strobe = nullptr;
};

} // namespace sanuwave

#endif // STREAM_CONTEXT_H
