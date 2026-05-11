// src/stream_context.h
// Shared context passed to stream worker threads.

#ifndef STREAM_CONTEXT_H
#define STREAM_CONTEXT_H

#include "stream_frame_meta.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sanuwave
{

class IJpegEncoder;
class VBlankStrobeController;

// Callback signature matching CommandHandler::setStreamFrameCallback.
// The meta argument carries everything the wire frame needs, including
// optional per-frame motion measurement (see StreamFrameMeta::Motion).
using StreamFrameCallback = std::function<void(
    const std::vector<uint8_t>& data,
    const StreamFrameMeta&      meta)>;

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

    // Optional per-frame motion measurement.
    // When enabled, the rgb stream worker computes a phase-correlation
    // translation magnitude on a centered ROI of each preview frame and
    // populates StreamFrameMeta::Motion on the way out.
    struct MotionConfig
    {
        bool        enabled   = false;
        int         roi_size  = 512;         // pixels (power-of-two recommended)
        std::string reference = "previous";  // protocol::MotionReference::PREVIOUS | ANCHOR
    } motion;
};

} // namespace sanuwave

#endif // STREAM_CONTEXT_H
