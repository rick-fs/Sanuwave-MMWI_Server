// include/stream_frame_meta.h
// Per-frame metadata produced by streaming workers and consumed by TCPServer.
// Bundled into a struct so adding fields doesn't require touching every callsite.
//
// Copyright 2026 Sanuwave Medical LLC.
#ifndef STREAM_FRAME_META_H
#define STREAM_FRAME_META_H

#include <cstdint>
#include <string>

namespace sanuwave
{

struct StreamFrameMeta
{
    // Required fields (replaces the prior six positional callback arguments).
    std::string modality;
    std::string format;
    int         width        = 0;
    int         height       = 0;
    uint64_t    timestamp_ms = 0;

    // Optional per-frame motion measurement.
    // valid == false means motion was not measured for this frame
    // (feature disabled, first frame of a stream, ROI too large for frame,
    // etc.) -- clients should treat as "unknown", not "still".
    struct Motion
    {
        bool        valid      = false;
        double      trans_px   = 0.0;
        double      rot_deg    = 0.0;
        double      confidence = 0.0;
        std::string reference;   // protocol::MotionReference::PREVIOUS | ANCHOR
    } motion;
};

} // namespace sanuwave

#endif // STREAM_FRAME_META_H
