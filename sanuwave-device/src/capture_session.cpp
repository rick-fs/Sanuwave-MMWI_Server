// Copyright 2026 Sanuwave Medical LLC.
//
// capture_session.cpp

#include "capture_session.h"

#include <ctime>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace sanuwave {

std::string generateSessionId()
{
    // Wall-clock prefix: YYYYMMDD_HHMMSS
    std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");

    // 4-hex suffix from low 16 bits of monotonic clock microseconds
    // Avoids collisions when two sessions start in the same second
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    uint16_t suffix = static_cast<uint16_t>(now_us & 0xFFFF);

    oss << '_'
        << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << suffix;

    return oss.str();
}

std::string frameFileStem(const CaptureFrameRecord& rec)
{
    std::ostringstream oss;
    oss << rec.session_id
        << "__" << rec.camera_id
        << "__f" << std::setfill('0') << std::setw(4) << rec.frame_index;
    return oss.str();
}

} // namespace sanuwave
