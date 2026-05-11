// src/rgb_stream_worker.cpp
//
// Commit 3 - Phase-correlation motion measurement.
//
// When ctx.motion.enabled is true, each preview frame is converted to
// grayscale-float, a centered ROI is extracted, and cv::phaseCorrelate is
// used to compute the sub-pixel translation between the current ROI and a
// reference ROI. Two reference modes are supported:
//
//   "previous"  - compared against the previous frame's ROI (rolling)
//   "anchor"    - compared against the first frame's ROI of this stream
//
// The Euclidean magnitude of the translation is reported as
// StreamFrameMeta::Motion::trans_px and the peak response from
// phaseCorrelate as confidence (0..1, higher == sharper peak). Rotation is
// reserved (0.0) and would require a log-polar transform pass.
//
// Thresholding is the client's responsibility - the server returns the raw
// scalar so thresholds can be tuned without a server release.
//
// State is local to the worker thread (no shared mutables). The Hann window
// is precomputed for the effective ROI size and reused frame-to-frame.
//
// Motion measurement is rgb-only; the thermal worker is a separate function
// and leaves ctx.motion at its default-disabled state.
//
// Copyright 2026 Sanuwave Medical LLC.
#include "rgb_stream_worker.h"
#include "camera_base.h"
#include "ijpeg_encoder.h"
#include "logger.h"
#include "protocol_constants.h"
#include "stream_frame_meta.h"
#include "vblank_strobe_controller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace sanuwave
{

namespace
{

// Per-worker phase-correlation state. Carried on the stack of rgbStreamWorker
// (no heap allocations after init beyond what OpenCV does internally).
struct MotionState
{
    bool       initialized      = false;
    int        effectiveRoiSize = 0;   // actual ROI size after frame-dim clamp
    cv::Mat    hannWindow;             // CV_32F, effectiveRoiSize x effectiveRoiSize
    cv::Mat    prevRoi;                // CV_32F grayscale, normalized to [0,1]
    cv::Mat    anchorRoi;              // CV_32F grayscale, normalized to [0,1]
    bool       loggedRoiClamp   = false;
};

// Convert an arbitrary input frame to a centered, grayscale, float32 ROI
// normalized to [0,1]. Returns an empty Mat if the frame is too small for
// any meaningful ROI.
cv::Mat extractRoi(const cv::Mat& frame, int requestedRoi, MotionState& st)
{
    if (frame.empty() || requestedRoi <= 0)
        return {};

    const int side = std::min({requestedRoi, frame.cols, frame.rows});
    if (side <= 0)
        return {};

    if (side != requestedRoi && !st.loggedRoiClamp)
    {
        LOG_WARNING << "motion: ROI " << requestedRoi
                    << " exceeds frame " << frame.cols << "x" << frame.rows
                    << ", clamping to " << side << std::endl;
        st.loggedRoiClamp = true;
    }

    const int x0 = (frame.cols - side) / 2;
    const int y0 = (frame.rows - side) / 2;
    cv::Mat roi = frame(cv::Rect(x0, y0, side, side));

    cv::Mat gray;
    if (roi.channels() == 1)
        gray = roi;
    else
        cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);

    cv::Mat gray32;
    gray.convertTo(gray32, CV_32F, 1.0 / 255.0);

    // If the effective ROI size changed (first frame, or frame size changed
    // mid-stream), recompute the Hann window and drop the stale references.
    if (st.effectiveRoiSize != side)
    {
        st.effectiveRoiSize = side;
        cv::createHanningWindow(st.hannWindow, cv::Size(side, side), CV_32F);
        st.prevRoi.release();
        st.anchorRoi.release();
        st.initialized = false;
    }

    return gray32;
}

// Run phase correlation and write the result into meta.motion. Returns true
// if motion was successfully measured. Updates st.prevRoi / st.anchorRoi as
// dictated by the configured reference mode.
bool measureMotion(const cv::Mat& currentRoi,
                   MotionState& st,
                   const StreamContext::MotionConfig& cfg,
                   StreamFrameMeta::Motion& outMotion)
{
    using namespace sanuwave::protocol;

    const bool useAnchor = (cfg.reference == MotionReference::ANCHOR);

    cv::Mat reference;
    if (useAnchor)
        reference = st.anchorRoi;
    else
        reference = st.prevRoi;

    // First frame of the stream (or just after a size change): seed the
    // reference and report no measurement.
    if (!st.initialized || reference.empty())
    {
        if (useAnchor)
            st.anchorRoi = currentRoi.clone();
        else
            st.prevRoi = currentRoi.clone();
        st.initialized = true;
        return false;
    }

    double response = 0.0;
    cv::Point2d shift = cv::phaseCorrelate(reference, currentRoi,
                                           st.hannWindow, &response);

    outMotion.valid      = true;
    outMotion.trans_px   = std::hypot(shift.x, shift.y);
    outMotion.rot_deg    = 0.0;     // reserved; not measured by phase correlation
    outMotion.confidence = response;
    outMotion.reference  = useAnchor ? MotionReference::ANCHOR
                                     : MotionReference::PREVIOUS;

    // "previous" mode rolls forward each frame; "anchor" leaves the seed
    // untouched for the life of the stream.
    if (!useAnchor)
        st.prevRoi = currentRoi.clone();

    return true;
}

} // namespace


void rgbStreamWorker(CameraBase* camera, StreamContext ctx)
{
    uint64_t frameCount = 0;
    auto statsStart = std::chrono::steady_clock::now();
    std::string label = ctx.modality + " stream";

    double totalGetFrame_ms  = 0;
    double totalEncode_ms    = 0;
    double totalCallback_ms  = 0;
    double totalMotion_ms    = 0;

    MotionState motionState;

    if (ctx.motion.enabled)
    {
        LOG_INFO << label << ": motion measurement enabled"
                 << " roi=" << ctx.motion.roi_size
                 << " ref=" << ctx.motion.reference << std::endl;
    }

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

        // -------- Motion measurement (optional, rgb-only) --------
        StreamFrameMeta::Motion motionOut;   // valid == false by default
        auto tm0 = std::chrono::steady_clock::now();
        if (ctx.motion.enabled)
        {
            cv::Mat roi = extractRoi(frame, ctx.motion.roi_size, motionState);
            if (!roi.empty())
                measureMotion(roi, motionState, ctx.motion, motionOut);
        }
        auto tm1 = std::chrono::steady_clock::now();

        // -------- JPEG encode --------
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
            meta.width        = ctx.width;
            meta.height       = ctx.height;
            meta.timestamp_ms = ts;
            meta.motion       = motionOut;

            ctx.callback(encoded, meta);
        }
        auto t3 = std::chrono::steady_clock::now();

        totalGetFrame_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalMotion_ms   += std::chrono::duration<double, std::milli>(tm1 - tm0).count();
        totalEncode_ms   += std::chrono::duration<double, std::milli>(t2 - tm1).count();
        totalCallback_ms += std::chrono::duration<double, std::milli>(t3 - t2).count();

        frameCount++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - statsStart).count();
        if (elapsed >= 5000)
        {
            double fps = frameCount * 1000.0 / elapsed;
            LOG_INFO << label << ": " << fps << " fps (" << frameCount << " frames)"
                     << "  getFrame=" << (totalGetFrame_ms  / frameCount) << "ms"
                     << "  motion="   << (totalMotion_ms    / frameCount) << "ms"
                     << "  encode="   << (totalEncode_ms    / frameCount) << "ms"
                     << "  callback=" << (totalCallback_ms  / frameCount) << "ms"
                     << "  total="    << ((totalGetFrame_ms + totalMotion_ms
                                           + totalEncode_ms + totalCallback_ms)
                                          / frameCount) << "ms"
                     << std::endl;
            statsStart       = now;
            frameCount       = 0;
            totalGetFrame_ms = 0;
            totalMotion_ms   = 0;
            totalEncode_ms   = 0;
            totalCallback_ms = 0;
        }
    }

    LOG_DEBUG << label << " thread exiting" << std::endl;
}

} // namespace sanuwave
