// Copyright 2026 Sanuwave Medical LLC.
//
// session_fields.h
//
// Server-side helpers for building capture session metadata at capture time.
// Depends on capture_session.h (LedState) — server only, not shared with client.

#pragma once

#include "capture_session.h"
#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace sanuwave {

// ---------------------------------------------------------------------------
// SessionFields — the per-capture identity and timestamp stamped into
// every capture_complete JSON response.
// ---------------------------------------------------------------------------
struct SessionFields
{
    std::string session_id;
    int64_t     timestamp_us = 0;
};

// Generate a fresh SessionFields. Call once per capture, after
// prepareLedForCapture() has fired so the timestamp is as close
// to actual shutter time as possible.
inline SessionFields buildSessionFields(const std::string& providedId = "")
{
    SessionFields f;
    f.session_id   = providedId.empty() ? generateSessionId() : providedId;
    f.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return f;
}



} // namespace sanuwave
