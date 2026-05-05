// Copyright 2026 Sanuwave Medical LLC.
//
// capture_session.h
//
// Shared data structures for capture session and per-frame metadata.
// Server-side: depends on frame_data.h for FrameMetadata (libcamera fields).
// Client-side: include only the structs below LedState/SessionMetadata,
// which have no libcamera dependency.

#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace sanuwave {

// ---------------------------------------------------------------------------
// LedState — snapshot of one LED channel at capture time
// ---------------------------------------------------------------------------
struct LedState
{
    std::string led_id;     // e.g. "led_white_1", "led_uv"
    bool        active = false;
    float       drive_ma = 0.0f;  // actual drive current in mA
};

// ---------------------------------------------------------------------------
// CaptureFrameRecord — session identity + LED state for one captured frame.
// Complements frame_data.h::FrameMetadata (libcamera fields) — does not
// replace it. On the server, both are populated per capture and serialised
// together into the capture_complete JSON response.
// On the client, this struct is reconstructed from that JSON response and
// used by SessionManager to write per-frame sidecar files.
// ---------------------------------------------------------------------------
struct CaptureFrameRecord
{
    // Session identity
    std::string session_id;       // e.g. "20260312_143022_a3f1"
    uint32_t    frame_index = 0;  // 0-based within session, per camera
    std::string camera_id;        // "imx708", "imx219", "lepton"
    std::string modality;         // "rgb", "arducam", "thermal", "raw"

    // Monotonic clock microseconds (server steady_clock)
    int64_t     capture_timestamp_us = 0;

    // LED state at capture time
    std::vector<LedState> leds;

    bool isValid() const { return !session_id.empty() && !camera_id.empty(); }
};

// ---------------------------------------------------------------------------
// SessionMetadata — session-level record written to session.json
// ---------------------------------------------------------------------------
struct SessionMetadata
{
    std::string              session_id;
    int64_t                  start_timestamp_us = 0;  // monotonic
    std::string              start_wall_time;          // ISO-8601 for human readability
    std::string              capture_mode;             // "single", "sequence", "burst"
    std::vector<std::string> camera_ids;

    // Measured I2C delay offsets per camera (us), populated as sessions run
    std::map<std::string, int64_t> camera_delay_offsets_us;

    bool isValid() const { return !session_id.empty(); }
};

// ---------------------------------------------------------------------------
// generateSessionId()
//
// Returns a session ID of the form "YYYYMMDD_HHMMSS_XXXX" where XXXX is a
// 4-character hex suffix from the low bits of the current monotonic clock.
// Call once per session on the server at capture time.
// Assumes POSIX (Linux/Pi) — uses localtime_r.
// ---------------------------------------------------------------------------
std::string generateSessionId();

// Returns the base filename (no extension) for a frame's output files:
//   {session_id}__{camera_id}__f{frame_index:04d}
// e.g. "20260312_143022_a3f1__imx708__f0000"
// Assumes POSIX (Linux/Pi) — uses localtime_r.
std::string frameFileStem(const CaptureFrameRecord& rec);

} // namespace sanuwave
