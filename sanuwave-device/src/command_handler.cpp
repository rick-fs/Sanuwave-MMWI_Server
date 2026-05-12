// src/command_handler.cpp
#include "command_handler.h"
#include "as7331_wrapper.h"
#include "frame_data.h"
#include "jpeg_encoder_factory.h"
#include "logger.h"
#include "protocol_constants.h"
#include "sensor_timing_calculator.h"
#include "rgb_stream_worker.h"
#include "thermal_stream_worker.h"
#include "vd6283tx_wrapper.h"
#include <chrono>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>
#include <iomanip>
#include <unistd.h>
#include <opencv2/opencv.hpp>
#include <sstream>
#include "protocol_constants.h"
#include "LedMgr.h"
#include "temp_monitor.h"
#include "param_extractor.h"
#include "session_fields.h"
#include "version.h"
#include "vblank_strobe_controller.h"
#include <fstream>
#include <filesystem>

namespace sanuwave
{

namespace Camera = protocol::Camera;
namespace Command = protocol::Command;
namespace Param = protocol::Param;
namespace Colormap = protocol::Colormap;
namespace StreamFormat = protocol::StreamFormat;
namespace ParamMode = protocol::ParamMode;
namespace ResponseType = protocol::ResponseType;
namespace Modality = protocol::Modality;

// ── SANUWAVE: sysfs helpers for kernel-driven LED strobe ──────────────────

/// Write a string value to a sysfs attribute file.
static bool writeSysfs(const std::string& path, const std::string& value)
{
    std::ofstream f(path);
    if (!f.is_open()) {
        LOG_ERROR << "SANUWAVE sysfs: failed to open " << path << std::endl;
        return false;
    }
    f << value;
    f.close();
    if (f.fail()) {
        LOG_ERROR << "SANUWAVE sysfs: failed to write '" << value
                  << "' to " << path << std::endl;
        return false;
    }
    LOG_INFO << "SANUWAVE sysfs: wrote '" << value << "' to " << path << std::endl;
    return true;
}

#ifdef FUTURE
/// Read a string value from a sysfs attribute file.
static std::string readSysfs(const std::string& path)
{
    std::ifstream f(path);
    std::string value;
    if (f.is_open())
        std::getline(f, value);
    return value;
}
#endif
// ──────────────────────────────────────────────────────────────────────────

// ===========================================================================
// UVBF burst motion measurement
//
// Phase correlation between the illuminated frames of a UVBF burst to
// detect device drift during the capture. Operates on a centered ROI of
// the G1 (green-1) Bayer channel — single-channel, no demosaic needed,
// and the 2-pixel-period Bayer aliasing is sidestepped by taking only
// every other pixel in both axes from the G1 position.
//
// N-aware. For each illum frame at 1-based illum-sequence index k:
//   - k == 1: no measurement (no prior frame to correlate against)
//   - k >= 2: two measurements
//       * prev   = illum_{k-1} -> illum_k  (rolling reference, jitter)
//       * anchor = illum_1     -> illum_k  (fixed reference, cumulative drift)
// For k == 2 these are the same pair; the schema reports them uniformly.
//
// All work happens AFTER the camera burst is complete (post-disarmLeds).
// The motion computation has zero impact on the 400 ms LED-flash-timeout
// budget; it adds only to the transfer-start latency.
//
// The two confidence values let the client decide which measurement to
// trust if one is filtered out by its confidence floor (e.g. anchor over
// a long burst may drift enough to lose confidence while prev stays good).
// ===========================================================================
namespace {

constexpr int UVBF_MOTION_ROI = 512;   // side of centered ROI (G1-sampled)

struct UvbfPairResult
{
    bool   valid       = false;
    double trans_px    = 0.0;
    double confidence  = 0.0;
};

struct UvbfFrameMotion
{
    UvbfPairResult prev;     // illum_{k-1} -> illum_k
    UvbfPairResult anchor;   // illum_1     -> illum_k
};

// Extract a centered ROI from the G1 Bayer channel. For BGGR the G1
// position is (row even, col odd):
//   B G B G ...
//   G R G R ...
// Taking every other pixel pair from G1 gives a single-channel image
// with no Bayer aliasing.
//
// Returned mat is CV_32F single-channel, side x side, normalized to [0,1].
// Empty mat on failure.
cv::Mat extractUvbfMotionRoi(const cv::Mat& bayerImage)
{
    if (bayerImage.empty())
        return {};
    if (bayerImage.type() != CV_16UC1 && bayerImage.type() != CV_8UC1)
    {
        LOG_WARNING << "UVBF motion: unexpected Bayer mat type "
                    << bayerImage.type() << ", skipping" << std::endl;
        return {};
    }

    const int g1W = bayerImage.cols / 2;
    const int g1H = bayerImage.rows / 2;
    const int side = std::min({UVBF_MOTION_ROI, g1W, g1H});
    if (side <= 0)
        return {};

    const int x0_g1 = (g1W - side) / 2;
    const int y0_g1 = (g1H - side) / 2;
    const int x0_orig = x0_g1 * 2 + 1;   // +1 for BGGR G1 column offset
    const int y0_orig = y0_g1 * 2;

    cv::Mat g1(side, side, bayerImage.type());
    if (bayerImage.type() == CV_16UC1)
    {
        for (int y = 0; y < side; ++y)
        {
            const uint16_t* src = bayerImage.ptr<uint16_t>(y0_orig + y * 2)
                                  + x0_orig;
            uint16_t* dst = g1.ptr<uint16_t>(y);
            for (int x = 0; x < side; ++x)
                dst[x] = src[x * 2];
        }
    }
    else // CV_8UC1
    {
        for (int y = 0; y < side; ++y)
        {
            const uint8_t* src = bayerImage.ptr<uint8_t>(y0_orig + y * 2)
                                 + x0_orig;
            uint8_t* dst = g1.ptr<uint8_t>(y);
            for (int x = 0; x < side; ++x)
                dst[x] = src[x * 2];
        }
    }

    cv::Mat gray32;
    const double scale = (bayerImage.type() == CV_16UC1) ? (1.0 / 65535.0)
                                                          : (1.0 / 255.0);
    g1.convertTo(gray32, CV_32F, scale);
    return gray32;
}

// Run phase correlation between two prepared ROIs.
UvbfPairResult uvbfPhaseCorr(const cv::Mat& reference,
                              const cv::Mat& current,
                              const cv::Mat& hannWindow)
{
    UvbfPairResult out;
    if (reference.empty() || current.empty() || hannWindow.empty())
        return out;
    if (reference.size() != current.size() ||
        reference.size() != hannWindow.size())
        return out;

    double response = 0.0;
    cv::Point2d shift = cv::phaseCorrelate(reference, current,
                                            hannWindow, &response);
    out.valid      = true;
    out.trans_px   = std::hypot(shift.x, shift.y);
    out.confidence = response;
    return out;
}

// N-aware motion tracker for a UVBF burst.
//
// Usage:
//   UvbfBurstMotion m;
//   m.addFrame(illum1.image);        // 1-based index assigned in order
//   m.addFrame(illum2.image);
//   ... up to N
//   auto r2 = m.getMotion(2);        // {prev, anchor} for illum2
//   auto r3 = m.getMotion(3);        // {prev, anchor} for illum3
//
// getMotion(k) returns default (valid=false) for k==1 or k out of range.
//
// Failure semantics. If a frame's ROI extraction fails:
//   - That frame's own results are invalid.
//   - The anchor reference is unaffected (set by illum_1).
//   - The prev reference is released, so the NEXT frame's prev result is
//     invalid. The next frame's anchor result is still valid if the
//     anchor survived.
//   - makeHeader requires both prev and anchor to be valid to emit the
//     motion sub-object, so partial-validity frames are reported as "no
//     measurement." Client surfaces that as "Motion check: not available".
class UvbfBurstMotion
{
public:
    void addFrame(const cv::Mat& bayerImage)
    {
        cv::Mat roi = extractUvbfMotionRoi(bayerImage);
        ++frameCount_;

        if (roi.empty())
        {
            // Record an empty slot so 1-based indexing stays aligned.
            // Invalidate prevRoi_ so the next frame's prev measurement is
            // correctly reported as invalid. The anchor is unaffected.
            frameResults_.emplace_back();
            prevRoi_.release();
            return;
        }

        // Lazy init: first valid ROI sets the Hann window and the anchor.
        if (anchorRoi_.empty())
        {
            cv::createHanningWindow(hannWindow_, roi.size(), CV_32F);
            anchorRoi_ = roi.clone();
            prevRoi_   = roi.clone();
            frameResults_.emplace_back();   // illum_1: no measurement
            return;
        }

        UvbfFrameMotion fm;
        fm.prev   = uvbfPhaseCorr(prevRoi_,   roi, hannWindow_);
        fm.anchor = uvbfPhaseCorr(anchorRoi_, roi, hannWindow_);
        frameResults_.push_back(fm);

        // Roll the prev reference forward for the next frame.
        prevRoi_ = roi.clone();
    }

    UvbfFrameMotion getMotion(int illumIndex) const
    {
        if (illumIndex < 2 || illumIndex > static_cast<int>(frameResults_.size()))
            return {};
        return frameResults_[illumIndex - 1];
    }

    int frameCount() const { return frameCount_; }

    void logSummary() const
    {
        if (frameResults_.empty())
        {
            LOG_INFO << "UVBF motion: no frames" << std::endl;
            return;
        }
        for (size_t i = 1; i < frameResults_.size(); ++i)
        {
            const auto& fm = frameResults_[i];
            const int k = static_cast<int>(i) + 1;
            LOG_INFO << "UVBF motion: illum_" << k
                     << " prev "   << fm.prev.trans_px
                     << " (conf "  << fm.prev.confidence  << "), "
                     << "anchor "  << fm.anchor.trans_px
                     << " (conf "  << fm.anchor.confidence << ")"
                     << std::endl;
        }
    }

private:
    cv::Mat hannWindow_;
    cv::Mat anchorRoi_;
    cv::Mat prevRoi_;
    int     frameCount_ = 0;
    std::vector<UvbfFrameMotion> frameResults_;
};

} // anonymous namespace


CommandHandler::CommandHandler(CameraBase *rgbCamera, ThermalCamera *thermalCamera,
                               CameraBase *arducamCamera, VL53L4CDWrapper *distanceSensor,
                               AS7331Wrapper *uvSensor, VD6283TXWrapper *alsSensor,
                               Lsm6ds3trcWrapper* imuSensor,
                               LedMgr *ledManager,
                               LedGpioController *ledGpio,
                               TemperatureMonitor* tempMonitor,
                               std::unique_ptr<MultiCameraParameterHandler> paramHandler)
    : imx708Camera(rgbCamera), thermalCamera(thermalCamera), imx219Camera(arducamCamera),
      distanceSensor(distanceSensor), uvSensor(uvSensor), alsSensor(alsSensor),imuSensor(imuSensor),
      ledManager(ledManager), ledGpio(ledGpio), tempMonitor(tempMonitor),
      multiParams(std::move(paramHandler)), jpegEncoder(JpegEncoderFactory::createDefaultEncoder()),
      distanceRanging(false), streaming(false), dualStreaming(false), streamQuality(80)
{

    strobeController = std::make_unique<VBlankStrobeController>(ledGpio);

    LOG_INFO << "CommandHandler initialized" << std::endl;
    if (arducamCamera)
        LOG_INFO << "  Arducam (IMX219) camera available" << std::endl;
    registerCommands();
}

CommandHandler::~CommandHandler()
{
    if (streaming || dualStreaming)
    {
        streaming = false;
        dualStreaming = false;
        frameCondition.notify_all();
        if (streamWorker.joinable())
            streamWorker.join();
        if (dualStreamWorker.joinable())
            dualStreamWorker.join();
    }
    intervalShooting = false;
    if (intervalStillWorker.joinable())
    {
        intervalStillWorker.join();
    }   
    uvbfActive = false;
    if (uvbfWorker.joinable())
    {
         uvbfWorker.join();   
    }     
    LOG_INFO << "CommandHandler destroyed" << std::endl;
}

void CommandHandler::registerCommands()
{
    // Capture
    router_.registerCommand(Command::CAPTURE_RGB,
                            [this](const auto &p) { return handleCaptureIMX708(p); });
    router_.registerCommand(Command::CAPTURE_RAW,
                            [this](const auto &p) { return handleCaptureRaw(p); });
    router_.registerCommand(Command::CAPTURE_THERMAL,
                            [this](const auto &p) { return handleCaptureThermal(p); });

    router_.registerCommand(Command::CAPTURE_ARDUCAM_CUSTOM,
                            [this](const auto &p) { return handleCaptureIMX219Custom(p); });

    // Streaming
    router_.registerCommand(Command::STREAM_START,
                            [this](const auto &p) { return handleStreamStart(p); });
    router_.registerCommand(Command::STREAM_STOP,
                            [this](const auto &p) { return handleStreamStop(p); });

    // Parameters
    router_.registerCommand(Command::SET_CAMERA_PARAM,
                            [this](const auto &p) { return handleSetParam(p); });
    router_.registerCommand(Command::GET_PARAMS,
                            [this](const auto &p) { return handleGetParams(p); });
    router_.registerCommand(Command::GET_ALL_PARAMS,
                            [this](const auto &p) { return handleGetAllParams(p); });
    router_.registerCommand(Command::RESET_SETTINGS,
                            [this](const auto &p) { return handleResetSettings(p); });
    router_.registerCommand(Command::SET_LENS_POSITION,
                            [this](const auto& p) { return handleSetLensPosition(p); });    
    // Distance sensor
    router_.registerCommand(Command::DISTANCE_INIT,
                            [this](const auto &p) { return handleDistanceInit(p); });
    router_.registerCommand(Command::DISTANCE_START,
                            [this](const auto &p) { return handleDistanceStart(p); });
    router_.registerCommand(Command::DISTANCE_STOP,
                            [this](const auto &p) { return handleDistanceStop(p); });
    router_.registerCommand(Command::DISTANCE_READ,
                            [this](const auto &p) { return handleDistanceRead(p); });
    router_.registerCommand(Command::DISTANCE_SET_MODE,
                            [this](const auto &p) { return handleDistanceSetMode(p); });
    router_.registerCommand(Command::DISTANCE_CALIBRATE,
                            [this](const auto &p) { return handleDistanceCalibrate(p); });

    // UV sensor
    router_.registerCommand(Command::UV_INIT, [this](const auto &p) { return handleUVInit(p); });
    router_.registerCommand(Command::UV_SHUTDOWN,
                            [this](const auto &p) { return handleUVShutdown(p); });
    router_.registerCommand(Command::UV_READ, [this](const auto &p) { return handleUVRead(p); });
    router_.registerCommand(Command::UV_SET_MODE,
                            [this](const auto &p) { return handleUVSetMode(p); });
    router_.registerCommand(Command::UV_SET_GAIN,
                            [this](const auto &p) { return handleUVSetGain(p); });
    router_.registerCommand(Command::UV_SET_TIME,
                            [this](const auto &p) { return handleUVSetTime(p); });
    // ALS sensor
    router_.registerCommand(Command::ALS_INIT, [this](const auto &p) { return handleALSInit(p); });
    router_.registerCommand(Command::ALS_SHUTDOWN,
                            [this](const auto &p) { return handleALSShutdown(p); });
    router_.registerCommand(Command::ALS_READ, [this](const auto &p) { return handleALSRead(p); });
    router_.registerCommand(Command::ALS_SET_GAIN,
                            [this](const auto &p) { return handleALSSetGain(p); });
    router_.registerCommand(Command::ALS_SET_EXPOSURE,
                            [this](const auto &p) { return handleALSSetExposure(p); });
    // IMU control
     router_.registerCommand(Command::IMU_INIT,
        [this](const auto &p) { return handleImuInit(p); });
    router_.registerCommand(Command::IMU_SHUTDOWN,
        [this](const auto &p) { return handleImuShutdown(p); });
    router_.registerCommand(Command::IMU_CONFIGURE,
        [this](const auto &p) { return handleImuConfigure(p); });
    router_.registerCommand(Command::IMU_START,
        [this](const auto &p) { return handleImuStart(p); });
    router_.registerCommand(Command::IMU_STOP,
        [this](const auto &p) { return handleImuStop(p); });
    router_.registerCommand(Command::IMU_SOFT_RESET,
        [this](const auto &p) { return handleImuSoftReset(p); });
    router_.registerCommand(Command::IMU_READ_REG,
        [this](const auto &p) { return handleImuReadReg(p); });
    router_.registerCommand(Command::IMU_WRITE_REG,
        [this](const auto &p) { return handleImuWriteReg(p); });


                            // LED control
    router_.registerCommand(Command::LED_INIT,
                            [this](const auto &p) { return handleLedInit(p); });
    router_.registerCommand(Command::LED_SHUTDOWN,
                            [this](const auto &p) { return handleLedShutdown(p); });
    router_.registerCommand(Command::LED_TORCH,
                            [this](const auto &p) { return handleLedTorch(p); });
    router_.registerCommand(Command::LED_FLASH,
                            [this](const auto &p) { return handleLedFlash(p); });
    router_.registerCommand(Command::LED_OFF,
                            [this](const auto &p) { return handleLedOff(p); });
    router_.registerCommand(Command::LED_ALL_OFF,
                            [this](const auto &p) { return handleLedAllOff(p); });
    router_.registerCommand(Command::LED_STROBE_SYNC_ENABLE,
                             [this](const auto &p) { return handleLedStrobeSyncEnable(p); });
    router_.registerCommand(Command::LED_GPIO_FLASH,
                             [this](const auto &p) { return handleLedGpioFlash(p); });
    router_.registerCommand(Command::LED_SET_FLASH_DURATION,
                            [this](const auto &p) { return handleLedSetFlashDuration(p); });
    router_.registerCommand(Command::LED_SET_FLASH_TIMEOUT,
                            [this](const auto &p) { return handleLedSetFlashTimeout(p); });
    router_.registerCommand(Command::LED_SET_GPIO_MODE,
                            [this](const auto &p) { return handleLedSetGpioMode(p); });
    router_.registerCommand(Command::LED_SELECT,
                            [this](const auto& p) { return handleLedSelect(p); });
    router_.registerCommand(Command::LED_DESELECT,
                            [this](const auto& p) { return handleLedDeselect(p); });

    router_.registerCommand(Command::LED_GET_STATUS,
                            [this](const auto& p) { return handleLedGetStatus(p); });


    router_.registerCommand(Command::INTERVAL_STILL_START,
                [this](const auto& p) { return handleIntervalStillStart(p); });
    router_.registerCommand(Command::INTERVAL_STILL_STOP,
                [this]([[maybe_unused]] const auto& p) { return handleIntervalStillStop(); });

    // Sensor timing
    router_.registerCommand(Command::GET_SENSOR_TIMING,
                            [this](const auto &p) { return handleGetSensorTiming(p); });
    router_.registerCommand(Command::GET_SENSOR_TEMPERATURE,
                            [this](const auto& p) { return handleGetSensorTemperature(p); });
                            // Status
    router_.registerCommand(Command::GET_STATUS,
                            [this](const auto &p) { return handleGetStatus(p); });
    router_.registerCommand(protocol::DiagCommand::RAW_CAPTURE,
        [this](const auto& p) { return handleDiagRawCapture(p); });
    router_.registerCommand(protocol::DiagCommand::RAW_ROI_CAPTURE,
        [this](const auto& p) { return handleDiagRawRoiCapture(p); });
        
    router_.registerCommand(Command::UVBF_CAPTURE,
        [this](const auto& p) { return handleUVBFCapture(p); });
    router_.registerCommand(Command::UVBF_VBLANK_CAPTURE,
        [this](const auto& p) { return handleUVBFVBlankCapture(p); });

    LOG_INFO << "Registered " << router_.getRegisteredCommands().size() << " commands" << std::endl;
}

std::string CommandHandler::handleCommand(const std::string &jsonCommand)
{
    return router_.route(jsonCommand);
}

std::string CommandHandler::handleGetSensorTiming(const std::map<std::string, std::string> &params)
{
    LOG_INFO << "Handling get_sensor_timing command" << std::endl;

    ParamExtractor p(params);
    std::string camera = p.getString(Param::CAMERA, Camera::IMX708);

    // Check if width/height provided for calculated timing
    bool hasResolution = p.hasKey(Param::WIDTH) && p.hasKey(Param::HEIGHT);

    if (hasResolution)
    {
        // Calculate timing for requested resolution (no camera interaction needed)
        int width = p.getInt(Param::WIDTH, 0);
        int height = p.getInt(Param::HEIGHT, 0);

        auto timing = SensorTimingCalculator::calculate(camera, width, height);

        if (!timing.valid)
        {
            return buildJsonError("Could not calculate timing for " + camera + " at " +
                                  std::to_string(width) + "x" + std::to_string(height));
        }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << R"({"type":"sensor_timing")"
            << R"(,"camera":")" << camera << R"(")"
            << R"(,"calculated":true)"
            << R"(,"width":)" << width << R"(,"height":)" << height << R"(,"hblank":)"
            << timing.hblank << R"(,"vblank":)" << timing.vblank << R"(,"pixel_rate":)"
            << timing.pixelRate << R"(,"line_time_us":)" << timing.lineTime_us
            << R"(,"frame_time_us":)" << timing.frameTime_us << R"(,"rolling_shutter_us":)"
            << timing.rollingShutter_us << R"(,"max_frame_rate":)" << timing.maxFrameRate
            << R"(,"active_width":)" << timing.activeWidth << R"(,"active_height":)"
            << timing.activeHeight << R"(,"valid":true)"
            << "}";
        return oss.str();
    }

    // No resolution specified - get current timing from camera (original behavior)
    CameraBase *targetCamera = nullptr;

    if (camera == Camera::IMX708)
        targetCamera = imx708Camera;
    else if (camera == Camera::IMX219)
        targetCamera = imx219Camera;
    else
        return buildJsonError("Unknown camera: " + camera);

    if (!targetCamera)
        return buildJsonError("Camera not available: " + camera);

    auto timing = targetCamera->getSensorTiming();
    if (!timing)
        return buildJsonError("Could not get sensor timing for " + camera);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << R"({"type":"sensor_timing")"
        << R"(,"camera":")" << camera << R"(")"
        << R"(,"calculated":false)"
        << R"(,"hblank":)" << timing->hblank << R"(,"hblank_min":)" << timing->hblankMin
        << R"(,"hblank_max":)" << timing->hblankMax << R"(,"vblank":)" << timing->vblank
        << R"(,"vblank_min":)" << timing->vblankMin << R"(,"vblank_max":)" << timing->vblankMax
        << R"(,"pixel_rate":)" << timing->pixelRate << R"(,"line_time_us":)" << timing->lineTime_us
        << R"(,"frame_time_us":)" << timing->frameTime_us << R"(,"rolling_shutter_us":)"
        << timing->rollingShutter_us << R"(,"active_width":)" << timing->activeWidth
        << R"(,"active_height":)" << timing->activeHeight << R"(,"valid":)"
        << (timing->valid ? "true" : "false") << "}";
    return oss.str();
}

std::string CommandHandler::handleGetSensorTemperature(
    const std::map<std::string, std::string>& params)
{
    ParamExtractor p(params);
    std::string camera = p.getString(Param::CAMERA, Camera::IMX708);

    if (camera != Camera::IMX708)
    {
        return R"({"type":"error","error":"temperature_not_supported","message":"Temperature reading not supported for )"
               + camera + R"("})";
    }

    if (!tempMonitor)
    {
        return R"({"type":"error","error":"temperature_not_supported",)"
               R"("message":"Temperature monitor not available"})";
    }

    auto reading = tempMonitor->getLatest();

    if (!reading)
    {
        return R"({"type":"error","error":"no_reliable_reading",)"
               R"("message":"No reliable temperature reading available yet"})";
    }

    std::ostringstream oss;
    oss << R"({"type":"sensor_temperature")"
        << R"(,"camera":")"     << camera << R"(")"
        << R"(,"celsius":)"     << static_cast<int>(reading->celsius)
        << R"(,"reliable":)"    << (reading->reliable ? "true" : "false")
        << R"(,"age_seconds":)" << reading->age_s
        << "}";
    return oss.str();
}

std::string CommandHandler::handleSetLensPosition(
    const std::map<std::string, std::string>& params)
{
    ParamExtractor p(params);
    std::string camera = p.getString(Param::CAMERA, Camera::IMX708);
    float position     = p.getFloat(Param::LENS_POSITION, 0.0f);

    // Clamp to safe ranges per sensor
    if (camera == Camera::IMX708)
        position = std::clamp(position, 0.0f, 15.0f);
    else if (camera == Camera::IMX219)
        position = std::clamp(position, 0.0f, 32.0f);
    else
        return buildJsonError("Lens position not supported for camera: " + camera);

    if (!streaming)
        return buildJsonError("Cannot set lens position: no active stream");

    // Only applies to the currently active stream camera
    CameraBase* targetCamera = nullptr;
    if (camera == Camera::IMX708 && streamModality == Camera::IMX708)
        targetCamera = imx708Camera;
    else if (camera == Camera::IMX219 && streamModality == Camera::IMX219)
        targetCamera = imx219Camera;

    if (!targetCamera)
        return buildJsonError("Camera not streaming: " + camera);

    // Build a settings update that forces manual AF + new lens position.
    // We retrieve the current streaming settings so everything else is
    // preserved, then override only the focus fields.
    CaptureSettings s = (camera == Camera::IMX708)
        ? multiParams->getRgbStreamingSettings()
        : multiParams->getArducamStreamingSettings();

    s.width        = streamWidth;
    s.height       = streamHeight;
    s.autoFocus    = false;          // force manual — applyFocusControls() sets AfMode::Manual
    s.lensPosition = position;

    targetCamera->setPendingStreamSettings(s);

    LOG_INFO << "Set lens position: camera=" << camera
             << " position=" << position << std::endl;

    std::ostringstream oss;
    oss << R"({"type":"status","message":"Lens position set")"
        << R"(,"camera":")"         << camera   << R"(")"
        << R"(,"lens_position":)"   << position
        << "}";
    return oss.str();
}

 

// Returns a snapshot of currently armed LEDs as LedState vector.
// Called immediately after prepareLedForCapture() so state reflects
// what was active at shutter time.
std::vector<sanuwave::LedState> CommandHandler::getCurrentLedState() const
{
    std::vector<sanuwave::LedState> states;
    std::lock_guard<std::mutex> lk(ledCaptureMutex);
    for (const auto& sel : selectedLeds)
    {
        sanuwave::LedState s;
        s.led_id   = "led_" + std::to_string(sel.ledId);
        s.active   = true;
        // Convert LM3643 brightness register (0-255) to approximate mA.
        // LM3643 torch max is 187.5 mA at register 0x7F (full scale = 255 maps
        // to ~375 mA flash; torch headroom is half that).
        // Use 187.5 / 127.0 as the torch mA-per-count ratio.
        s.drive_ma = sel.brightness * (187.5f / 127.0f);
        states.push_back(s);
    }
    return states;
}


std::string CommandHandler::handleLedGpioFlash(
    const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);

    const uint8_t  brightness  = static_cast<uint8_t>(
                                     std::clamp(p.getInt("brightness", 128), 0, 255));
    const int64_t  duration_us = static_cast<int64_t>(
                                     std::max(p.getInt("duration_us", 200000), 1));
    constexpr uint16_t FLASH_TIMEOUT_MS = 400;

    if (!ledManager || !ledManager->isInitialized())
        return buildJsonError("led_gpio_flash: LED manager not initialized");
    if (!ledGpio)
        return buildJsonError("led_gpio_flash: LED GPIO not available");

    LOG_INFO << "led_gpio_flash: brightness=" << static_cast<int>(brightness)
             << " duration=" << duration_us << " us" << std::endl;

    // Turn off all enabled LEDs first
    for (int i = 0; i < 32; ++i)
        if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
            ledManager->turnOff(static_cast<LedMgr::LedId>(i));

    // Arm each selected LED for hardware level-mode strobe
    for (const auto& sel : selectedLeds)
    {
        auto id = static_cast<LedMgr::LedId>(sel.ledId);
        if (!ledManager->setFlashTorchBrightness(id, sel.brightness))
        {
            LOG_ERROR << "led_gpio_flash: setFlashTorchBrightness failed LED=" << sel.ledId << std::endl;
            return buildJsonError("led_gpio_flash: setFlashTorchBrightness failed: " + ledManager->getLastError());
        }
        if (!ledManager->setFlashTimeout(id, FLASH_TIMEOUT_MS))
        {
            LOG_ERROR << "led_gpio_flash: setFlashTimeout failed LED=" << sel.ledId << std::endl;
            return buildJsonError("led_gpio_flash: setFlashTimeout failed: " + ledManager->getLastError());
        }
        if (!ledManager->setStrobeEnable(id, LedMgr::LedHwStrobeMode::STROBE_MODE_LEVEL))
        {
            LOG_ERROR << "led_gpio_flash: setStrobeEnable failed LED=" << sel.ledId << std::endl;
            return buildJsonError("led_gpio_flash: setStrobeEnable failed: " + ledManager->getLastError());
        }
    }

    // Fire — enableHwStrobeLevelMode pulses each GPIO line for duration_us
    const uint32_t duration_us32 = static_cast<uint32_t>(std::min(duration_us, (int64_t)UINT32_MAX));
    LOG_INFO << "led_gpio_flash: sleeping " << duration_us << " us" << std::endl;
    ledGpio->strobeOn(LedGpioController::Group::ALL);
    usleep(duration_us32);
    ledGpio->strobeOff(LedGpioController::Group::ALL);  
    LOG_INFO << "led_gpio_flash: sleep complete" << std::endl;

    // Disarm — turn off all enabled LEDs
    for (int i = 0; i < 32; ++i)
        if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
            ledManager->turnOff(static_cast<LedMgr::LedId>(i));

    LOG_INFO << "led_gpio_flash: complete" << std::endl;

    return R"({"type":"status","message":"led_gpio_flash_complete"})";
}

std::string CommandHandler::handleCaptureThermal(const std::map<std::string, std::string> &params)
{
    if (intervalShooting)
        return buildJsonError("Cannot capture while interval still is active");

    LOG_INFO << "=== Capturing thermal image ===" << std::endl;

    if (!thermalCamera || !thermalCamera->isReady())
        return buildJsonError("Thermal camera not available");

    // Thermal has no LED association by design, but we snapshot anyway
    // so the sidecar is consistent with RGB frames in a multi-capture session.
    ParamExtractor p(params);
std::string sessionId = p.getString(Param::SESSION_ID, "");
    try
    {
        multiParams->applyThermalSettings(thermalCamera, ParamMode::CAPTURE);

        auto ledSnapshot = getCurrentLedState(); // typically empty for thermal
        cv::Mat thermalImage = thermalCamera->captureThermalVisualization();

        if (thermalImage.empty())
            return buildJsonError("Failed to capture thermal image");

        lastImageData = jpegEncoder->encode(thermalImage, 95);
        lastImageModality = Camera::THERMAL;

        auto sf = buildSessionFields(sessionId);

        LOG_INFO << "Thermal captured (" << lastImageData.size() << " bytes)"
                 << " session=" << sf.session_id << std::endl;

        std::ostringstream oss;
        oss << R"({"type":"capture_complete","modality":"thermal","status":"ready_to_send")"
            << R"(,"session_id":")"         << sf.session_id    << R"(")"
            << R"(,"frame_index":0)"
            << R"(,"camera_id":"lepton")"
            << R"(,"capture_timestamp_us":)" << sf.timestamp_us
            << R"(,"width":)"               << thermalImage.cols
            << R"(,"height":)"              << thermalImage.rows
            << R"(,"leds":)"                << ledStateToJson(ledSnapshot)
            << "}";
        return oss.str();
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Exception: ") + e.what());
    }
}

// ============================================================
// REPLACE handleCaptureIMX708()
// ============================================================
std::string CommandHandler::handleCaptureIMX708(const std::map<std::string, std::string> &params)
{
    if (intervalShooting)
        return buildJsonError("Cannot capture while interval still is active");

    ParamExtractor p(params);
    CaptureSettings settings = extractCaptureSettings(p, 4608, 2592);
    int quality = p.getInt(Param::QUALITY, 95);

    LOG_INFO << "=== Capturing IMX708 image ===" << std::endl;
    logCaptureSettings(settings, "IMX708");

    try
    {
        FrameMetadata metaData;
        prepareLedForCapture();
        auto ledSnapshot = getCurrentLedState();
        cv::Mat image = imx708Camera->capture(settings, &metaData);
        finishLedAfterCapture();

        if (image.empty())
            return buildJsonError("Failed to capture RGB image");

        lastImageData = imx708Camera->encodeImageForTransmission(image, true, quality);
        lastImageModality = "RGB";

        auto sf = buildSessionFields(p.getString("session_id", ""));

        LOG_INFO << "RGB captured (" << lastImageData.size() << " bytes)"
                 << " session=" << sf.session_id << std::endl;

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << R"({"type":"capture_complete","modality":"rgb","status":"ready_to_send")"
            << R"(,"session_id":")"         << sf.session_id         << R"(")"
            << R"(,"frame_index":0)"
            << R"(,"camera_id":"imx708")"
            << R"(,"capture_timestamp_us":)" << sf.timestamp_us
            << R"(,"actual_exposure_us":)"   << metaData.exposureTime_us
            << R"(,"actual_gain":)"          << metaData.analogGain
            << R"(,"ae_active":)"            << (settings.autoExposure ? "true" : "false")
            << R"(,"af_active":)"            << (settings.autoFocus    ? "true" : "false")
            << R"(,"lens_position":)"        << settings.lensPosition
            << R"(,"width":)"               << metaData.width
            << R"(,"height":)"              << metaData.height
            << R"(,"leds":)"                << ledStateToJson(ledSnapshot)
            << "}";
        return oss.str();
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Exception: ") + e.what());
    }
}


// ============================================================
// REPLACE handleCaptureIMX219Custom()
// ============================================================
std::string CommandHandler::handleCaptureIMX219Custom(
    const std::map<std::string, std::string> &params)
{
    if (intervalShooting)
        return buildJsonError("Cannot capture while interval still is active");

    LOG_INFO << "IMX219 capture params: ";
    for (const auto& [k, v] : params)
        LOG_INFO << "  " << k << " = " << v;
    LOG_INFO << std::endl;

    ParamExtractor p(params);

    if (!imx219Camera)
        return buildJsonError("Arducam camera not available");

    CaptureSettings settings = extractCaptureSettings(p, 3280, 2464);
    int quality = p.getInt(Param::QUALITY, 95);

    logCaptureSettings(settings, "Arducam/IMX219");

    try
    {
        FrameMetadata metadata;
        prepareLedForCapture();
        auto ledSnapshot = getCurrentLedState();
        cv::Mat image = imx219Camera->capture(settings, &metadata);
        finishLedAfterCapture();

        if (image.empty())
            return buildJsonError("Failed to capture from Arducam");

        lastImageData = jpegEncoder->encode(image, quality);
        lastImageModality = Modality::ARDUCAM_CUSTOM;

        auto sf = buildSessionFields(p.getString(Param::SESSION_ID, ""));

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        oss << R"({"type":"capture_complete","modality":"arducam_custom","status":"ready_to_send")"
            << R"(,"session_id":")"         << sf.session_id         << R"(")"
            << R"(,"frame_index":0)"
            << R"(,"camera_id":"imx219")"
            << R"(,"capture_timestamp_us":)" << sf.timestamp_us
            << R"(,"actual_exposure_us":)"   << metadata.exposureTime_us
            << R"(,"actual_gain":)"          << metadata.analogGain
            << R"(,"ae_active":)"            << (settings.autoExposure ? "true" : "false")
            << R"(,"af_active":)"            << (settings.autoFocus    ? "true" : "false")
            << R"(,"lens_position":)"        << settings.lensPosition
            << R"(,"width":)"               << metadata.width
            << R"(,"height":)"              << metadata.height
            << R"(,"leds":)"                << ledStateToJson(ledSnapshot)
            << "}";
        return oss.str();
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Exception: ") + e.what());
    }
}


std::string CommandHandler::handleCaptureRaw(const std::map<std::string, std::string> &params)
{
    if (intervalShooting)
        return buildJsonError("Cannot capture while interval still is active");
    ParamExtractor p(params);
    std::string camera = p.getString(Param::CAMERA, Camera::IMX708);

    LOG_INFO << "=== Capturing RAW Bayer image ===" << std::endl;

    CameraBase *targetCamera = nullptr;
    std::string bayerPattern;
    int sensorBitDepth = 10;
    int blackLevel = 0;
    int defaultWidth, defaultHeight;

    if (camera == Camera::IMX708)
    {
        targetCamera = imx708Camera;
        bayerPattern = "BGGR";
        blackLevel = 4096;
        defaultWidth = 4608;
        defaultHeight = 2592;
    }
    else if (camera == Camera::IMX219)
    {
        targetCamera = imx219Camera;
        bayerPattern = "BGGR";
        blackLevel = 4096;
        defaultWidth = 3280;
        defaultHeight = 2464;
    }
    else
    {
        return buildJsonError("Unknown camera for raw capture: " + camera);
    }

    if (!targetCamera)
        return buildJsonError("Camera not available: " + camera);

    CaptureSettings settings = extractCaptureSettings(p, defaultWidth, defaultHeight);

    LOG_INFO << "RAW capture extracted settings: " << settings << std::endl;
    LOG_INFO << "  Client sent auto_exposure key: " << p.hasKey(Param::AUTO_EXPOSURE) << std::endl;

    settings.rawMode = true;
    settings.rawBitDepth = p.getInt(Param::RAW_BIT_DEPTH, 10);

    // For raw, default to manual exposure if not specified
    if (!p.hasKey({Param::AUTO_EXPOSURE}) && !p.hasKey(Param::EXPOSURE_TIME_US))
    {
        settings.autoExposure = false;
        if (settings.exposureTime_us == 0)
            settings.exposureTime_us = 10000;
    }
    if (!p.hasKey(Param::AUTO_ANALOG_GAIN))
    {
        settings.autoAnalogGain = false;
        if (settings.analogGain <= 1.0f)
            settings.analogGain = 4.0f;
    }

    if (camera != Camera::IMX708)
        settings.autoFocus = false;

    logCaptureSettings(settings, camera + " RAW");

    try
    {
        FrameMetadata metaData;
        prepareLedForCapture();
        cv::Mat image = targetCamera->capture(settings, &metaData);
        finishLedAfterCapture();
        if (image.empty())
            return buildJsonError("Failed to capture raw image from " + camera);

        int storageBits = (image.depth() == CV_16U) ? 16 : 8;

        std::string header = "RAW|" + std::to_string(image.cols) + "|" +
                             std::to_string(image.rows) + "|" + std::to_string(sensorBitDepth) +
                             "|" + std::to_string(storageBits) + "|" + std::to_string(blackLevel) +
                             "|" + bayerPattern + "|";

        size_t dataSize = image.total() * image.elemSize();
        lastImageData.clear();
        lastImageData.reserve(header.size() + dataSize);
        lastImageData.insert(lastImageData.end(), header.begin(), header.end());
        lastImageData.insert(lastImageData.end(), image.data, image.data + dataSize);
        lastImageModality = StreamFormat::RAW;

        LOG_INFO << "RAW captured: " << image.cols << "x" << image.rows << " " << sensorBitDepth
                 << "-bit in " << storageBits << "-bit container"
                 << " " << bayerPattern << " (black_level=" << blackLevel << ")"
                 << " (" << lastImageData.size() << " bytes)" << std::endl;

        std::ostringstream oss;
        oss << R"({"type":"capture_complete","modality":"raw")"
            << R"(,"camera":")" << camera << R"(")"
            << R"(,"width":)" << image.cols << R"(,"height":)" << image.rows << R"(,"bit_depth":)"
            << sensorBitDepth << R"(,"storage_bits":)" << storageBits << R"(,"black_level":)"
            << blackLevel << R"(,"bayer_pattern":")" << bayerPattern << R"(")"
            << R"(,"exposure_us":)" << metaData.exposureTime_us << R"(,"analog_gain":)"
            << metaData.analogGain << R"(,"data_size":)" << lastImageData.size()
            << R"(,"status":"ready_to_send"})";
        return oss.str();
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Raw capture exception: ") + e.what());
    }
}


std::string CommandHandler::handleUVBFCapture(
    const std::map<std::string, std::string>& params)
{
    if (uvbfActive)
        return buildJsonError("UVBF capture already in progress");
    if (streaming || dualStreaming)
        return buildJsonError("Cannot start UVBF capture while streaming");
    if (intervalShooting)
        return buildJsonError("Cannot start UVBF capture while interval still is active");

    ParamExtractor p(params);

    std::string camera     = p.getString(sanuwave::protocol::UVBFParam::CAMERA,      Camera::IMX219);
    float       analogGain = p.getFloat (sanuwave::protocol::UVBFParam::ANALOG_GAIN, 1.0f);
    std::string sessionId  = p.getString(sanuwave::protocol::Param::SESSION_ID,      "");

    CameraBase* targetCamera = nullptr;
    if      (camera == Camera::IMX219) targetCamera = imx219Camera;
    else if (camera == Camera::IMX708) targetCamera = imx708Camera;
    else
        return buildJsonError("UVBF: unknown camera: " + camera);

    if (!targetCamera)
        return buildJsonError("UVBF: camera not available: " + camera);
    if (!ledManager || !ledManager->isInitialized())
        return buildJsonError("UVBF: LED manager not initialized");
    if (!ledGpio)
        return buildJsonError("UVBF: LED GPIO not available");
    if (selectedLeds.empty())
        return buildJsonError("UVBF: no LEDs selected — send LED_SELECT before UVBF_CAPTURE");
    if (analogGain < 1.0f)
        return buildJsonError("UVBF: analog_gain must be >= 1.0");

    auto timingOpt = targetCamera->getSensorTiming();
    if (!timingOpt || !timingOpt->valid || timingOpt->rollingShutter_us <= 0.0)
        return buildJsonError("UVBF: no valid sensor timing — cannot derive flash timeout");

    const int      rollingShutter_ms = static_cast<int>(timingOpt->rollingShutter_us / 1000.0 + 0.5);
    const uint16_t flashTimeout_ms   = static_cast<uint16_t>(rollingShutter_ms * 2 + 20);

    LOG_INFO << "UVBF: rolling shutter=" << timingOpt->rollingShutter_us
             << "us, flash timeout=" << flashTimeout_ms << "ms" << std::endl;

    std::vector<LedSelection> ledSnapshot;
    {
        std::lock_guard<std::mutex> lk(ledCaptureMutex);
        ledSnapshot = selectedLeds;
    }

    const uint16_t timeout_ms = flashTimeout_ms;

    uvbfActive = true;

    if (uvbfWorker.joinable())
        uvbfWorker.join();

    uvbfWorker = std::thread([this, targetCamera, camera,
                               analogGain, timeout_ms, sessionId, ledSnapshot]()
    {
        const std::string bayerPattern = "BGGR";
        const int         blackLevel   = 4096;
        const int         sensorBits   = 10;

        uint64_t ledOnTimestamp1_ms  = 0;
        uint64_t ledOffTimestamp1_ms = 0;
        uint64_t ledOnTimestamp2_ms  = 0;
        uint64_t ledOffTimestamp2_ms = 0;
        uint64_t ledOnTimestamp3_ms  = 0;
        uint64_t ledOffTimestamp3_ms = 0;

        ledGpio->strobeOff(LedGpioController::Group::ALL);
        LOG_INFO << "UVBF: strobe GPIO deasserted before arm" << std::endl;

        LOG_INFO << "UVBF: arming LEDs in hardware strobe mode" << std::endl;

        for (int i = 0; i < 32; ++i)
            if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
                ledManager->turnOff(static_cast<LedMgr::LedId>(i));

        bool armOk = true;
        for (const auto& sel : ledSnapshot)
        {
            if (!ledManager->isLedEnabled(static_cast<LedMgr::LedId>(sel.ledId)))
                continue;
            auto id = static_cast<LedMgr::LedId>(sel.ledId);
            if (!ledManager->setFlashTorchBrightness(id, sel.brightness))
            {
                LOG_ERROR << "UVBF: setFlashTorchBrightness failed LED=" << sel.ledId << std::endl;
                armOk = false;
            }
            if (!ledManager->setFlashTimeout(id, timeout_ms))
            {
                LOG_ERROR << "UVBF: setFlashTimeout failed LED=" << sel.ledId << std::endl;
                armOk = false;
            }
            if (!ledManager->setStrobeEnable(id, LedMgr::LedHwStrobeMode::STROBE_MODE_LEVEL))
            {
                LOG_ERROR << "UVBF: setStrobeEnable failed LED=" << sel.ledId << std::endl;
                armOk = false;
            }
        }

        if (!armOk)
        {
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"led_arm","reason":"LED arm failed — check log"})");
            uvbfActive = false;
            return;
        }

        LOG_INFO << "UVBF: LEDs armed, GPIO strobe pin will trigger illumination" << std::endl;

        auto disarmLeds = [&]()
        {
            LOG_INFO << "UVBF: disarming LEDs" << std::endl;
            ledGpio->strobeOff(LedGpioController::Group::ALL);
            for (int i = 0; i < 32; ++i)
                if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
                    ledManager->turnOff(static_cast<LedMgr::LedId>(i));
        };

        auto packageRaw = [&](const StrobeCaptureResult& r) -> std::vector<uint8_t>
        {
            int storageBits = (r.image.depth() == CV_16U) ? 16 : 8;
            std::string header = "RAW|"
                + std::to_string(r.image.cols)  + "|"
                + std::to_string(r.image.rows)  + "|"
                + std::to_string(sensorBits)    + "|"
                + std::to_string(storageBits)   + "|"
                + std::to_string(blackLevel)    + "|"
                + bayerPattern                  + "|";

            size_t dataSize = r.image.total() * r.image.elemSize();
            std::vector<uint8_t> payload;
            payload.reserve(header.size() + dataSize);
            payload.insert(payload.end(), header.begin(), header.end());
            payload.insert(payload.end(), r.image.data, r.image.data + dataSize);
            return payload;
        };

        auto makeHeader = [&](const std::string& role,
                               size_t payloadSize,
                               const StrobeCaptureResult& r,
                               uint64_t ledOn_ms  = 0,
                               uint64_t ledOff_ms = 0) -> std::string
        {
            std::ostringstream h;
            h << R"({"type":")"  << protocol::ResponseType::FRAME_TRANSFER  << R"(")"
              << R"(,")"  << protocol::Param::FRAME_ROLE    << R"(":")" << role          << R"(")"
              << R"(,")"  << protocol::Param::SESSION_ID    << R"(":")" << sessionId     << R"(")"
              << R"(,")"  << protocol::Param::CAMERA        << R"(":")" << camera        << R"(")"
              << R"(,")"  << protocol::Param::PAYLOAD_SIZE  << R"(":)"  << payloadSize
              << R"(,"bayer_pattern":")"                                  << bayerPattern << R"(")"
              << R"(,"bit_depth":)"                                       << sensorBits
              << R"(,"black_level":)"                                     << blackLevel
              << R"(,"width":)"                                           << r.image.cols
              << R"(,"height":)"                                          << r.image.rows
              << R"(,"exposure_us":)"                                     << r.exposureTime_us
              << R"(,"capture_timestamp_ms":)"                            << r.captureTimestamp_ms
              << R"(,"led_on_timestamp_ms":)"                             << ledOn_ms
              << R"(,"led_off_timestamp_ms":)"                            << ledOff_ms
              << "}";
            return h.str();
        };

        CaptureSettings burstSettings;
        burstSettings.analogGain = analogGain;

        if (!targetCamera->beginStrobeBurst(burstSettings))
        {
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"burst_init","reason":")"
                       + targetCamera->getLastError() + R"("})");
            disarmLeds();
            uvbfActive = false;
            return;
        }

        targetCamera->setStrobeCallback(nullptr);

        uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_STARTED)
                   + R"(","session_id":")" + sessionId + R"("})");

        // ── Frame 1: Dark 1 (LEDs off) ────────────────────────────────────────
        LOG_INFO << "UVBF: capturing dark frame 1" << std::endl;
        auto dark1Result = targetCamera->captureStrobeBurstFramePreQueue(nullptr);
        if (!dark1Result.success)
        {
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"dark1_capture","reason":")" + dark1Result.error + R"("})");
            targetCamera->endStrobeBurst();
            disarmLeds();
            uvbfActive = false;
            return;
        }

        // ── Frame 2: Prime 1 (discard) — strobeOn pipeline lead for illum1 ────
        LOG_INFO << "UVBF: capturing prime frame 1 (discarded)" << std::endl;
        auto prime1PreQueue = [&]()
        {
            ledOnTimestamp1_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            ledGpio->strobeOn(LedGpioController::Group::ALL);
            LOG_INFO << "UVBF: strobeOn prime 1 pre-queue (pipeline lead for illum1)" << std::endl;
        };
        auto prime1Result = targetCamera->captureStrobeBurstFramePreQueue(prime1PreQueue);
        if (!prime1Result.success)
        {
            ledGpio->strobeOff(LedGpioController::Group::ALL);
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"prime1_capture","reason":")" + prime1Result.error + R"("})");
            targetCamera->endStrobeBurst();
            disarmLeds();
            uvbfActive = false;
            return;
        }

        // ── Frame 3: Illuminated 1 ────────────────────────────────────────────
        LOG_INFO << "UVBF: capturing illuminated frame 1" << std::endl;
        auto illum1Result = targetCamera->captureStrobeBurstFramePreQueue(nullptr);

        ledGpio->strobeOff(LedGpioController::Group::ALL);
        ledOffTimestamp1_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        LOG_INFO << "UVBF: strobeOff after illuminated frame 1" << std::endl;

        if (!illum1Result.success)
        {
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"illuminated1_capture","reason":")" + illum1Result.error + R"("})");
            targetCamera->endStrobeBurst();
            disarmLeds();
            uvbfActive = false;
            return;
        }
     

        // ── Frame 4: Prime 2 (discard) — strobeOn pipeline lead for illum2 ────
        LOG_INFO << "UVBF: capturing prime frame 2 (discarded)" << std::endl;
        auto prime2PreQueue = [&]()
        {
            ledOnTimestamp2_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            ledGpio->strobeOn(LedGpioController::Group::ALL);
            LOG_INFO << "UVBF: strobeOn prime 2 pre-queue (pipeline lead for illum2)" << std::endl;
        };
        auto prime2Result = targetCamera->captureStrobeBurstFramePreQueue(prime2PreQueue);
        if (!prime2Result.success)
        {
            ledGpio->strobeOff(LedGpioController::Group::ALL);
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"prime2_capture","reason":")" + prime2Result.error + R"("})");
            targetCamera->endStrobeBurst();
            disarmLeds();
            uvbfActive = false;
            return;
        }

        // ── Frame 5: Illuminated 2 ────────────────────────────────────────────
        LOG_INFO << "UVBF: capturing illuminated frame 2" << std::endl;
        auto illum2Result = targetCamera->captureStrobeBurstFramePreQueue(nullptr);

        ledGpio->strobeOff(LedGpioController::Group::ALL);
        ledOffTimestamp2_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        LOG_INFO << "UVBF: strobeOff after illuminated frame 2" << std::endl;

        if (!illum2Result.success)
        {
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"illuminated2_capture","reason":")" + illum2Result.error + R"("})");
            targetCamera->endStrobeBurst();
            disarmLeds();
            uvbfActive = false;
            return;
        }

        // ── Frame 6: Prime 3 (discard) — strobeOn pipeline lead for illum3 ────
        LOG_INFO << "UVBF: capturing prime frame 3 (discarded)" << std::endl;
        auto prime3PreQueue = [&]()
        {
            ledOnTimestamp3_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            ledGpio->strobeOn(LedGpioController::Group::ALL);
            LOG_INFO << "UVBF: strobeOn prime 3 pre-queue (pipeline lead for illum3)" << std::endl;
        };
        auto prime3Result = targetCamera->captureStrobeBurstFramePreQueue(prime3PreQueue);
        if (!prime3Result.success)
        {
            ledGpio->strobeOff(LedGpioController::Group::ALL);
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"prime3_capture","reason":")" + prime3Result.error + R"("})");
            targetCamera->endStrobeBurst();
            disarmLeds();
            uvbfActive = false;
            return;
        }

        // ── Frame 7: Illuminated 3 ────────────────────────────────────────────
        LOG_INFO << "UVBF: capturing illuminated frame 3" << std::endl;
        auto illum3Result = targetCamera->captureStrobeBurstFramePreQueue(nullptr);

        ledGpio->strobeOff(LedGpioController::Group::ALL);
        ledOffTimestamp3_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        LOG_INFO << "UVBF: strobeOff after illuminated frame 3" << std::endl;

        if (!illum3Result.success)
        {
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"illuminated3_capture","reason":")" + illum3Result.error + R"("})");
            targetCamera->endStrobeBurst();
            disarmLeds();
            uvbfActive = false;
            return;
        }

        // ── Frame 8: Dark 2 (LEDs off) ────────────────────────────────────────
        LOG_INFO << "UVBF: capturing dark frame 2" << std::endl;
        auto dark2Result = targetCamera->captureStrobeBurstFramePreQueue(nullptr);
        if (!dark2Result.success)
        {
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"dark2_capture","reason":")" + dark2Result.error + R"("})");
            targetCamera->endStrobeBurst();
            disarmLeds();
            uvbfActive = false;
            return;
        }
        // ── End burst, disarm ─────────────────────────────────────────────────
        targetCamera->endStrobeBurst();
        disarmLeds();
        uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_FRAME_CAPTURED)
                   + R"(",")" + protocol::Param::FRAME_ROLE + R"(":")"
                   + protocol::FrameRole::DARK_1 + R"("})");

        uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_FRAME_CAPTURED)
                   + R"(",")" + protocol::Param::FRAME_ROLE + R"(":")"
                   + protocol::FrameRole::ILLUMINATED_1 + R"("})");

        uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_FRAME_CAPTURED)
                   + R"(",")" + protocol::Param::FRAME_ROLE + R"(":")"
                   + protocol::FrameRole::ILLUMINATED_2 + R"("})");

        uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_FRAME_CAPTURED)
                   + R"(",")" + protocol::Param::FRAME_ROLE + R"(":")"
                   + protocol::FrameRole::ILLUMINATED_3 + R"("})");

        uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_FRAME_CAPTURED)
                   + R"(",")" + protocol::Param::FRAME_ROLE + R"(":")"
                   + protocol::FrameRole::DARK_2 + R"("})");


        // ── Package and transfer (prime frames not included) ──────────────────
        auto dark1Payload  = packageRaw(dark1Result);
        auto illum1Payload = packageRaw(illum1Result);
        auto illum2Payload = packageRaw(illum2Result);
        auto illum3Payload = packageRaw(illum3Result);
        auto dark2Payload  = packageRaw(dark2Result);

        LOG_INFO << "UVBF: transferring "
                 << dark1Payload.size()  << " + "
                 << illum1Payload.size() << " + "
                 << illum2Payload.size() << " + "
                 << illum3Payload.size() << " + "
                 << dark2Payload.size()  << " bytes" << std::endl;

        if (sendDngCallback)
        {
            sendDngCallback(makeHeader(protocol::FrameRole::DARK_1,
                                       dark1Payload.size(), dark1Result),
                            dark1Payload.data(), dark1Payload.size());

            sendDngCallback(makeHeader(protocol::FrameRole::ILLUMINATED_1,
                                       illum1Payload.size(), illum1Result,
                                       ledOnTimestamp1_ms, ledOffTimestamp1_ms),
                            illum1Payload.data(), illum1Payload.size());

            sendDngCallback(makeHeader(protocol::FrameRole::ILLUMINATED_2,
                                       illum2Payload.size(), illum2Result,
                                       ledOnTimestamp2_ms, ledOffTimestamp2_ms),
                            illum2Payload.data(), illum2Payload.size());

            sendDngCallback(makeHeader(protocol::FrameRole::ILLUMINATED_3,
                                       illum3Payload.size(), illum3Result,
                                       ledOnTimestamp3_ms, ledOffTimestamp3_ms),
                            illum3Payload.data(), illum3Payload.size());

            sendDngCallback(makeHeader(protocol::FrameRole::DARK_2,
                                       dark2Payload.size(), dark2Result),
                            dark2Payload.data(), dark2Payload.size());
        }

        uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_COMPLETE)
                   + R"(","session_id":")" + sessionId + R"("})");

        LOG_INFO << "UVBF: sequence complete" << std::endl;
        uvbfActive = false;
    });

    return R"({"type":"status","message":"UVBF capture started","session_id":")"
           + sessionId + R"("})";
}


// ============================================================================
// uvbfArmLeds
// Arms only the selected LEDs via I2C torch mode.
// No GPIO lines are used — UVBF uses software timing only.
// ============================================================================
bool CommandHandler::uvbfArmLeds(const std::vector<int>& ledIds, uint8_t brightness)
{
    for (int id : ledIds)
    {
        auto ledId = static_cast<LedMgr::LedId>(id);

        if (!ledManager->setFlashTorchBrightness(ledId, brightness))
        {
            LOG_ERROR << "UVBF: failed to set brightness for LED " << id << std::endl;
            return false;
        }
        if (!ledManager->setFlashMode(ledId))
        {
            LOG_ERROR << "UVBF: failed to set torch mode for LED " << id << std::endl;
            return false;
        }
    }
    LOG_INFO << "UVBF: armed " << ledIds.size() << " LEDs at brightness "
             << static_cast<int>(brightness) << std::endl;
    return true;
}

// ============================================================================
// uvbfDisarmLeds
// Unconditional best-effort — called on both success and error paths.
// ============================================================================
void CommandHandler::uvbfDisarmLeds(const std::vector<int>& ledIds)
{
    if (!ledManager || !ledManager->isInitialized())
        return;

    for (int id : ledIds)
        ledManager->turnOff(static_cast<LedMgr::LedId>(id));

    LOG_INFO << "UVBF: disarmed " << ledIds.size() << " LEDs" << std::endl;
}

// ============================================================================
// uvbfCaptureDng
// Captures one RAW frame and packages it with the RAW| header so the
// client can parse and export it as DNG using the existing path.
// Returns empty vector on failure, populates errorOut.
// ============================================================================
std::vector<uint8_t> CommandHandler::uvbfCaptureDng(CameraBase*            targetCamera,
                                                      const CaptureSettings& settings,
                                                      std::string&           errorOut)
{
    try
    {
        FrameMetadata meta;
        cv::Mat image = targetCamera->capture(settings, &meta);

        if (image.empty())
        {
            errorOut = "capture() returned empty frame";
            return {};
        }

        // Build the same RAW| framed payload that handleCaptureRaw produces
        // so the client's existing DngExporter::buildFromCapture() works
        // without modification.
        const std::string bayerPattern = "BGGR";
        const int sensorBitDepth       = 10;
        const int blackLevel           = 4096;
        int storageBits = (image.depth() == CV_16U) ? 16 : 8;

        std::string header = "RAW|"
            + std::to_string(image.cols)    + "|"
            + std::to_string(image.rows)    + "|"
            + std::to_string(sensorBitDepth)+ "|"
            + std::to_string(storageBits)   + "|"
            + std::to_string(blackLevel)    + "|"
            + bayerPattern                  + "|";

        size_t dataSize = image.total() * image.elemSize();
        std::vector<uint8_t> payload;
        payload.reserve(header.size() + dataSize);
        payload.insert(payload.end(), header.begin(), header.end());
        payload.insert(payload.end(), image.data, image.data + dataSize);

        LOG_INFO << "UVBF: captured " << image.cols << "x" << image.rows
                 << " RAW frame (" << payload.size() << " bytes)" << std::endl;

        return payload;
    }
    catch (const std::exception& e)
    {
        errorOut = std::string("exception: ") + e.what();
        return {};
    }
}

// ============================================================================
// uvbfNotify
// ============================================================================
void CommandHandler::uvbfNotify(const std::string& json)
{
    LOG_INFO << "UVBF notify: " << json << std::endl;
    if (notifyCallback)
        notifyCallback(json);
}

std::string CommandHandler::handleStreamStart(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    std::string camera = p.getString(Param::CAMERA, Camera::IMX708);
    int width = p.getInt(Param::WIDTH, 1920);
    int height = p.getInt(Param::HEIGHT, 1080);
    int quality = p.getInt(Param::QUALITY, 80);

    LOG_INFO << "=== Starting " << camera << " stream ===" << std::endl;
    LOG_INFO << "  Resolution: " << width << "x" << height << ", quality: " << quality << std::endl;

    if (streaming)
        return buildJsonError("Stream already active. Stop current stream first.");

    // Parse optional motion-measurement parameters. Staged into
    // pendingMotion_, which the rgb start methods copy into their
    // StreamContext. Thermal start methods ignore this (motion is
    // rgb-only). Defaults: disabled, ROI 512 px, reference "previous".
    {
        namespace P = sanuwave::protocol;
        pendingMotion_ = StreamContext::MotionConfig{};   // reset to defaults

        pendingMotion_.enabled  = p.getBool(Param::MOTION_ENABLED,  false);
        int roi                 = p.getInt (Param::MOTION_ROI_SIZE, 512);
        if (roi < 64 || roi > 2048)
        {
            LOG_WARNING << "motion: ROI " << roi
                        << " out of range [64,2048], using 512" << std::endl;
            roi = 512;
        }
        pendingMotion_.roi_size = roi;

        std::string ref = p.getString(Param::MOTION_REFERENCE,
                                      P::MotionReference::PREVIOUS);
        if (ref != P::MotionReference::PREVIOUS &&
            ref != P::MotionReference::ANCHOR)
        {
            LOG_WARNING << "motion: unknown reference '" << ref
                        << "', using '" << P::MotionReference::PREVIOUS
                        << "'" << std::endl;
            ref = P::MotionReference::PREVIOUS;
        }
        pendingMotion_.reference = ref;

        if (pendingMotion_.enabled)
        {
            LOG_INFO << "  motion: enabled roi=" << pendingMotion_.roi_size
                     << " ref=" << pendingMotion_.reference << std::endl;
        }
    }

    bool success = false;
    if (camera == Camera::IMX708)
        success = startIMX708Stream(width, height, quality);
    else if (camera == Camera::IMX219)
        success = startIMX219Stream(width, height, quality);
    else if (camera == Camera::THERMAL)
        success = startThermalStream(width, height, quality);
    else if (camera == Camera::DUAL)
        success = startDualStream(width, height, quality);
    else
        return buildJsonError("Unknown stream camera: " + camera);

    if (success)
    {
        std::ostringstream oss;
        oss << R"({"type":"status","message":")" << sanuwave::protocol::StatusMessage::STREAM_STARTED << R"(")"
            << R"(,"camera":")" << camera << R"(")"
            << R"(,"width":)"   << width
            << R"(,"height":)"  << height
            << R"(,"quality":)" << quality
            << R"(,"mode":"max_throughput")";

        // Embed frame duration limits so the client has them at stream-start time.
        // Query from whichever camera just started streaming.
        CameraBase *streamCam = nullptr;
        if (camera == Camera::IMX708)       streamCam = imx708Camera;
        else if (camera == Camera::IMX219)  streamCam = imx219Camera;
        else if (camera == Camera::DUAL)    streamCam = imx708Camera;

        if (streamCam)
        {
            auto limits = streamCam->getFrameDurationLimits();
            if (limits.valid)
            {
                oss << R"(,")" << sanuwave::protocol::FrameDurationField::MIN_US << R"(":)" << limits.minUs
                    << R"(,")" << sanuwave::protocol::FrameDurationField::MAX_US << R"(":)" << limits.maxUs;
            }
        }

        oss << "}";
        return oss.str();
    }
    return buildJsonError("Failed to start " + camera + " stream");
}

std::string CommandHandler::handleStreamStop(const std::map<std::string, std::string> &)
{
    LOG_INFO << "=== Stopping stream ===" << std::endl;

    if (!streaming && !dualStreaming)
        return buildJsonSuccess("No active stream to stop");

    streaming = false;
    dualStreaming = false;

    if (streamStopCallback)
        streamStopCallback();

    if (streamModality == Camera::IMX708 && imx708Camera)
        imx708Camera->stopStreaming();
    else if (streamModality == Camera::IMX219 && imx219Camera)
        imx219Camera->stopStreaming();
    else if (streamModality == Camera::DUAL && imx708Camera)
    {
        imx708Camera->stopStreaming();
    }

    if (strobeController)
        strobeController->setEnabled(false);

    if (streamWorker.joinable())
    {
        streamWorker.join();
        LOG_DEBUG << "Stream worker thread joined" << std::endl;
    }
    if (dualStreamWorker.joinable())
    {
        dualStreamWorker.join();
        LOG_DEBUG << "Dual stream worker thread joined" << std::endl;
        jpegEncoderDual.reset();
    }

    multiParams->setParameter(Camera::IMX708, Param::FRAME_DURATION_ENABLED, "false", ParamMode::STREAMING);
    multiParams->setParameter(Camera::IMX219, Param::FRAME_DURATION_ENABLED, "false", ParamMode::STREAMING);
    multiParams->setParameter(Camera::IMX708, Param::FRAME_DURATION_US, "0", ParamMode::STREAMING);
    multiParams->setParameter(Camera::IMX219, Param::FRAME_DURATION_US, "0", ParamMode::STREAMING);

 // Reset VBlank to minimum via V4L2 so hardware doesn't carry stale
    // frame duration into the next stream session.
   // Reset VBlank to minimum on all cameras so hardware doesn't carry
    // stale frame duration into the next stream session.
    auto resetVBlank = [](CameraBase* cam) {
        if (!cam) return;
        auto timing = cam->getSensorTiming();
        if (timing && timing->vblankMin > 0)
        {
            auto result = cam->setVBlank(timing->vblankMin);
            if (!result)
            {
                LOG_ERROR << "Failed to reset VBlank: " << cam->getLastError() << std::endl;
            }
            else
            {
                LOG_INFO << "Reset VBlank to " << timing->vblankMin << " lines" << std::endl;
            }
        }
    };
    resetVBlank(imx708Camera);
    resetVBlank(imx219Camera);

    LOG_INFO << "StatusMessage::STREAM_STOPPED" << std::endl;
    return buildJsonSuccess(sanuwave::protocol::StatusMessage::STREAM_STOPPED);
}

bool CommandHandler::startIMX708Stream(int width, int height, int quality)
{
    if (!imx708Camera)
        return false;
#ifdef MEASURE_FRAME_LATENCY
    imx708Camera->setLogFrameMetadata(true);
#endif
    CaptureSettings settings = multiParams->getRgbStreamingSettings();
    settings.width = width;
    settings.height = height;

    LOG_INFO << "Starting RGB stream" << std::endl;
    LOG_INFO << "  frameDurationEnabled=" << settings.frameDurationEnabled
             << " frameDuration_us=" << settings.frameDuration_us << std::endl;

    if (!imx708Camera->startStreaming(settings))
    {
        LOG_ERROR << "Failed to start RGB streaming: " << imx708Camera->getLastError() << std::endl;
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(streamStateMutex);
   
        streamModality = Camera::IMX708;
        streamQuality = quality;
        streamWidth = width;
        streamHeight = height;
    }

    streaming = true;
    StreamContext ctx{streaming, quality, width, height,
                     streamModality, StreamFormat::JPEG,
                     jpegEncoder.get(), streamCallback,
                     strobeController.get(),
                     pendingMotion_}; 
    streamWorker = std::thread(rgbStreamWorker, imx708Camera, std::move(ctx));

    return true;
}

bool CommandHandler::startIMX219Stream(int width, int height, int quality)
{
    if (!imx219Camera)
        return false;
#ifdef MEASURE_FRAME_LATENCY
    imx219Camera->setLogFrameMetadata(true);
#endif
    CaptureSettings settings = multiParams->getArducamStreamingSettings();
    settings.width = width;
    settings.height = height;

    LOG_INFO << "Starting Arducam stream" << std::endl;

    if (!imx219Camera->startStreaming(settings))
    {
        LOG_ERROR << "Failed to start Arducam streaming: " << imx219Camera->getLastError()
                  << std::endl;
        return false;
    }
    { 

        std::lock_guard<std::mutex> lk(streamStateMutex);
        streamModality = Camera::IMX219;
        streamQuality = quality;
        streamWidth = width;
        streamHeight = height;
    }
    streaming = true;

    StreamContext ctx{streaming, quality, width, height,
                     streamModality, StreamFormat::JPEG,
                     jpegEncoder.get(), streamCallback,
                     strobeController.get(),
                     pendingMotion_};  
    streamWorker = std::thread(rgbStreamWorker, imx219Camera, std::move(ctx));

    return true;
}

bool CommandHandler::startThermalStream(int width, int height, int quality)
{
    if (!thermalCamera)
    {
        LOG_ERROR << "Thermal camera pointer is null" << std::endl;
        return false;
    }
    if (!thermalCamera->isReady())
    {
        LOG_ERROR << "Thermal camera not ready: " << thermalCamera->getLastError() << std::endl;
        return false;
    }

    multiParams->applyThermalSettings(thermalCamera, ParamMode::STREAMING);
    LOG_INFO << "Starting Thermal stream" << std::endl;

    {
        std::lock_guard<std::mutex> lk(streamStateMutex);
    
        streamModality = Camera::THERMAL;
        streamQuality = quality;
        streamWidth = width;
        streamHeight = height;
    }
    streaming = true;

    int captureScale = std::max(1, width / 160);

    StreamContext ctx{streaming, quality, width, height,
                     streamModality, StreamFormat::JPEG,
                     jpegEncoder.get(), streamCallback,
                     nullptr,                          // no strobe on thermal
                     StreamContext::MotionConfig{}};   // motion disabled (rgb-only)
    streamWorker = std::thread(thermalStreamWorker, thermalCamera,
                               std::move(ctx), captureScale);

    return true;
}

bool CommandHandler::startDualStream(int rgbWidth, int rgbHeight, int quality)
{
    if (!imx708Camera)
    {
        LOG_ERROR << "RGB camera null for dual stream" << std::endl;
        return false;
    }
    if (!thermalCamera)
    {
        LOG_ERROR << "Thermal camera null for dual stream" << std::endl;
        return false;
    }
    if (!thermalCamera->isReady())
    {
        LOG_ERROR << "Thermal not ready for dual stream" << std::endl;
        return false;
    }

    CaptureSettings settings = multiParams->getRgbStreamingSettings();
    settings.width = rgbWidth;
    settings.height = rgbHeight;

    if (!imx708Camera->startStreaming(settings))
    {
        LOG_ERROR << "Failed to start RGB for dual mode" << std::endl;
        return false;
    }

    multiParams->applyThermalSettings(thermalCamera, ParamMode::STREAMING);

    LOG_INFO << "Starting dual stream (RGB + Thermal, decoupled)" << std::endl;

    dualStreaming = true;
    streaming = true;
    streamModality = Camera::DUAL;
    streamQuality = quality;
    streamWidth = rgbWidth;
    streamHeight = rgbHeight;

    jpegEncoderDual = JpegEncoderFactory::createDefaultEncoder();
    if (!jpegEncoderDual)
    {
        LOG_ERROR << "Failed to create second JPEG encoder for dual stream" << std::endl;
        return false;
    }

    // RGB thread — runs at camera's native rate
    StreamContext rgbCtx{dualStreaming, quality, rgbWidth, rgbHeight,
                         Modality::RGB, StreamFormat::JPEG,
                         jpegEncoder.get(), streamCallback,
                         nullptr,                // no strobe in dual mode
                         pendingMotion_};        // commit 3: rgb-only motion measurement
    streamWorker = std::thread(rgbStreamWorker, imx708Camera, std::move(rgbCtx));

    // Thermal thread — runs at Lepton's native rate.
    // Motion measurement is intentionally NOT enabled on thermal: phase
    // correlation on a Lepton's low-resolution low-contrast output is not
    // meaningful, and thermalStreamWorker is a separate function.
    StreamContext thermalCtx{dualStreaming, quality, 0, 0,
                             Modality::THERMAL, StreamFormat::JPEG,
                             jpegEncoderDual.get(), streamCallback,
                             nullptr,                          // no strobe
                             StreamContext::MotionConfig{}};   // motion disabled
    dualStreamWorker = std::thread(thermalStreamWorker, thermalCamera,
                                    std::move(thermalCtx), 1);

    return true;
}

std::string CommandHandler::handleIntervalStillStart(
    const std::map<std::string, std::string>& params)
{
    if (intervalShooting)
        return buildJsonError("Interval still already active");
    if (streaming)
        return buildJsonError("Cannot start interval still while streaming");

    ParamExtractor p(params);
    std::string camera  = p.getString(Param::CAMERA, Camera::IMX708);
    int width           = p.getInt(Param::WIDTH,       4608);
    int height          = p.getInt(Param::HEIGHT,      2592);
    int quality         = p.getInt(Param::QUALITY,     90);
    int intervalMs      = p.getInt(Param::INTERVAL_MS, 1000);

    if (intervalMs < 100)
        return buildJsonError("interval_ms minimum is 100");

    if (!startIntervalStill(camera, width, height, quality, intervalMs))
        return buildJsonError("Failed to start interval still");

    std::ostringstream oss;
    oss << R"({"type":"status","message":"Interval still started")"
        << R"(,"camera":")"      << camera      << R"(")"
        << R"(,"width":)"        << width
        << R"(,"height":)"       << height
        << R"(,"quality":)"      << quality
        << R"(,"interval_ms":)"  << intervalMs  << "}";
    return oss.str();
}

std::string CommandHandler::handleIntervalStillStop()
{
    if (!intervalShooting)
        return buildJsonError("No interval still active");

    intervalShooting = false;

    if (intervalStillWorker.joinable())
        intervalStillWorker.join();

    LOG_INFO << "Interval still stopped" << std::endl;
    return R"({"type":"status","message":"Interval still stopped"})";
}

bool CommandHandler::startIntervalStill(const std::string& camera,
                                         int width, int height,
                                         int quality, int intervalMs)
{
    CameraBase* cam = nullptr;
    std::string modality;

    if (camera == Camera::IMX708)
    {
        if (!imx708Camera) { LOG_ERROR << "IMX708 not available" << std::endl; return false; }
        cam      = imx708Camera;
        modality = Camera::IMX708_STILL;
    }
    else if (camera == Camera::IMX219)
    {
        if (!imx219Camera) { LOG_ERROR << "IMX219 not available" << std::endl; return false; }
        cam      = imx219Camera;
        modality = Camera::IMX219_STILL;
    }
    else
    {
        LOG_ERROR << "Unsupported camera for interval still: " << camera << std::endl;
        return false;
    }

    intervalShooting = true;

    intervalStillWorker = std::thread([this, cam, modality, width, height,
                                       quality, intervalMs]()
    {
        LOG_INFO << "Interval still worker started: " << modality
                 << " " << width << "x" << height
                 << " every " << intervalMs << "ms" << std::endl;

        uint64_t frameCount = 0;
        const auto interval = std::chrono::milliseconds(intervalMs);

        // Choose settings based on camera type
        CaptureSettings settings = (modality == Camera::IMX708_STILL)
            ? multiParams->getRgbCaptureSettings()
            : multiParams->getArducamCaptureSettings();

        settings.width  = width;
        settings.height = height;
        ConvergedSettings lastConverged;
        lastConverged.valid = false;
        bool firstImage = true;
        while (intervalShooting)
        {
            auto captureStart = std::chrono::steady_clock::now();

            try
            {
                FrameMetadata meta;

                // On first frame, let capture() do full warmup as normal.
                // On subsequent frames, override auto settings with last
                // converged values to skip warmup entirely.
                CaptureSettings frameSettings = settings;
                if (firstImage)
                {
                    // First frame: use auto settings so warmup/convergence runs normally
                    frameSettings.autoExposure     = true;
                    frameSettings.autoAnalogGain         = true;
                    frameSettings.autoWhiteBalance = true;
                }
                else
                {
                    // Subsequent frames: supply converged values directly, warmup is skipped
                    frameSettings.autoExposure     = false;
                    frameSettings.exposureTime_us  = lastConverged.exposureTime_us;
                    frameSettings.autoAnalogGain         = false;
                    frameSettings.analogGain       = lastConverged.analogGain;
                    frameSettings.autoWhiteBalance = false;
                    frameSettings.redGain          = lastConverged.redGain;
                    frameSettings.blueGain         = lastConverged.blueGain;
                }


                cv::Mat image = cam->capture(frameSettings, &meta);
                // Cache converged values from metadata for next iteration
                if (meta.valid)
                {
                    lastConverged.valid           = true;
                    lastConverged.exposureTime_us = meta.exposureTime_us;
                    lastConverged.analogGain      = meta.analogGain;
                    lastConverged.redGain         = meta.redGain;
                    lastConverged.blueGain        = meta.blueGain;
                    firstImage                    = false;
                }



                if (!image.empty())
                {
                    // Update last converged settings for next frame
                
                    lastConverged.exposureTime_us = meta.exposureTime_us;
                    lastConverged.analogGain = meta.analogGain;
                    lastConverged.redGain = meta.redGain;
                    lastConverged.blueGain = meta.blueGain;
                    lastConverged.valid = true;
                }
                if (image.empty())
                {
                    LOG_WARNING << "Interval still: empty frame, skipping" << std::endl;
                }
                else if (streamCallback)
                {
                    std::vector<uint8_t> encoded =
                        cam->encodeImageForTransmission(image, true, quality);

                    if (!encoded.empty())
                    {
                        auto now = std::chrono::system_clock::now();
                        uint64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()).count();

                        StreamFrameMeta frameMeta;
                        frameMeta.modality     = modality;
                        frameMeta.format       = StreamFormat::JPEG;
                        frameMeta.width        = width;
                        frameMeta.height       = height;
                        frameMeta.timestamp_ms = ts;
                        // motion left default (valid==false): interval-still
                        // does not measure motion.

                        streamCallback(encoded, frameMeta);
                        frameCount++;

                        LOG_DEBUG << "Interval still sent frame " << frameCount
                                  << " (" << encoded.size() << " bytes)" << std::endl;
                    }
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR << "Interval still capture exception: " << e.what() << std::endl;
            }

            // Sleep for the remainder of the interval
            auto elapsed = std::chrono::steady_clock::now() - captureStart;
            if (elapsed < interval)
                std::this_thread::sleep_for(interval - elapsed);
        }

        LOG_INFO << "Interval still worker exiting ("
                 << frameCount << " frames sent)" << std::endl;
    });

    return true;
}

std::string CommandHandler::handleALSInit(const std::map<std::string, std::string> &)
{
    LOG_INFO << "Initializing ALS sensor..." << std::endl;

    if (!alsSensor)
        return buildJsonError("ALS sensor not available");

    try
    {
        if (alsSensor->isInitialized())
            return buildJsonSuccess(sanuwave::protocol::StatusMessage::ALS_INITIALIZED);

        if (!alsSensor->init(VD6283TX::DEFAULT_I2C_ADDR))
            return buildJsonError("Failed to init ALS sensor: " + alsSensor->getLastError());

        LOG_INFO << sanuwave::protocol::StatusMessage::ALS_INITIALIZED << std::endl;
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::ALS_INITIALIZED);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Init exception: ") + e.what());
    }
}

std::string CommandHandler::handleALSShutdown(const std::map<std::string, std::string> &)
{
    LOG_INFO << "Shutting down ALS sensor..." << std::endl;

    if (!alsSensor)
        return buildJsonError("ALS sensor not available");

    try
    {
        if (!alsSensor->isInitialized())
            return buildJsonSuccess("ALS sensor not initialized");

        alsSensor->shutdown();
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::ALS_SHUTDOWN);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Shutdown exception: ") + e.what());
    }
}


std::string CommandHandler::handleALSRead(const std::map<std::string, std::string> &)
{
    if (!alsSensor || !alsSensor->isInitialized())
        return buildJsonError("ALS sensor not initialized");

    try
    {
        sanuwave::ALSData data;
        if (!alsSensor->readSingle(data))
            return buildJsonError("Measurement failed: " + alsSensor->getLastError());

        std::ostringstream oss;
        oss << R"({"type":"als_data")"
            << R"(,"red":)"     << data.red
            << R"(,"green":)"   << data.green
            << R"(,"blue":)"    << data.blue
            << R"(,"clear":)"   << data.clear
            << R"(,"ir":)"      << data.ir
            << R"(,"visible":)" << data.visible
            << R"(,"valid":)"   << (data.valid ? "true" : "false")
            << R"(,"timestamp":)" << data.timestamp
            << R"(,"ready_to_send":true})";
        return oss.str();
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("ALS read exception: ") + e.what());
    }
}


std::string CommandHandler::handleALSSetGain(const std::map<std::string, std::string> &params)
{
    if (!alsSensor || !alsSensor->isInitialized())
        return buildJsonError("ALS sensor not initialized");

    ParamExtractor p(params);

    // Accept a numeric gain code (0x01–0x0F) matching VD6283TX::Gain enum values.
    // Client sends "gain" as an integer gain code. Default to X1 (0x0D).
    int gainCode = p.getInt(Param::GAIN, static_cast<int>(VD6283TX::Gain::X1));
    gainCode = std::clamp(gainCode, 0x01, 0x0F);

    auto gain = static_cast<VD6283TX::Gain>(gainCode);

    LOG_INFO << "Setting ALS gain code: 0x" << std::hex << gainCode << std::dec << std::endl;

    try
    {
        if (!alsSensor->setGainAll(gain))
            return buildJsonError("Failed to set gain: " + alsSensor->getLastError());
        return buildJsonSuccess("ALS gain set");
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Set gain exception: ") + e.what());
    }
}

std::string CommandHandler::handleALSSetExposure(const std::map<std::string, std::string> &params)
{
    if (!alsSensor || !alsSensor->isInitialized())
        return buildJsonError("ALS sensor not initialized");

    ParamExtractor p(params);
    float exposure_ms = p.getFloat(Param::EXPOSURE_MS, 50.0f);

    LOG_INFO << "Setting ALS exposure: " << exposure_ms << " ms" << std::endl;

    try
    {
        if (!alsSensor->setExposureMs(exposure_ms))
            return buildJsonError("Failed to set exposure: " + alsSensor->getLastError());
        return buildJsonSuccess("ALS exposure set to " + std::to_string(exposure_ms) + " ms");
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Set exposure exception: ") + e.what());
    }
}


std::string CommandHandler::handleDistanceInit(const std::map<std::string, std::string> &)
{
    LOG_INFO << "Initializing distance sensor..." << std::endl;
    if (!distanceSensor)
        return buildJsonError("Distance sensor not available");

    try
    {
        if (distanceSensor->isInitialized())
            return buildJsonSuccess(sanuwave::protocol::StatusMessage::DISTANCE_INITIALIZED);

         VL53L4CDWrapper::Config config;
        config.mode = VL53L4CDWrapper::RangingMode::SHORT;
        config.timingBudget_ms = 50;
        config.intermeasurementPeriod_ms = 100;
        config.errorCallback = [](const std::string &error)
        { LOG_ERROR << "[VL53L4CD] " << error << std::endl; };

        if (!distanceSensor->init(config))
            return buildJsonError("Failed to init distance sensor: " +
                                  distanceSensor->getLastError());

        LOG_INFO << sanuwave::protocol::StatusMessage::DISTANCE_INITIALIZED << std::endl;
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::DISTANCE_INITIALIZED);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Init exception: ") + e.what());
    }
}

std::string CommandHandler::handleDistanceStart(const std::map<std::string, std::string> &)
{
    LOG_INFO << "Starting distance ranging..." << std::endl;

    if (!distanceSensor || !distanceSensor->isInitialized())
        return buildJsonError("Distance sensor not initialized");

    try
    {
        if (distanceRanging)
            return buildJsonSuccess(sanuwave::protocol::StatusMessage::DISTANCE_STARTED);

        if (!distanceSensor->startRanging())
            return buildJsonError("Failed to start ranging: " + distanceSensor->getLastError());

        distanceRanging = true;
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::DISTANCE_STARTED);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Start ranging exception: ") + e.what());
    }
}

std::string CommandHandler::handleDistanceStop(const std::map<std::string, std::string> &)
{
    LOG_INFO << "Stopping distance ranging..." << std::endl;

    if (!distanceSensor || !distanceSensor->isInitialized())
        return buildJsonError("Distance sensor not initialized");

    try
    {
        if (!distanceRanging)
            return buildJsonSuccess(sanuwave::protocol::StatusMessage::DISTANCE_STOPPED);

        if (!distanceSensor->stopRanging())
            return buildJsonError("Failed to stop ranging: " + distanceSensor->getLastError());

        distanceRanging = false;
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::DISTANCE_STOPPED);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Stop ranging exception: ") + e.what());
    }
}

std::string CommandHandler::handleDistanceRead(const std::map<std::string, std::string> &)
{
    if (!distanceSensor || !distanceSensor->isInitialized())
        return buildJsonError("Distance sensor not initialized");

    try
    {
        VL53L4CDWrapper::Measurement m = distanceSensor->getMeasurement(1000);

        std::ostringstream oss;
        oss << R"({"type":"distance_data")"
            << R"(,")" << sanuwave::protocol::DistanceField::DISTANCE_MM      << R"(":)" << m.distance_mm
            << R"(,")" << sanuwave::protocol::DistanceField::SIGNAL_PER_SPAD  << R"(":)" << m.signal_per_spad
            << R"(,")" << sanuwave::protocol::DistanceField::AMBIENT_PER_SPAD << R"(":)" << m.ambient_per_spad
            << R"(,")" << sanuwave::protocol::DistanceField::NUM_SPADS        << R"(":)" << static_cast<int>(m.num_spads)
            << R"(,")" << sanuwave::protocol::DistanceField::RANGE_STATUS     << R"(":)" << static_cast<int>(m.range_status)
            << R"(,"range_status_text":")" << VL53L4CDWrapper::getRangeStatusString(m.range_status) << R"(")"
            << R"(,")" << sanuwave::protocol:: DistanceField::VALID            << R"(":)" << (m.valid ? "true" : "false")
            << R"(,"ready_to_send":true})";
        return oss.str();
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Distance read exception: ") + e.what());
    }
}


std::string CommandHandler::handleDistanceSetMode(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    std::string mode = p.getString(Param::MODE);

    LOG_INFO << "Setting distance sensor mode: " << mode << std::endl;

    if (!distanceSensor || !distanceSensor->isInitialized())
        return buildJsonError("Distance sensor not initialized");

    try
    {
        VL53L4CDWrapper::RangingMode rangingMode;
        if (mode == "short" || mode == "SHORT")
            rangingMode = VL53L4CDWrapper::RangingMode::SHORT;
        else
            return buildJsonError("Invalid mode. Use 'short'");

        if (!distanceSensor->setRangingMode(rangingMode))
            return buildJsonError("Failed to set mode: " + distanceSensor->getLastError());

        return buildJsonSuccess("Mode set to " + mode);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Set mode exception: ") + e.what());
    }
}

std::string CommandHandler::handleDistanceCalibrate(
    const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    std::string type = p.getString("type");
    int targetDistance = p.getInt(Param::TARGET_DISTANCE, 100);

    LOG_INFO << "Calibrating distance sensor (" << type << ") at " << targetDistance << " mm"
             << std::endl;

    if (!distanceSensor || !distanceSensor->isInitialized())
        return buildJsonError("Distance sensor not initialized");

    try
    {
        bool success = false;
        if (type == "offset")
            success = distanceSensor->calibrateOffset(targetDistance);
        else if (type == "crosstalk" || type == "xtalk")
            success = distanceSensor->calibrateCrosstalk(targetDistance);
        else
            return buildJsonError("Invalid calibration type. Use 'offset' or 'crosstalk'");

        if (!success)
            return buildJsonError("Calibration failed: " + distanceSensor->getLastError());

        return buildJsonSuccess("Calibration complete");
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Calibration exception: ") + e.what());
    }
}


//Pulls a uint8 out of the
// param map; returns false if missing or out of range.
namespace {

bool extractU8(const std::map<std::string, std::string>& params,
               const std::string& key,
               uint8_t& outValue,
               std::string& outError)
{
    auto it = params.find(key);
    if (it == params.end()) {
        outError = "missing param: " + key;
        return false;
    }
    try {
        int v = std::stoi(it->second, nullptr, 0);  // base 0 → accepts 0x..
        if (v < 0 || v > 0xFF) {
            outError = key + " out of range";
            return false;
        }
        outValue = static_cast<uint8_t>(v);
        return true;
    } catch (...) {
        outError = "bad value for " + key + ": " + it->second;
        return false;
    }
}

bool extractBool(const std::map<std::string, std::string>& params,
                 const std::string& key,
                 bool fallback)
{
    auto it = params.find(key);
    if (it == params.end()) return fallback;
    return it->second == "true" || it->second == "1";
}

bool extractU16(const std::map<std::string, std::string>& params,
                const std::string& key,
                uint16_t fallback)
{
    auto it = params.find(key);
    if (it == params.end()) return fallback;
    try {
        int v = std::stoi(it->second, nullptr, 0);
        if (v < 0 || v > 0xFFFF) return fallback;
        return static_cast<uint16_t>(v);
    } catch (...) { return fallback; }
}

sanuwave::Lsm6ds3trc::ChipAxis chipAxisFromStr(const std::string& s, bool& ok)
{
    ok = true;
    if (s == "X" || s == "x") return sanuwave::Lsm6ds3trc::ChipAxis::X;
    if (s == "Y" || s == "y") return sanuwave::Lsm6ds3trc::ChipAxis::Y;
    if (s == "Z" || s == "z") return sanuwave::Lsm6ds3trc::ChipAxis::Z;
    ok = false;
    return sanuwave::Lsm6ds3trc::ChipAxis::X;
}

}  // anonymous namespace


std::string CommandHandler::handleImuInit(const std::map<std::string, std::string>& params)
{
    LOG_INFO << "Initializing IMU..." << std::endl;
    if (!imuSensor)
        return buildJsonError("IMU not available");

    try
    {
        if (imuSensor->isInitialized())
            return buildJsonSuccess(sanuwave::protocol::StatusMessage::IMU_INITIALIZED);

        // Allow override of the I2C address — defaults to 0x6A (Qwiic).
        uint8_t addr = 0x6A;
        std::string err;
        if (params.count(sanuwave::protocol::ImuParam::REG_ADDRESS)) {
            // Reuse the reg_address param key for the slave address override
            // — keeps the parameter set small. Real applications with both
            // SA0 strap options would add a dedicated slave_address key.
            extractU8(params, sanuwave::protocol::ImuParam::REG_ADDRESS, addr, err);
        }

        if (!imuSensor->init(addr))
            return buildJsonError("Failed to init IMU: " + imuSensor->getLastError());

        LOG_INFO << sanuwave::protocol::StatusMessage::IMU_INITIALIZED << std::endl;
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::IMU_INITIALIZED);
    }
    catch (const std::exception& e)
    {
        return buildJsonError(std::string("IMU init exception: ") + e.what());
    }
}


std::string CommandHandler::handleImuShutdown(const std::map<std::string, std::string>&)
{
    LOG_INFO << "Shutting down IMU..." << std::endl;
    if (!imuSensor)
        return buildJsonError("IMU not available");
    imuSensor->stop();
    return buildJsonSuccess(sanuwave::protocol::StatusMessage::IMU_SHUTDOWN_OK);
}


std::string CommandHandler::handleImuConfigure(const std::map<std::string, std::string>& params)
{
    using namespace sanuwave;
    using namespace sanuwave::protocol;

    if (!imuSensor || !imuSensor->isInitialized())
        return buildJsonError("IMU not initialized");

    Lsm6ds3trc::Config cfg;  // start from defaults; override what's present
    std::string err;
    uint8_t u8;

    if (params.count(ImuParam::ACCEL_ODR) && extractU8(params, ImuParam::ACCEL_ODR, u8, err))
        cfg.accelOdr = static_cast<Lsm6ds3trc::Odr>(u8);
    if (params.count(ImuParam::GYRO_ODR)  && extractU8(params, ImuParam::GYRO_ODR,  u8, err))
        cfg.gyroOdr  = static_cast<Lsm6ds3trc::Odr>(u8);
    if (params.count(ImuParam::ACCEL_FS)  && extractU8(params, ImuParam::ACCEL_FS,  u8, err))
        cfg.accelFs  = static_cast<Lsm6ds3trc::AccelFs>(u8);
    if (params.count(ImuParam::GYRO_FS)   && extractU8(params, ImuParam::GYRO_FS,   u8, err))
        cfg.gyroFs   = static_cast<Lsm6ds3trc::GyroFs>(u8);
    if (params.count(ImuParam::FIFO_MODE) && extractU8(params, ImuParam::FIFO_MODE, u8, err))
        cfg.fifoMode = static_cast<Lsm6ds3trc::FifoMode>(u8);

    cfg.fifoWatermark   = extractU16(params, ImuParam::FIFO_WATERMARK, cfg.fifoWatermark);
    cfg.blockDataUpdate = extractBool(params, ImuParam::BLOCK_DATA_UPDATE, cfg.blockDataUpdate);

    cfg.tap.enabled      = extractBool(params, ImuParam::TAP_ENABLED,  cfg.tap.enabled);
    cfg.tap.enableX      = extractBool(params, ImuParam::TAP_AXIS_X,   cfg.tap.enableX);
    cfg.tap.enableY      = extractBool(params, ImuParam::TAP_AXIS_Y,   cfg.tap.enableY);
    cfg.tap.enableZ      = extractBool(params, ImuParam::TAP_AXIS_Z,   cfg.tap.enableZ);
    cfg.tap.enableDouble = extractBool(params, ImuParam::TAP_DOUBLE,   cfg.tap.enableDouble);
    if (params.count(ImuParam::TAP_THRESHOLD) && extractU8(params, ImuParam::TAP_THRESHOLD, u8, err)) cfg.tap.threshold = u8;
    if (params.count(ImuParam::TAP_SHOCK)     && extractU8(params, ImuParam::TAP_SHOCK,     u8, err)) cfg.tap.shock     = u8;
    if (params.count(ImuParam::TAP_QUIET)     && extractU8(params, ImuParam::TAP_QUIET,     u8, err)) cfg.tap.quiet     = u8;
    if (params.count(ImuParam::TAP_DURATION)  && extractU8(params, ImuParam::TAP_DURATION,  u8, err)) cfg.tap.duration  = u8;

    cfg.freeFall.enabled = extractBool(params, ImuParam::FREE_FALL_ENABLED, cfg.freeFall.enabled);
    if (params.count(ImuParam::FREE_FALL_THRESHOLD) && extractU8(params, ImuParam::FREE_FALL_THRESHOLD, u8, err)) cfg.freeFall.threshold = u8;
    if (params.count(ImuParam::FREE_FALL_DURATION ) && extractU8(params, ImuParam::FREE_FALL_DURATION,  u8, err)) cfg.freeFall.duration  = u8;

    cfg.wake.enabled = extractBool(params, ImuParam::WAKE_ENABLED, cfg.wake.enabled);
    if (params.count(ImuParam::WAKE_THRESHOLD) && extractU8(params, ImuParam::WAKE_THRESHOLD, u8, err)) cfg.wake.threshold = u8;
    if (params.count(ImuParam::WAKE_DURATION ) && extractU8(params, ImuParam::WAKE_DURATION,  u8, err)) cfg.wake.duration  = u8;

    cfg.routing.int1_fifoWatermark = extractBool(params, ImuParam::INT1_FIFO_WATERMARK, cfg.routing.int1_fifoWatermark);
    cfg.routing.int1_fifoOverrun   = extractBool(params, ImuParam::INT1_FIFO_OVERRUN,   cfg.routing.int1_fifoOverrun);
    cfg.routing.int1_dataReady     = extractBool(params, ImuParam::INT1_DATA_READY,     cfg.routing.int1_dataReady);
    cfg.routing.int2_freeFall      = extractBool(params, ImuParam::INT2_FREE_FALL,      cfg.routing.int2_freeFall);
    cfg.routing.int2_singleTap     = extractBool(params, ImuParam::INT2_SINGLE_TAP,     cfg.routing.int2_singleTap);
    cfg.routing.int2_doubleTap     = extractBool(params, ImuParam::INT2_DOUBLE_TAP,     cfg.routing.int2_doubleTap);

    // Coordinate frame — only override if all six params are present.
    if (params.count(ImuParam::FRAME_X_SOURCE) && params.count(ImuParam::FRAME_Y_SOURCE) &&
        params.count(ImuParam::FRAME_Z_SOURCE))
    {
        bool axOk = true;
        Lsm6ds3trc::CoordinateFrame f;
        f.x.source = chipAxisFromStr(params.at(ImuParam::FRAME_X_SOURCE), axOk);
        f.y.source = chipAxisFromStr(params.at(ImuParam::FRAME_Y_SOURCE), axOk);
        f.z.source = chipAxisFromStr(params.at(ImuParam::FRAME_Z_SOURCE), axOk);
        try {
            f.x.sign = static_cast<int8_t>(std::stoi(params.at(ImuParam::FRAME_X_SIGN)));
            f.y.sign = static_cast<int8_t>(std::stoi(params.at(ImuParam::FRAME_Y_SIGN)));
            f.z.sign = static_cast<int8_t>(std::stoi(params.at(ImuParam::FRAME_Z_SIGN)));
        } catch (...) { axOk = false; }
        if (!axOk || !f.isValid())
            return buildJsonError("imu_configure: invalid coordinate frame");
        cfg.frame = f;
    }

    if (!imuSensor->configure(cfg))
        return buildJsonError("Failed to configure IMU: " + imuSensor->getLastError());

    return buildJsonSuccess(StatusMessage::IMU_CONFIGURED);
}


std::string CommandHandler::handleImuStart(const std::map<std::string, std::string>&)
{
    LOG_INFO << "Starting IMU streaming..." << std::endl;
    if (!imuSensor || !imuSensor->isInitialized())
        return buildJsonError("IMU not initialized");

    try
    {
        if (imuSensor->isStreaming())
            return buildJsonSuccess(sanuwave::protocol::StatusMessage::IMU_STARTED);
        if (!imuSensor->start())
            return buildJsonError("Failed to start IMU streaming: " + imuSensor->getLastError());
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::IMU_STARTED);
    }
    catch (const std::exception& e)
    {
        return buildJsonError(std::string("IMU start exception: ") + e.what());
    }
}


std::string CommandHandler::handleImuStop(const std::map<std::string, std::string>&)
{
    LOG_INFO << "Stopping IMU streaming..." << std::endl;
    if (!imuSensor)
        return buildJsonError("IMU not available");

    try
    {
        if (!imuSensor->isStreaming())
            return buildJsonSuccess(sanuwave::protocol::StatusMessage::IMU_STOPPED);
        imuSensor->stop();
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::IMU_STOPPED);
    }
    catch (const std::exception& e)
    {
        return buildJsonError(std::string("IMU stop exception: ") + e.what());
    }
}


std::string CommandHandler::handleImuSoftReset(const std::map<std::string, std::string>&)
{
    LOG_INFO << "IMU soft reset" << std::endl;
    if (!imuSensor)
        return buildJsonError("IMU not available");
    if (!imuSensor->softReset())
        return buildJsonError("IMU soft reset failed: " + imuSensor->getLastError());
    return buildJsonSuccess("IMU soft reset OK");
}


std::string CommandHandler::handleImuReadReg(const std::map<std::string, std::string>& params)
{
    using namespace sanuwave::protocol;
    if (!imuSensor || !imuSensor->isInitialized())
        return buildJsonError("IMU not initialized");

    uint8_t reg = 0;
    std::string err;
    if (!extractU8(params, ImuParam::REG_ADDRESS, reg, err))
        return buildJsonError("imu_read_reg: " + err);

    uint8_t val = 0;
    if (!imuSensor->readRegister(reg, val))
        return buildJsonError("imu_read_reg: " + imuSensor->getLastError());

    // Single-message reply with the register value embedded.
    std::ostringstream ss;
    ss << R"({"type":")" << ResponseType::IMU_REG << R"(",)"
       << R"(")" << ImuField::VALID       << R"(":true,)"
       << R"(")" << ImuField::REG_ADDRESS << R"(":)" << static_cast<int>(reg) << ","
       << R"(")" << ImuField::REG_VALUE   << R"(":)" << static_cast<int>(val)
       << "}";
    return ss.str();
}


std::string CommandHandler::handleImuWriteReg(const std::map<std::string, std::string>& params)
{
    using namespace sanuwave::protocol;
    if (!imuSensor || !imuSensor->isInitialized())
        return buildJsonError("IMU not initialized");

    uint8_t reg = 0, val = 0;
    std::string err;
    if (!extractU8(params, ImuParam::REG_ADDRESS, reg, err))
        return buildJsonError("imu_write_reg: " + err);
    if (!extractU8(params, ImuParam::REG_VALUE,   val, err))
        return buildJsonError("imu_write_reg: " + err);

    if (!imuSensor->writeRegister(reg, val))
        return buildJsonError("imu_write_reg: " + imuSensor->getLastError());

    return buildJsonSuccess(StatusMessage::IMU_REG_WRITTEN);
}



std::string CommandHandler::handleUVInit(const std::map<std::string, std::string> &)
{
    LOG_INFO << "Initializing UV sensor..." << std::endl;

    if (!uvSensor)
        return buildJsonError("UV sensor not available");

    try
    {
        if (uvSensor->isInitialized())
            return buildJsonSuccess(sanuwave::protocol::StatusMessage::UV_INITIALIZED);

        if (!uvSensor->init(0x74))
            return buildJsonError("Failed to init UV sensor: " + uvSensor->getLastError());

        return buildJsonSuccess(sanuwave::protocol::StatusMessage::UV_INITIALIZED);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Init exception: ") + e.what());
    }
}

std::string CommandHandler::handleUVShutdown(const std::map<std::string, std::string> &)
{
    LOG_INFO << "Shutting down UV sensor..." << std::endl;

    if (!uvSensor)
        return buildJsonError("UV sensor not available");

    try
    {
        if (!uvSensor->isInitialized())
            return buildJsonSuccess("UV sensor not initialized");

        uvSensor->shutdown();
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::UV_SHUTDOWN);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Shutdown exception: ") + e.what());
    }
}

std::string CommandHandler::handleUVRead(const std::map<std::string, std::string> &)
{
    if (!uvSensor || !uvSensor->isInitialized())
        return buildJsonError("UV sensor not initialized");

    try
    {
        float uva, uvb, uvc, temp_c;
        if (!uvSensor->takeMeasurement(uva, uvb, uvc, temp_c))
            return buildJsonError("Measurement failed: " + uvSensor->getLastError());

        auto now = std::chrono::system_clock::now();
        uint64_t timestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        std::ostringstream oss;
        oss << R"({"type":"uv_data")"
            << R"(,"uva":)" << uva << R"(,"uvb":)" << uvb << R"(,"uvc":)" << uvc << R"(,"temp_c":)"
            << temp_c << R"(,"valid":true)"
            << R"(,"timestamp":)" << timestamp << R"(,"ready_to_send":true})";
        return oss.str();
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("UV read exception: ") + e.what());
    }
}

std::string CommandHandler::handleUVSetMode(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    std::string mode = p.getString(Param::MODE);

    LOG_INFO << "Setting UV sensor mode: " << mode << std::endl;

    if (!uvSensor || !uvSensor->isInitialized())
        return buildJsonError("UV sensor not initialized");

    try
    {
        if (!uvSensor->setMode(mode))
            return buildJsonError("Failed to set mode: " + uvSensor->getLastError());
        return buildJsonSuccess("Mode set to " + mode);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Set mode exception: ") + e.what());
    }
}

std::string CommandHandler::handleUVSetGain(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    std::string gain = p.getString(Param::GAIN);

    LOG_INFO << "Setting UV sensor gain: " << gain << std::endl;

    if (!uvSensor || !uvSensor->isInitialized())
        return buildJsonError("UV sensor not initialized");

    try
    {
        if (!uvSensor->setGain(gain))
            return buildJsonError("Failed to set gain: " + uvSensor->getLastError());
        return buildJsonSuccess("Gain set to " + gain);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Set gain exception: ") + e.what());
    }
}

std::string CommandHandler::handleUVSetTime(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    std::string time = p.getString("time");

    LOG_INFO << "Setting UV sensor integration time: " << time << std::endl;

    if (!uvSensor || !uvSensor->isInitialized())
        return buildJsonError("UV sensor not initialized");

    try
    {
        if (!uvSensor->setIntegrationTime(time))
            return buildJsonError("Failed to set integration time: " + uvSensor->getLastError());
        return buildJsonSuccess("Integration time set to " + time);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Set time exception: ") + e.what());
    }
}


// ============================================================
// LED handler implementations
// ============================================================

std::string CommandHandler::handleLedInit(const std::map<std::string, std::string> &)
{
    LOG_INFO << "Initializing LED manager..." << std::endl;

    if (!ledManager)
        return buildJsonError("LED manager not available");

    try
    {
        if (ledManager->isInitialized())
            return buildJsonSuccess(sanuwave::protocol::StatusMessage::LED_INITIALIZED);

        if (!ledManager->initialize())
            return buildJsonError("Failed to initialize LED manager");

        LOG_INFO << sanuwave::protocol::StatusMessage::LED_INITIALIZED << std::endl;
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::LED_INITIALIZED);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("LED init exception: ") + e.what());
    }
}

std::string CommandHandler::handleLedShutdown(const std::map<std::string, std::string> &)
{
    LOG_INFO << "Shutting down LED manager..." << std::endl;

    if (!ledManager)
        return buildJsonError("LED manager not available");

    try
    {
        if (!ledManager->isInitialized())
            return buildJsonSuccess("LED manager not initialized");

        // Turn off all LEDs before shutdown
        for (int i = 0; i < 32; ++i)
            ledManager->turnOff(static_cast<LedMgr::LedId>(i));

        LOG_INFO << sanuwave::protocol::StatusMessage::LED_SHUTDOWN << std::endl;
        return buildJsonSuccess(sanuwave::protocol::StatusMessage::LED_SHUTDOWN);
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("LED shutdown exception: ") + e.what());
    }
}

std::string CommandHandler::handleLedTorch(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    int ledId     = p.getInt("led_id", -1);
    int brightness = p.getInt("brightness", 128);

    if (ledId < 0 || ledId > 31)
        return buildJsonError("Invalid led_id (0-31)");

    if (!ledManager || !ledManager->isInitialized())
        return buildJsonError("LED manager not initialized");

    try
    {
        ledManager->setFlashTorchBrightness(static_cast<LedMgr::LedId>(ledId), static_cast<uint8_t>(brightness));
        if (!ledManager->setTorchMode(static_cast<LedMgr::LedId>(ledId)))
        {
            return buildJsonError("Failed to set torch mode for LED " + std::to_string(ledId));
        }

        // Track in selectedLeds so getCurrentLedState() captures it in the sidecar
        {
            std::lock_guard<std::mutex> lk(ledCaptureMutex);
            auto it = std::find_if(selectedLeds.begin(), selectedLeds.end(),
                                   [ledId](const LedSelection& s){ return s.ledId == ledId; });
            if (it != selectedLeds.end())
                it->brightness = static_cast<uint8_t>(brightness);
            else
                selectedLeds.push_back({ledId, static_cast<uint8_t>(brightness)});
        }

        std::ostringstream oss;
        oss << R"({"type":"status","message":"LED torch set")"
            << R"(,"led_id":)"    << ledId
            << R"(,"brightness":)" << brightness << "}";
        return oss.str();
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("LED torch exception: ") + e.what());
    }
}

std::string CommandHandler::handleLedFlash(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    int ledId = p.getInt("led_id", -1);
    int brightness = p.getInt("brightness", 128);
    int duration_ms = p.getInt("duration_ms", 100);

    if (ledId < 0 || ledId > 31)
        return buildJsonError("Invalid led_id (0-31)");

    LOG_INFO << "LED flash: id=" << ledId << " brightness=" << brightness
             << " duration=" << duration_ms << "ms" << std::endl;

    if (!ledManager || !ledManager->isInitialized())
        return buildJsonError("LED manager not initialized");

    try
    {
         if (!ledManager->setFlashTorchBrightness(
            static_cast<LedMgr::LedId>(ledId), static_cast<uint8_t>(brightness)))
        {
            LOG_ERROR << "LED flash: failed to set brightness for LED " << ledId
                    << ": " << ledManager->getLastError() << std::endl;
            return buildJsonError("Failed to set brightness for LED " + std::to_string(ledId)
                                + ": " + ledManager->getLastError());
        }


        if (!ledManager->setFlashTimeout(
            static_cast<LedMgr::LedId>(ledId), static_cast<uint16_t>(duration_ms)))
        {
            LOG_ERROR << "LED flash: failed to set timeout for LED " << ledId
                    << ": " << ledManager->getLastError() << std::endl;
            return buildJsonError("Failed to set flash timeout for LED " + std::to_string(ledId)
                                + ": " + ledManager->getLastError());
        }

        if (!ledManager->setFlashMode(static_cast<LedMgr::LedId>(ledId)))
        {
            LOG_ERROR << "LED flash: failed to set flash mode for LED " << ledId
                    << ": " << ledManager->getLastError() << std::endl;
            return buildJsonError("Failed to set flash mode for LED " + std::to_string(ledId)
                                + ": " + ledManager->getLastError());
        }

        std::ostringstream oss;
        oss << R"({"type":"status","message":"LED flash set")"
            << R"(,"led_id":)" << ledId
            << R"(,"brightness":)" << brightness
            << R"(,"duration_ms":)" << duration_ms << "}";
        return oss.str();
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("LED flash exception: ") + e.what());
    }
}

std::string CommandHandler::handleLedOff(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    int ledId = p.getInt("led_id", -1);

    if (ledId < 0 || ledId > 31)
        return buildJsonError("Invalid led_id (0-31)");

    LOG_INFO << "LED off: id=" << ledId << std::endl;

    if (!ledManager || !ledManager->isInitialized())
        return buildJsonError("LED manager not initialized");

    try
    {
        if (!ledManager->turnOff(static_cast<LedMgr::LedId>(ledId)))
            return buildJsonError("Failed to turn off LED " + std::to_string(ledId));
{
        std::lock_guard<std::mutex> lk(ledCaptureMutex);
        selectedLeds.erase(
            std::remove_if(selectedLeds.begin(), selectedLeds.end(),
                        [ledId](const LedSelection& s){ return s.ledId == ledId; }),
            selectedLeds.end());
}
        return buildJsonSuccess("LED " + std::to_string(ledId) + " off");
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("LED off exception: ") + e.what());
    }
}

std::string CommandHandler::handleLedAllOff(
    const std::map<std::string, std::string> &/*params*/)
{
    if (!ledManager || !ledManager->isInitialized())
        return buildJsonError("led_all_off: LED manager not initialized");
    if (!ledGpio)
        return buildJsonError("led_all_off: LED GPIO not available");

    LOG_INFO << "led_all_off: strobeOff + torchOff + disarm" << std::endl;

    // Drop GPIO lines first
    ledGpio->strobeOff(LedGpioController::Group::ALL);
    ledGpio->torchOff(LedGpioController::Group::ALL);

    // Clear any strobe callback left by UVBF before disarming
    if (imx219Camera) imx219Camera->setStrobeCallback(nullptr);
    if (imx708Camera) imx708Camera->setStrobeCallback(nullptr);

    for (int i = 0; i < 32; ++i)
    {
        if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
            ledManager->turnOff(static_cast<LedMgr::LedId>(i));
    }

    // Clear the selected LED list
    {
        std::lock_guard<std::mutex> lk(ledCaptureMutex);
        selectedLeds.clear();
    }

    LOG_INFO << "led_all_off: complete" << std::endl;

    return R"({"type":"status","message":"led_all_off_complete"})";
}


std::string CommandHandler::handleLedStrobeSyncEnable(
    const std::map<std::string, std::string>& params)
{
    ParamExtractor p(params);
    bool   enable         = p.getBool("enabled", false);
    double leadTime_ms    = p.getFloat(Param::LED_STROBE_LEAD_TIME_MS, 2.0f);

    if (!strobeController)
        return buildJsonError("Strobe controller not available");

    if (!ledGpio)
        return buildJsonError("LED GPIO not available");

    if (enable && !streaming)
        return buildJsonError("Cannot enable strobe sync: no active stream");

    if (enable)
    {
        int64_t leadTime_us = static_cast<int64_t>(leadTime_ms * 1000.0);
        strobeController->setStrobeLeadTime_us(leadTime_us);

        if (!ledManager || !ledManager->isInitialized())
            return buildJsonError("LED manager not initialized");

        for (int i = 0; i < 32; ++i)
            if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
                ledManager->turnOff(static_cast<LedMgr::LedId>(i));
        for (const auto& sel : selectedLeds)
            if (!ledManager->setStrobeEnable(static_cast<LedMgr::LedId>(sel.ledId),
                                            LedMgr::LedHwStrobeMode::STROBE_MODE_LEVEL))
                return buildJsonError("Failed to arm strobe: " + ledManager->getLastError());

        strobeController->setRearmCallback([this]() {
            if (ledManager && ledManager->isInitialized())
        {
            for (int i = 0; i < 32; ++i)
                if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
                    ledManager->turnOff(static_cast<LedMgr::LedId>(i));
            for (const auto& sel : selectedLeds)
                ledManager->setStrobeEnable(static_cast<LedMgr::LedId>(sel.ledId),
                                            LedMgr::LedHwStrobeMode::STROBE_MODE_LEVEL);
        }
        });
    
        strobeController->setEnabled(true);
    }
    else
    {
        strobeController->setEnabled(false);
        if (ledManager && ledManager->isInitialized())
            for (int i = 0; i < 32; ++i)
            {
                if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
                    ledManager->turnOff(static_cast<LedMgr::LedId>(i));
            }
    }

    LOG_INFO << "VBlank strobe sync " << (enable ? "enabled" : "disabled")
             << (enable ? ", strobe lead time=" + std::to_string(static_cast<int64_t>(leadTime_ms * 1000.0)) + " µs" : "")
             << std::endl;

    std::ostringstream oss;
    oss << R"({"type":"status","message":"VBlank strobe sync )"
        << (enable ? "enabled" : "disabled") << R"(")"
        << R"(,"enabled":)" << (enable ? "true" : "false");
    if (enable)
        oss << R"(,"strobe_lead_time_us":)" << static_cast<int64_t>(leadTime_ms * 1000.0);
    oss << "}";
    return oss.str();
}

std::string CommandHandler::handleLedSetFlashDuration(
    const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    int ledId       = p.getInt("led_id", -1);
    int duration_ms = p.getInt("duration_ms", 100);

    if (ledId < 0 || ledId > 31)
        return buildJsonError("Invalid led_id (0-31)");

    LOG_INFO << "LED flash duration: id=" << ledId 
             << " duration=" << duration_ms << " ms" << std::endl;

    if (!ledManager || !ledManager->isInitialized())
        return buildJsonError("LED manager not initialized");

    try
    {
        if (!ledManager->setFlashTimeout(static_cast<LedMgr::LedId>(ledId),
                                           static_cast<uint16_t>(duration_ms)))
            return buildJsonError("Failed to set flash timeout for LED " + std::to_string(ledId));

        return buildJsonSuccess("Flash duration set to " + 
                                std::to_string(duration_ms) + " ms");
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Flash duration exception: ") + e.what());
    }
}

std::string CommandHandler::handleLedSetFlashTimeout(
    const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    int timeout_ms = p.getInt("timeout_ms", 400);

    LOG_INFO << "LED flash timeout: " << timeout_ms << " ms" << std::endl;

    if (!ledManager || !ledManager->isInitialized())
        return buildJsonError("LED manager not initialized");

    try
    {
        if (!ledManager->setFlashTimeout(LedMgr::LedId::LED0, static_cast<uint16_t>(timeout_ms)))
            return buildJsonError("Failed to set flash timeout");

        return buildJsonSuccess("Flash timeout set to " + std::to_string(timeout_ms) + " ms");
    }
    catch (const std::exception &e)
    {
        return buildJsonError(std::string("Flash timeout exception: ") + e.what());
    }
}

std::string CommandHandler::handleLedSetGpioMode(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    std::string mode = p.getString(Param::LED_GPIO_MODE, "off");
    int delay = p.getInt(Param::LED_PRE_FRAME_DELAY_MS, 0);
    bool postOff = p.getBool(Param::LED_POST_CAPTURE_OFF, false);

    // Validate
    if (delay < 0) delay = 0;
    if (delay > 500) delay = 500;

    if (mode == "torch")
        ledGpioMode = LedGpioMode::TORCH;
    else if (mode == "strobe")
        ledGpioMode = LedGpioMode::STROBE;
    else
        ledGpioMode = LedGpioMode::OFF;

    ledPreFrameDelay_ms = delay;
    ledPostCaptureOff = postOff;

    LOG_INFO << "LED GPIO mode: " << mode
             << ", pre-frame delay: " << delay << " ms"
             << ", post-capture off: " << (postOff ? "true" : "false") << std::endl;

    // If mode just set to OFF, ensure GPIOs are low
    if (ledGpioMode == LedGpioMode::OFF && ledGpio)
    {
        ledGpio->torchOff(LedGpioController::Group::ALL);
        ledGpio->strobeOff(LedGpioController::Group::ALL);
    }

    std::ostringstream oss;
    oss << R"({"type":"status","message":"LED GPIO mode set")"
        << R"(,"led_gpio_mode":")" << mode << R"(")"
        << R"(,"led_pre_frame_delay_ms":)" << delay
        << R"(,"led_post_capture_off":)" << (postOff ? "true" : "false")
        << "}";
    return oss.str();
}

std::string CommandHandler::handleLedSelect(const std::map<std::string, std::string>& params)
{
    ParamExtractor p(params);
    auto ids         = p.getIntList  (Param::LED_IDS);
    auto brightnesses = p.getUInt8List(Param::LED_BRIGHTNESSES);

    if (ids.empty())
        return buildJsonError("led_select requires led_ids array");

    if (ids.size() != brightnesses.size())
        return buildJsonError("led_ids and led_brightnesses must have the same length");

    for (int id : ids)
        if (id < 0 || id > 31)
            return buildJsonError("led_id out of range (0-31): " + std::to_string(id));
    {
        std::lock_guard<std::mutex> lk(ledCaptureMutex);
        selectedLeds.clear();
        selectedLeds.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i)
            selectedLeds.push_back({ids[i], brightnesses[i]});
    }

    LOG_INFO << "LED selection: " << selectedLeds.size() << " LED(s)" << std::endl;
    for (const auto& sel : selectedLeds)
        LOG_INFO << "  LED " << sel.ledId << " brightness=" << (int)sel.brightness << std::endl;

    std::ostringstream oss;
    oss << R"({"type":"status","message":"LED selection set","count":)" << selectedLeds.size();
    oss << R"(,"leds":[)";
    for (size_t i = 0; i < selectedLeds.size(); ++i)
    {
        if (i) oss << ",";
        oss << R"({"id":)" << selectedLeds[i].ledId
            << R"(,"brightness":)" << (int)selectedLeds[i].brightness << "}";
    }
    oss << "]}";
    return oss.str();
}

std::string CommandHandler::handleLedDeselect(const std::map<std::string, std::string>&)
{
    size_t prev = selectedLeds.size();
    selectedLeds.clear();
    LOG_INFO << "LED selection cleared (" << prev << " LED(s) deselected)" << std::endl;
    return buildJsonSuccess("LED selection cleared");
}

std::string CommandHandler::handleLedGetStatus(const std::map<std::string, std::string>&)
{
    if (!ledManager || !ledManager->isInitialized())
        return buildJsonError("LED manager not initialized");

    std::ostringstream oss;
    oss << R"({"type":"led_status","total":32,"available_ids":[)";

    bool first = true;
    int count = 0;
    for (int i = 0; i < 32; ++i)
    {
        if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
        {
            if (!first) oss << ",";
            oss << i;
            first = false;
            ++count;
        }
    }
    oss << R"(],"available_count":)" << count << "}";
    return oss.str();
}


void CommandHandler::prepareLedForCapture()
{
    // Step 1: configure I2C LED brightness for all selected LEDs
    if (!selectedLeds.empty() && ledManager && ledManager->isInitialized())
    {
        std::lock_guard<std::mutex> lk(ledCaptureMutex);
        for (const auto& sel : selectedLeds)
        {
            LOG_INFO << "LED select torch: id=" << sel.ledId
                     << " brightness=" << (int)sel.brightness << std::endl;
            ledManager->setFlashTorchBrightness(static_cast<LedMgr::LedId>(sel.ledId), sel.brightness);
            ledManager->setTorchMode(static_cast<LedMgr::LedId>(sel.ledId));
        }
    }

    // Step 2: assert GPIO torch/strobe lines (turns LEDs on)
    if (ledGpioMode == LedGpioMode::OFF || !ledGpio)
        return;

    if (ledGpioMode == LedGpioMode::TORCH)
    {
        LOG_INFO << "LED: Torch ON (all), pre-frame delay " << ledPreFrameDelay_ms << " ms"
                 << std::endl;
        ledGpio->torchOn(LedGpioController::Group::ALL);
    }
    else // STROBE
    {
        LOG_INFO << "LED: Strobe ON (all), pre-frame delay " << ledPreFrameDelay_ms << " ms"
                 << std::endl;
        ledGpio->strobeOn(LedGpioController::Group::ALL);
    }

    if (ledPreFrameDelay_ms > 0)
        usleep(ledPreFrameDelay_ms * 1000);
}

void CommandHandler::finishLedAfterCapture()
{
    // Step 1: de-assert GPIO lines
    if (ledGpio && ledGpioMode != LedGpioMode::OFF)
    {
        if (ledGpioMode == LedGpioMode::STROBE)
        {
            ledGpio->strobeOff(LedGpioController::Group::ALL);
            LOG_DEBUG << "LED: Strobe OFF (all)" << std::endl;
        }
        else if (ledGpioMode == LedGpioMode::TORCH && ledPostCaptureOff)
        {
            ledGpio->torchOff(LedGpioController::Group::ALL);
            LOG_DEBUG << "LED: Torch OFF (all) post-capture" << std::endl;
        }
    }

    // Step 2: turn off selected I2C LEDs
    if (!selectedLeds.empty() && ledManager && ledManager->isInitialized())
    {
        std::lock_guard<std::mutex> lk(ledCaptureMutex);
        for (const auto& sel : selectedLeds)
            ledManager->turnOff(static_cast<LedMgr::LedId>(sel.ledId));
    }
}

std::string CommandHandler::handleUVBFVBlankCapture(
    const std::map<std::string, std::string>& params)
{
    if (uvbfActive)
        return buildJsonError("UVBF capture already in progress");
    if (streaming || dualStreaming)
        return buildJsonError("Cannot start UVBF VBlank capture while streaming");
    if (intervalShooting)
        return buildJsonError("Cannot start UVBF VBlank capture while interval still active");
 
    ParamExtractor p(params);
     std::string camera     = p.getString(sanuwave::protocol::UVBFParam::CAMERA,      Camera::IMX219);
    int         exposure_us = p.getInt  (sanuwave::protocol::UVBFParam::EXPOSURE_US, 20000);
    float       analogGain = p.getFloat (sanuwave::protocol::UVBFParam::ANALOG_GAIN, 1.0f);
    std::string sessionId  = p.getString(sanuwave::protocol::Param::SESSION_ID,      "");
    int         width      = p.getInt   (sanuwave::protocol::Param::WIDTH,            1640);
    int         height     = p.getInt   (sanuwave::protocol::Param::HEIGHT,           1232);
    bool        predictVBlank = p.getBool("predict_vblank", false);
    bool        kernelStrobe  = p.getBool("kernel_strobe", false);
    bool        motionCheck   = p.getBool(sanuwave::protocol::Param::UVBF_MOTION_CHECK, false);

 
    CameraBase* targetCamera = nullptr;
    if      (camera == Camera::IMX219) targetCamera = imx219Camera;
    else if (camera == Camera::IMX708) targetCamera = imx708Camera;
    else
        return buildJsonError("UVBF VBlank: unknown camera: " + camera);
 
    if (!targetCamera)
        return buildJsonError("UVBF VBlank: camera not available: " + camera);
    if (!ledManager || !ledManager->isInitialized())
        return buildJsonError("UVBF VBlank: LED manager not initialized");
    if (!ledGpio)
        return buildJsonError("UVBF VBlank: LED GPIO not available");
    if (selectedLeds.empty())
        return buildJsonError("UVBF VBlank: no LEDs selected — send LED_SELECT first");
    if (analogGain < 1.0f)
        return buildJsonError("UVBF VBlank: analog_gain must be >= 1.0");
    if (exposure_us < 100)
        return buildJsonError("UVBF VBlank: exposure_us must be >= 100");
 
    auto timingOpt = targetCamera->getSensorTiming();
    if (!timingOpt || !timingOpt->valid || timingOpt->rollingShutter_us <= 0.0)
        return buildJsonError("UVBF VBlank: no valid sensor timing");
 
    const int64_t rollingShutter_us = static_cast<int64_t>(
        timingOpt->rollingShutter_us);
    // Frame period = rolling shutter + VBlank gap.  At steady state the IPA
    // holds VBlank at vblankMin, so the gap is vblank * lineTime_us.
    const int64_t vblankGap_us = static_cast<int64_t>(
        timingOpt->vblank * timingOpt->lineTime_us);
    const int64_t framePeriod_us = rollingShutter_us + vblankGap_us;
    // Use the hardware maximum (400 ms) so the timeout doesn't expire mid-burst.
    const uint16_t flashTimeout_ms = 400;

    LOG_INFO << "UVBF VBlank: exposure=" << exposure_us
             << " us  analog_gain=" << analogGain
             << "  rolling_shutter=" << timingOpt->rollingShutter_us
             << " us  vblank_gap=" << vblankGap_us
             << " us  frame_period=" << framePeriod_us
             << " us  flash_timeout=" << flashTimeout_ms << " ms" << std::endl;
 
    // Snapshot selected LEDs
    std::vector<LedSelection> ledSnapshot;
    {
        std::lock_guard<std::mutex> lk(ledCaptureMutex);
        ledSnapshot = selectedLeds;
    }
 
    // Arm LEDs in hardware strobe level mode
    ledGpio->strobeOff(LedGpioController::Group::ALL);
 
    for (int i = 0; i < 32; ++i)
        if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
            ledManager->turnOff(static_cast<LedMgr::LedId>(i));
 
    bool armOk = true;
    for (const auto& sel : ledSnapshot)
    {
        auto id = static_cast<LedMgr::LedId>(sel.ledId);
        if (!ledManager->isLedEnabled(id)) continue;
 
        if (!ledManager->setFlashTorchBrightness(id, sel.brightness))
        {
            LOG_ERROR << "UVBF VBlank: setFlashTorchBrightness failed LED="
                      << sel.ledId << std::endl;
            armOk = false;
        }
        if (!ledManager->setFlashTimeout(id, flashTimeout_ms))
        {
            LOG_ERROR << "UVBF VBlank: setFlashTimeout failed LED="
                      << sel.ledId << std::endl;
            armOk = false;
        }
        if (!ledManager->setStrobeEnable(
                id, LedMgr::LedHwStrobeMode::STROBE_MODE_LEVEL))
        {
            LOG_ERROR << "UVBF VBlank: setStrobeEnable failed LED="
                      << sel.ledId << std::endl;
            armOk = false;
        }
    }
    if (!armOk)
        return buildJsonError("UVBF VBlank: LED arm failed — check log");

    // ── Kernel strobe setup ────────────────────────────────────────────
    // Build the frame sequence string: "01110" for 5 frames (dark, illum, illum, illum, dark)
    const int maxFrames = 5;
    std::string frameSequence;
    {
        // Pattern must match captureVBlankBurst's alternating sequence:
        // dark, illum, dark, illum, ..., dark
        // Even indices (0, 2, 4) = dark, odd indices (1, 3) = illuminated
        for (int i = 0; i < maxFrames; i++)
            frameSequence += (i % 2 == 1) ? '1' : '0';
    }

    // In kernel strobe mode, the ISR starts counting frames from when
    // strobe_active is written — but captureVBlankBurst runs warmup
    // frames first (1 flush + 5 IPA warmup = 6 frames) before the
    // real burst.  Prepend zeros so the ISR skips the warmup frames.
    if (kernelStrobe) {
        const int warmupFrames = 6;  // 1 flush + 5 captureVBlankWarmup
        std::string skipPrefix(warmupFrames, '0');
        frameSequence = skipPrefix + frameSequence;
        LOG_INFO << "UVBF VBlank: kernel strobe sequence with "
                 << warmupFrames << " warmup skip: " << frameSequence << std::endl;
    }

    // Flash timeout validation (worst-case: timeout does NOT reset on de-assertion)
    if (kernelStrobe) {
        int firstIllum = -1, lastIllum = -1;
        for (int i = 0; i < (int)frameSequence.size(); i++) {
            if (frameSequence[i] == '1') {
                if (firstIllum < 0) firstIllum = i;
                lastIllum = i;
            }
        }
        if (firstIllum >= 0) {
            int spanFrames = lastIllum - firstIllum + 1;
            int64_t spanDuration_ms = (spanFrames * framePeriod_us) / 1000;
            if (spanDuration_ms >= flashTimeout_ms - 50) {
                return buildJsonError("UVBF: frame sequence would exceed LM3643 flash timeout ("
                    + std::to_string(flashTimeout_ms) + " ms). Illuminated span = "
                    + std::to_string(spanDuration_ms) + " ms ("
                    + std::to_string(spanFrames) + " frames)");
            }
        }
    }

    std::string strobeSysfsPath;
    if (kernelStrobe) {
        // Use the per-camera path resolved at init time rather than a
        // first-match udev glob. On Pi 5 each CSI port has its own CFE
        // instance with its own sanuwave_strobe sysfs; writing to the
        // wrong one is a silent no-op.
        strobeSysfsPath = targetCamera->getKernelStrobeSysfsPath();
        if (strobeSysfsPath.empty())
            return buildJsonError("UVBF: kernel_strobe enabled but this camera "
                                  "has no resolved sanuwave_strobe sysfs path. "
                                  "Is the patched rp1-cfe module loaded on the "
                                  "correct CFE?");

        LOG_INFO << "UVBF VBlank: kernel strobe mode — sysfs at "
                 << strobeSysfsPath << std::endl;

        // Release strobe GPIOs from userspace
        ledGpio->releaseStrobe(LedGpioController::Group::ALL);

        // Configure kernel module
        std::string gpioList = ledGpio->getStrobeGpioPins(LedGpioController::Group::ALL);
        if (!writeSysfs(strobeSysfsPath + "/strobe_gpio", gpioList) ||
            !writeSysfs(strobeSysfsPath + "/strobe_sequence", frameSequence) ||
            !writeSysfs(strobeSysfsPath + "/strobe_active", "1")) {
            // Failed — reclaim GPIOs and bail
            writeSysfs(strobeSysfsPath + "/strobe_active", "0");
            writeSysfs(strobeSysfsPath + "/strobe_gpio", "-1");
            ledGpio->reclaimStrobe(LedGpioController::Group::ALL);
            return buildJsonError("UVBF: failed to configure kernel strobe via sysfs");
        }

        LOG_INFO << "UVBF VBlank: kernel strobe armed — GPIOs=" << gpioList
                 << " sequence=" << frameSequence << std::endl;
    }

    uvbfActive = true;
    if (uvbfWorker.joinable())
        uvbfWorker.join();
 
    uvbfWorker = std::thread([this, targetCamera, camera,
                               exposure_us, analogGain, sessionId,
                               width, height, flashTimeout_ms,
                               framePeriod_us, predictVBlank,
                               kernelStrobe, strobeSysfsPath,
                               frameSequence, maxFrames, motionCheck]()
    {
        const std::string bayerPattern = "BGGR";
        const int         blackLevel   = 4096;
        const int         sensorBits   = 10;
 
        auto disarmLeds = [&]()
        {
            if (kernelStrobe) {
                // Deactivate kernel strobe and reclaim GPIOs
                writeSysfs(strobeSysfsPath + "/strobe_active", "0");
                writeSysfs(strobeSysfsPath + "/strobe_gpio", "-1");
                ledGpio->reclaimStrobe(LedGpioController::Group::ALL);
                LOG_INFO << "UVBF VBlank: kernel strobe disarmed, GPIOs reclaimed" << std::endl;
            } else {
                ledGpio->strobeOff(LedGpioController::Group::ALL);
            }
            for (int i = 0; i < 32; ++i)
                if (ledManager->isLedEnabled(static_cast<LedMgr::LedId>(i)))
                    ledManager->turnOff(static_cast<LedMgr::LedId>(i));
        };
 
        // ── Strobe event logging ───────────────────────────────────────
        struct StrobeEvent
        {
            bool    on;
            int64_t elapsed_ms;
            bool    predicted;     // true if this was a predicted VBlank off
        };
        std::vector<StrobeEvent> strobeLog;
        std::mutex strobeLogMutex;

        auto monoNow_ns = []() -> int64_t {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
        };

        const int64_t armTime_ns = monoNow_ns();

        // Track whether the LED is currently on so the predicted-off
        // thread and the normal callback don't fight.
        std::atomic<bool> ledCurrentlyOn{false};

        // In predict mode, a single timer thread drives all GPIO toggling
        // for the entire burst.  This flag prevents subsequent strobeToggle
        // calls from spawning additional threads.
        std::atomic<bool> predictTimerRunning{false};

        // Number of illuminated frames in the alternating sequence.
        // Burst pattern: dark, illum, dark, illum, ..., dark
        const int illuminatedCount = (maxFrames - 1) / 2;

        auto strobeToggle = [&](bool on)
        {
            const int64_t elapsed_ms =
                (monoNow_ns() - armTime_ns) / 1'000'000;

            if (on)
            {
                if (predictVBlank && predictTimerRunning)
                {
                    // Timer thread is driving GPIO — this callback-driven
                    // strobeOn is a no-op.
                    LOG_INFO << "UVBF VBlank: strobeOn skipped "
                             << "(predict timer driving)" << std::endl;
                    return;
                }

                ledGpio->strobeOn(LedGpioController::Group::ALL);
                ledCurrentlyOn = true;

                {
                    std::lock_guard<std::mutex> lk(strobeLogMutex);
                    strobeLog.push_back({true, elapsed_ms, false});
                }

                LOG_INFO << "UVBF VBlank: strobeOn  elapsed_since_arm="
                         << elapsed_ms << " ms" << std::endl;

                // If predict_vblank is enabled and this is the first
                // strobeOn, spawn a single timer thread that drives the
                // full on/off/on/off cycle for all illuminated frames.
                // Phase-locked to this callback, each sleep of framePeriod_us
                // lands at the same offset in subsequent VBlank gaps.
                if (predictVBlank && !predictTimerRunning.exchange(true))
                {
                    std::thread([&]()
                    {
                        // First strobeOn already happened above.
                        // Now alternate: off, on, off, on, ... off
                        // Total transitions = 2 * illuminatedCount - 1
                        // (first on already done, ends with final off)
                        for (int pulse = 0; pulse < illuminatedCount; ++pulse)
                        {
                            // ── strobeOff: end of illuminated frame ──
                            usleep(static_cast<useconds_t>(framePeriod_us));

                            if (ledCurrentlyOn.exchange(false))
                            {
                                ledGpio->strobeOff(LedGpioController::Group::ALL);
                                const int64_t offElapsed =
                                    (monoNow_ns() - armTime_ns) / 1'000'000;
                                {
                                    std::lock_guard<std::mutex> lk(strobeLogMutex);
                                    strobeLog.push_back({false, offElapsed, true});
                                }
                                LOG_INFO << "UVBF VBlank: strobeOff (predicted) "
                                         << "elapsed_since_arm=" << offElapsed
                                         << " ms" << std::endl;
                            }

                            // ── strobeOn: start of next illuminated frame ──
                            // (skip for the last pulse — burst ends with dark)
                            if (pulse + 1 < illuminatedCount)
                            {
                                usleep(static_cast<useconds_t>(framePeriod_us));

                                ledGpio->strobeOn(LedGpioController::Group::ALL);
                                ledCurrentlyOn = true;

                                const int64_t onElapsed =
                                    (monoNow_ns() - armTime_ns) / 1'000'000;
                                {
                                    std::lock_guard<std::mutex> lk(strobeLogMutex);
                                    strobeLog.push_back({true, onElapsed, true});
                                }
                                LOG_INFO << "UVBF VBlank: strobeOn  (predicted) "
                                         << "elapsed_since_arm=" << onElapsed
                                         << " ms" << std::endl;
                            }
                        }
                    }).detach();
                }
            }
            else
            {
                // Normal callback-driven strobeOff.
                // In predict mode this may be a no-op if the predicted
                // thread already turned it off.
                if (ledCurrentlyOn.exchange(false))
                {
                    ledGpio->strobeOff(LedGpioController::Group::ALL);

                    const int64_t offElapsed =
                        (monoNow_ns() - armTime_ns) / 1'000'000;
                    {
                        std::lock_guard<std::mutex> lk(strobeLogMutex);
                        strobeLog.push_back({false, offElapsed, false});
                    }
                    LOG_INFO << "UVBF VBlank: strobeOff elapsed_since_arm="
                             << offElapsed << " ms" << std::endl;
                }
                else if (predictVBlank)
                {
                    LOG_INFO << "UVBF VBlank: strobeOff skipped "
                             << "(predicted already off)" << std::endl;
                }
            }
        };
 
        CaptureSettings burstSettings;
        burstSettings.autoExposure    = false;
        burstSettings.exposureTime_us = exposure_us;
        burstSettings.autoAnalogGain  = false;
        burstSettings.analogGain      = analogGain;
        burstSettings.width           = width;
        burstSettings.height          = height;

        // ── Run the burst ──────────────────────────────────────────────────
        // captureVBlankBurst owns the full begin/capture/end lifecycle.
        // In kernel_strobe mode, the kernel ISR drives the GPIOs — no
        // userspace strobe callback needed.
        VBlankBurstResult result = kernelStrobe
            ? targetCamera->captureVBlankBurst(burstSettings, [](bool){}, maxFrames)
            : targetCamera->captureVBlankBurst(burstSettings, strobeToggle, maxFrames);

        // If predict_vblank is active (userspace mode only), a detached timer
        // thread drives the full on/off cycle. Safety sleep covers edge cases.
        if (predictVBlank && !kernelStrobe)
            usleep(static_cast<useconds_t>(framePeriod_us + 5000));

        disarmLeds();
 
        if (!result.success)
        {
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"vblank_burst","reason":")"
                       + result.error + R"("})");
            uvbfActive = false;
            return;
        }

        // Count only frames that will actually be transferred so the client's
        // progress bars reach 100% even if individual frames were dropped.
        int transferableCount = 0;
        for (const VBlankFrameResult& fr : result.frames)
            if (fr.success && !fr.image.empty()) ++transferableCount;

        if (transferableCount < 3)
        {
            uvbfNotify(R"({"type":")" + std::string(protocol::ResponseType::UVBF_ERROR)
                       + R"(","stage":"vblank_burst","reason":"insufficient valid frames: )"
                       + std::to_string(transferableCount) + R"("})");
            uvbfActive = false;
            return;
        }

        // ── Inter-frame motion measurement on illuminated frames ──────────────
        // Runs AFTER disarmLeds() — zero impact on the 400 ms LED-flash-
        // timeout budget. Cost is ~3 ms per phase-correlation pair on Pi 5;
        // even at the 9-illum maximum the total is well under 30 ms,
        // negligible against the multi-second wire transfer that follows.
        //
        // The N-aware UvbfBurstMotion tracker handles any illum count.
        // We feed it frames in burst order, filtered by fr.ledsOn (true ==
        // illuminated), and record each role's 1-based illum-sequence index
        // so the send loop below can pull motion results per frame.
        //
        // Skipped entirely when motionCheck is false (the protocol opt-in).
        UvbfBurstMotion motionTracker;
        std::map<std::string, int> roleToIllumIndex;
        if (motionCheck)
        {
            int kIllum = 0;
            for (const VBlankFrameResult& fr : result.frames)
            {
                if (!fr.success || fr.image.empty()) continue;
                if (!fr.ledsOn) continue;
                ++kIllum;
                roleToIllumIndex[fr.role] = kIllum;
                motionTracker.addFrame(fr.image);
            }
            motionTracker.logSummary();
        }
        else
        {
            LOG_INFO << "UVBF VBlank motion: check disabled by client (skipping)"
                     << std::endl;
        }

        // Build roles list from the frames that will be transferred
        std::ostringstream rolesJson;
        rolesJson << "[";
        bool firstRole = true;
        for (const VBlankFrameResult& fr : result.frames)
        {
            if (!fr.success || fr.image.empty()) continue;
            if (!firstRole) rolesJson << ",";
            rolesJson << R"(")" << fr.role << R"(")";
            firstRole = false;
        }
        rolesJson << "]";

        // Notify client — actual count and roles, no prediction needed
        uvbfNotify(
            R"({"type":")" + std::string(protocol::ResponseType::UVBF_STARTED)
            + R"(","session_id":")" + sessionId           + R"(")"
            + R"(,"frame_count":)"  + std::to_string(transferableCount)
            + R"(,"roles":)"        + rolesJson.str()
            + "}");

        LOG_INFO << "UVBF VBlank: frame_count=" << transferableCount
                 << " roles=" << rolesJson.str() << std::endl;

        // ── Package and transfer all frames ────────────────────────────────
        for (const VBlankFrameResult& fr : result.frames)
        {
            if (!fr.success || fr.image.empty())
            {
                LOG_WARNING << "UVBF VBlank: frame " << fr.role
                            << " missing image — skipping" << std::endl;
                continue;
            }
 
            const int storageBits = (fr.image.depth() == CV_16U) ? 16 : 8;
            const std::string rawHeader = "RAW|"
                + std::to_string(fr.image.cols) + "|"
                + std::to_string(fr.image.rows) + "|"
                + std::to_string(sensorBits)    + "|"
                + std::to_string(storageBits)   + "|"
                + std::to_string(blackLevel)    + "|"
                + bayerPattern                  + "|";
 
            const size_t dataSize = fr.image.total() * fr.image.elemSize();
            std::vector<uint8_t> payload;
            payload.reserve(rawHeader.size() + dataSize);
            payload.insert(payload.end(), rawHeader.begin(), rawHeader.end());
            payload.insert(payload.end(),
                           fr.image.data,
                           fr.image.data + dataSize);
 
            const int64_t callbackDelta_us =
                fr.sensorTimestamp_ns > 0
                ? (fr.callbackTimestamp_ns - fr.sensorTimestamp_ns) / 1000
                : -1;
 
            std::ostringstream frameHeader;
            frameHeader
                << R"({"type":")"            << protocol::ResponseType::FRAME_TRANSFER << R"(")"
                << R"(,"frame_role":")"       << fr.role                                << R"(")"
                << R"(,"session_id":")"       << sessionId                              << R"(")"
                << R"(,"camera":")"           << camera                                 << R"(")"
                << R"(,"payload_size":)"      << payload.size()
                << R"(,"bayer_pattern":")"    << bayerPattern                           << R"(")"
                << R"(,"bit_depth":)"         << sensorBits
                << R"(,"black_level":)"       << blackLevel
                << R"(,"width":)"             << fr.image.cols
                << R"(,"height":)"            << fr.image.rows
                << R"(,"exposure_us":)"       << fr.exposureTime_us
                << R"(,"leds_on":)"            << (fr.ledsOn ? "true" : "false")
                << R"(,"sensor_ts_ns":")"     << fr.sensorTimestamp_ns    << R"(")"
                << R"(,"callback_ts_ns":")"   << fr.callbackTimestamp_ns  << R"(")"
                << R"(,"callback_delta_us":)" << callbackDelta_us
                << R"(,"frame_dur_us":)"      << fr.frameDuration_us;

            // Optional motion sub-object. Only emitted when motionCheck was
            // requested AND this frame is an illuminated frame at illum-
            // sequence index k >= 2 AND both prev and anchor measurements
            // are valid and finite. Requiring both pairs to be valid keeps
            // the schema simple for the client (one check covers both
            // measurements); partial validity falls back to "no measurement"
            // for that frame.
            if (motionCheck && fr.ledsOn)
            {
                auto it = roleToIllumIndex.find(fr.role);
                if (it != roleToIllumIndex.end() && it->second >= 2)
                {
                    const UvbfFrameMotion fm = motionTracker.getMotion(it->second);
                    namespace UM = sanuwave::protocol::UvbfMotionField;
                    const bool emit = fm.prev.valid && fm.anchor.valid &&
                                      std::isfinite(fm.prev.trans_px)    &&
                                      std::isfinite(fm.prev.confidence)  &&
                                      std::isfinite(fm.anchor.trans_px)  &&
                                      std::isfinite(fm.anchor.confidence);
                    if (emit)
                    {
                        frameHeader
                            << R"(,")" << UM::OBJECT << R"(":{)"
                            << R"(")" << UM::VALID             << R"(":true,)"
                            << R"(")" << UM::PREV_TRANS_PX     << R"(":)"
                                      << std::fixed << std::setprecision(4)
                                      << fm.prev.trans_px     << ","
                            << R"(")" << UM::PREV_CONFIDENCE   << R"(":)"
                                      << fm.prev.confidence   << ","
                            << R"(")" << UM::ANCHOR_TRANS_PX   << R"(":)"
                                      << fm.anchor.trans_px   << ","
                            << R"(")" << UM::ANCHOR_CONFIDENCE << R"(":)"
                                      << fm.anchor.confidence
                            << "}";
                    }
                }
            }

            frameHeader << "}";

            uvbfNotify(
                R"({"type":")" + std::string(protocol::ResponseType::UVBF_FRAME_CAPTURED)
                + R"(","frame_role":")" + fr.role + R"("})");

            if (sendDngCallback)
                sendDngCallback(frameHeader.str(), payload.data(), payload.size());
        }
 
        // ── Timing summary to client ───────────────────────────────────────
        auto timingOpt2        = targetCamera->getSensorTiming();
        int64_t rollingShutter_us = 0;
        int64_t vblankEstimate_us = 0;
 
        if (timingOpt2 && timingOpt2->valid)
        {
            rollingShutter_us = static_cast<int64_t>(timingOpt2->rollingShutter_us);

            // Compute vblank_estimate_us as the median of consecutive inter-frame
            // deltas minus rolling shutter.  Using only frames[0]→[1] is wrong
            // when the IPA hasn't settled yet — those early frames run at ~2×
            // the steady-state period and would inflate the estimate.
            std::vector<int64_t> gaps;
            for (int i = 1; i < static_cast<int>(result.frames.size()); ++i)
            {
                const auto& prev = result.frames[i - 1];
                const auto& cur  = result.frames[i];
                if (prev.sensorTimestamp_ns > 0 && cur.sensorTimestamp_ns > 0)
                {
                    const int64_t period_us =
                        (cur.sensorTimestamp_ns - prev.sensorTimestamp_ns) / 1000;
                    const int64_t gap_us = period_us - rollingShutter_us;
                    if (gap_us > 0)
                        gaps.push_back(gap_us);
                }
            }
            if (!gaps.empty())
            {
                std::sort(gaps.begin(), gaps.end());
                vblankEstimate_us = gaps[gaps.size() / 2];
            }
        }
 
        std::ostringstream summary;
        summary << R"({"type":"uvbf_vblank_complete")"
                << R"(,"session_id":")"        << sessionId              << R"(")"
                << R"(,"frame_count":)"         << transferableCount
                << R"(,"commanded_exposure_us":)" << exposure_us
                << R"(,"commanded_analog_gain":)" << analogGain
                << R"(,"rolling_shutter_us":)"  << rollingShutter_us
                << R"(,"vblank_estimate_us":)"  << vblankEstimate_us
                << R"(,"frames":[)";
 
        for (int i = 0; i < static_cast<int>(result.frames.size()); i++)
        {
            const VBlankFrameResult& fr = result.frames[i];
            if (i) summary << ",";
 
            const int64_t callbackDelta_us =
                fr.sensorTimestamp_ns > 0
                ? (fr.callbackTimestamp_ns - fr.sensorTimestamp_ns) / 1000
                : -1;
 
            int64_t framePeriod_us = 0;
            if (i > 0
                && result.frames[i-1].sensorTimestamp_ns > 0
                && fr.sensorTimestamp_ns > 0)
            {
                framePeriod_us = (fr.sensorTimestamp_ns
                                  - result.frames[i-1].sensorTimestamp_ns) / 1000;
            }
 
            summary
                << R"({"role":")"             << fr.role                          << R"(")"
                << R"(,"leds_on":)"           << (fr.ledsOn ? "true" : "false")
                << R"(,"sensor_ts_ns":")"      << fr.sensorTimestamp_ns            << R"(")"
                << R"(,"callback_delta_us":)"  << callbackDelta_us
                // frame_dur_us: use measured inter-frame delta (consecutive sensor
                // timestamps) when available; fall back to libcamera FrameDuration.
                // Both measure the same thing — the inter-frame period in µs — but
                // the measured value is more accurate for the first frame where
                // libcamera may report 0 before the IPA settles.
                << R"(,"frame_dur_us":)"       << (framePeriod_us > 0
                                                   ? framePeriod_us
                                                   : fr.frameDuration_us)
                << "}";
        }
        summary << "]";

        // ── Strobe diagnostics ──────────────────────────────────────────
        {
            std::lock_guard<std::mutex> lk(strobeLogMutex);

            int64_t lastStrobeOn_ms = 0;
            for (const auto& ev : strobeLog)
                if (ev.on) lastStrobeOn_ms = ev.elapsed_ms;

            const bool timeoutExceeded = (!kernelStrobe && lastStrobeOn_ms >= flashTimeout_ms);

            summary << R"(,"flash_timeout_ms":)"              << flashTimeout_ms
                    << R"(,"arm_elapsed_at_last_strobe_ms":)" << lastStrobeOn_ms
                    << R"(,"timeout_exceeded":)"
                        << (timeoutExceeded ? "true" : "false")
                    << R"(,"predict_vblank":)"
                        << (predictVBlank ? "true" : "false")
                    << R"(,"kernel_strobe":)"
                        << (kernelStrobe ? "true" : "false")
                    << R"(,"strobe_mode":)"
                        << (kernelStrobe ? R"("kernel_isr")" : (predictVBlank ? R"("predict_vblank")" : R"("callback")"))
                    << R"(,"strobe_events":[)";

            for (size_t i = 0; i < strobeLog.size(); ++i)
            {
                if (i) summary << ",";
                summary << R"({"on":)"
                        << (strobeLog[i].on ? "true" : "false")
                        << R"(,"elapsed_ms":)" << strobeLog[i].elapsed_ms
                        << R"(,"predicted":)"
                        << (strobeLog[i].predicted ? "true" : "false")
                        << "}";
            }
            summary << "]";
        }
        summary << "}";
        LOG_INFO << "TIMINGDATA vblank_estimate_us=" << vblankEstimate_us
                 << " (median of " << (result.frames.size() > 1
                                       ? result.frames.size() - 1 : 0)
                 << " inter-frame gaps)"
                 << " rolling_shutter_us=" << rollingShutter_us
                 << " kernel_strobe=" << (kernelStrobe ? "true" : "false")
                 << std::endl;
 
        uvbfNotify(summary.str());
 
        LOG_INFO << "UVBF VBlank: experiment complete" << std::endl;
        uvbfActive = false;
    });
 
    return R"({"type":"status","message":"UVBF VBlank capture started","session_id":")"
           + sessionId + R"("})";
}

std::string CommandHandler::handleSetParam(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    std::string camera = p.getString(Param::CAMERA);
    std::string param = p.getString(Param::PARAM);
    std::string value = p.getString(Param::VALUE);
    std::string mode = p.getString(Param::MODE, ParamMode::CAPTURE);

    if (!multiParams->setParameter(camera, param, value, mode))
        return buildJsonError("Failed to set parameter");

    // Apply to active RGB stream
    if (streaming && streamModality == Camera::IMX708 && camera == Camera::IMX708 &&
        mode == ParamMode::STREAMING && imx708Camera)
    {
        CaptureSettings s = multiParams->getRgbStreamingSettings();
        s.width = streamWidth;
        s.height = streamHeight;
        imx708Camera->setPendingStreamSettings(s);
    }
    // Apply to active Arducam stream
    if (streaming && streamModality == Camera::IMX219 && camera == Camera::IMX219 &&
        mode == ParamMode::STREAMING && imx219Camera)
    {
        CaptureSettings s = multiParams->getArducamStreamingSettings();
        s.width = streamWidth;
        s.height = streamHeight;
        imx219Camera->setPendingStreamSettings(s);
    }
    // Apply to active Thermal stream
    if (streaming && streamModality == Camera::THERMAL && camera == Camera::THERMAL &&
        mode == ParamMode::STREAMING && thermalCamera)
        multiParams->applyThermalSettings(thermalCamera, ParamMode::STREAMING);
    // Apply to depth sensor
    if (camera == Camera::DEPTH && distanceSensor && distanceSensor->isInitialized())
        multiParams->applyDepthSettings(distanceSensor);

    std::ostringstream oss;
    oss << R"({"type":"status","message":"Parameter set")"
        << R"(,"camera":")" << camera << R"(","param":")" << param << R"(","value":")" << value
        << R"(","mode":")" << mode << R"("})";
    return oss.str();
}

std::string CommandHandler::handleGetParams(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    std::string camera = p.getString(Param::CAMERA);
    std::string mode = p.getString(Param::MODE, ParamMode::CAPTURE);

    std::string json = multiParams->getParameters(camera, mode);
    if (json == "{}")
        return buildJsonError("Unknown camera or mode: " + camera + " (" + mode + ")");

    std::ostringstream oss;
    oss << R"({"type":"params","camera":")" << camera << R"(","mode":")" << mode << R"(","params":)"
        << json << "}";
    return oss.str();
}

std::string CommandHandler::handleGetAllParams(const std::map<std::string, std::string> &)
{
    std::ostringstream oss;
    oss << R"({"type":"all_params","cameras":)" << multiParams->getAllParameters() << "}";
    return oss.str();
}

std::string CommandHandler::handleResetSettings(const std::map<std::string, std::string> &params)
{
    ParamExtractor p(params);
    std::string camera = p.getString(Param::CAMERA, Camera::IMX708);
    std::string mode = p.getString(Param::MODE, ParamMode::CAPTURE);

    LOG_INFO << "Resetting " << camera << " (" << mode << ") to defaults" << std::endl;
    multiParams->resetSettings(camera, mode);

    // Apply to active streams
    if (streaming && streamModality == Camera::IMX708 && camera == Camera::IMX708 &&
        mode == ParamMode::STREAMING && imx708Camera)
    {
        CaptureSettings s = multiParams->getRgbStreamingSettings();
        s.width = streamWidth;
        s.height = streamHeight;
        imx708Camera->setPendingStreamSettings(s);
    }
    if (streaming && streamModality == Camera::IMX219 && camera == Camera::IMX219 &&
        mode == ParamMode::STREAMING && imx219Camera)
    {
        CaptureSettings s = multiParams->getArducamStreamingSettings();
        s.width = streamWidth;
        s.height = streamHeight;
        imx219Camera->setPendingStreamSettings(s);
    }
    if (streaming && streamModality == Camera::THERMAL && camera == Camera::THERMAL &&
        mode == ParamMode::STREAMING && thermalCamera)
        multiParams->applyThermalSettings(thermalCamera, ParamMode::STREAMING);
    if (camera == Camera::DEPTH && distanceSensor && distanceSensor->isInitialized())
        multiParams->applyDepthSettings(distanceSensor);

    std::ostringstream oss;
    oss << R"({"type":"success","message":"Settings reset")"
        << R"(,"camera":")" << camera << R"(","mode":")" << mode << R"("})";
    return oss.str();
}


std::string CommandHandler::handleGetStatus(const std::map<std::string, std::string> &)
{
    std::ostringstream oss;
    oss << R"({"type":"status")"
        << R"(,")" << sanuwave::protocol::Device::IMX708_CAMERA  << R"(":)" << (imx708Camera ? "true" : "false")
        << R"(,")" << sanuwave::protocol::Device::IMX219_CAMERA  << R"(":)" << (imx219Camera ? "true" : "false")
        << R"(,"thermal_camera":)"
        << (thermalCamera && thermalCamera->isReady() ? "true" : "false")
        << R"(,"distance_sensor":)"
        << (distanceSensor && distanceSensor->isInitialized() ? "true" : "false")
        << R"(,"uv_sensor":)" << (uvSensor && uvSensor->isInitialized() ? "true" : "false")
        << R"(,"als_sensor":)" << (alsSensor && alsSensor->isInitialized() ? "true" : "false")
        << R"(,"led_manager":)" << (ledManager && ledManager->isInitialized() ? "true" : "false")
        << R"(,"led_gpio_mode":")" << (ledGpioMode == LedGpioMode::TORCH ? "torch" :
                                  ledGpioMode == LedGpioMode::STROBE ? "strobe" : "off") << R"(")"
        << R"(,"led_pre_frame_delay_ms":)" << ledPreFrameDelay_ms
        << R"(,"streaming":)" << (streaming ? "true" : "false");
    if (streaming)
        oss << R"(,"stream_modality":")" << streamModality << R"(","stream_mode":"max_throughput")";
    oss << R"(,"distance_ranging":)" << (distanceRanging ? "true" : "false");

    if (imx708Camera)
    {
        auto timing = imx708Camera->getSensorTiming();
        if (timing && timing->valid)
        {
            oss << R"(,"imx708_timing":{)"
                << R"("hblank":)" << timing->hblank << R"(,"vblank":)" << timing->vblank
                << R"(,"pixel_rate":)" << timing->pixelRate << R"(,"line_time_us":)"
                << timing->lineTime_us << R"(,"frame_time_us":)" << timing->frameTime_us
                << R"(,"rolling_shutter_us":)" << timing->rollingShutter_us << R"(,"active_width":)"
                << timing->activeWidth << R"(,"active_height":)" << timing->activeHeight << "}";
        }
        auto lensRange = imx708Camera->getLensPositionRange();
        if (lensRange.valid)
        {
            oss << R"(,")" << sanuwave::protocol::LensRangeField::IMX708_MIN << R"(":)" << lensRange.min
                << R"(,")" << sanuwave::protocol::LensRangeField::IMX708_MAX << R"(":)" << lensRange.max;
        }
    }

    if (imx219Camera)
    {
        auto timing = imx219Camera->getSensorTiming();
        if (timing && timing->valid)
        {
            oss << R"(,"imx219_timing":{)"
                << R"("hblank":)" << timing->hblank << R"(,"vblank":)" << timing->vblank
                << R"(,"pixel_rate":)" << timing->pixelRate << R"(,"line_time_us":)"
                << timing->lineTime_us << R"(,"frame_time_us":)" << timing->frameTime_us
                << R"(,"rolling_shutter_us":)" << timing->rollingShutter_us << R"(,"active_width":)"
                << timing->activeWidth << R"(,"active_height":)" << timing->activeHeight << "}";
        }
        auto lensRange = imx219Camera->getLensPositionRange();
        if (lensRange.valid)
        {
            oss << R"(,")" << sanuwave::protocol::LensRangeField::IMX219_MIN << R"(":)" << lensRange.min
                << R"(,")" << sanuwave::protocol::LensRangeField::IMX219_MAX << R"(":)" << lensRange.max;
        }
    }

    // Deliberately wrong hash for testing client version-mismatch dialog
#ifdef TEST_VERSION_MISMATCH
    oss << R"(,")" << sanuwave::protocol::VersionField::KEY_GIT_HASH    << R"(":")" << "deadbeef"         << R"(")"
        << R"(,")" << sanuwave::protocol::VersionField::KEY_GIT_BRANCH  << R"(":")" << SANUWAVE_GIT_BRANCH << R"(")"
        << R"(,")" << sanuwave::protocol::VersionField::KEY_BUILD_TIME  << R"(":")" << SANUWAVE_BUILD_TIME  << R"(")"
        << R"(,")" << sanuwave::protocol::VersionField::KEY_VERSION_STR << R"(":")" << "0.0.0-deadbeef"    << R"(")";
#else
    oss << R"(,")" << sanuwave::protocol::VersionField::KEY_GIT_HASH    << R"(":")" << SANUWAVE_VERSION_GIT_SHA << R"(")"
        << R"(,")" << sanuwave::protocol::VersionField::KEY_GIT_BRANCH  << R"(":")" << SANUWAVE_GIT_BRANCH      << R"(")"
        << R"(,")" << sanuwave::protocol::VersionField::KEY_BUILD_TIME  << R"(":")" << SANUWAVE_BUILD_TIME       << R"(")"
        << R"(,")" << sanuwave::protocol::VersionField::KEY_VERSION_STR << R"(":")" << SANUWAVE_VERSION_STRING   << R"(")";
#endif

    oss << "}";
    return oss.str();
}

CaptureSettings CommandHandler::extractCaptureSettings(const ParamExtractor &p, int defaultWidth,
                                                       int defaultHeight)
{
    CaptureSettings settings;

    // Resolution
    settings.width = p.getInt(Param::WIDTH, defaultWidth);
    settings.height = p.getInt(Param::HEIGHT, defaultHeight);

    // Exposure
    settings.autoExposure = p.getBool(Param::AUTO_EXPOSURE, true);
    settings.exposureTime_us = p.getInt(Param::EXPOSURE_TIME_US, 0);
    settings.evCompensation = p.getFloat(Param::EV_COMPENSATION, 0.0f);

    // Gain
    settings.autoAnalogGain = p.getBool(Param::AUTO_ANALOG_GAIN, true);
    settings.analogGain = p.getFloat(Param::ANALOG_GAIN, 1.0f);
    settings.digitalGain = p.getFloat(Param::DIGITAL_GAIN, 1.0f);

    // White balance
    settings.autoWhiteBalance = p.getBool(Param::AUTO_WHITE_BALANCE, true);
    settings.redGain = p.getFloat(Param::RED_GAIN, 1.0f);
    settings.blueGain = p.getFloat(Param::BLUE_GAIN, 1.0f);

    // Focus
    settings.autoFocus = p.getBool(Param::AUTO_FOCUS, true);
    settings.lensPosition = p.getFloat(Param::LENS_POSITION, 0.0f);

    // Advanced
    settings.denoiseMode = p.getString(Param::DENOISE_MODE, "auto");
    settings.hdrMode = p.getBool(Param::HDR_MODE, false);
    settings.meteringMode = p.getString(Param::METERING_MODE, "center");

    // Raw mode (for raw captures)
    settings.rawMode = p.getBool(Param::RAW_MODE, false);
    settings.rawBitDepth = p.getInt(Param::RAW_BIT_DEPTH, 10);

    return settings;
}

void CommandHandler::logCaptureSettings(const CaptureSettings &settings, const std::string &camera)
{
    LOG_INFO << "=== Capturing " << camera << " image ===" << std::endl;
    LOG_INFO << "  Resolution: " << settings.width << "x" << settings.height << std::endl;
    LOG_INFO << "  AutoExposure: " << settings.autoExposure
             << ", ExposureTime: " << settings.exposureTime_us << "us" << std::endl;
    LOG_INFO << "  AutoGain: " << settings.autoAnalogGain << ", AnalogGain: " << settings.analogGain
             << std::endl;
    LOG_INFO << "  AutoWB: " << settings.autoWhiteBalance << ", R/B gains: " << settings.redGain
             << "/" << settings.blueGain << std::endl;
    LOG_INFO << "  AutoFocus: " << settings.autoFocus << ", LensPos: " << settings.lensPosition
             << std::endl;
    if (settings.rawMode)
        LOG_INFO << "  RawMode: " << settings.rawBitDepth << "-bit" << std::endl;
}

void CommandHandler::onClientDisconnect()
{
    LOG_INFO << "Client disconnected, cleaning up..." << std::endl;

    if (streaming || dualStreaming)
    {
        LOG_INFO << "Stopping active " << streamModality << " stream" << std::endl;
        streaming = false;
        dualStreaming = false;
        frameCondition.notify_all();

        if (streamModality == Camera::IMX708 && imx708Camera)
            imx708Camera->stopStreaming();
        else if (streamModality == Camera::IMX219 && imx219Camera)
            imx219Camera->stopStreaming();
        else if (streamModality == Camera::DUAL && imx708Camera)
            imx708Camera->stopStreaming();

        if (streamWorker.joinable())
            streamWorker.join();
        if (dualStreamWorker.joinable())
        {
            dualStreamWorker.join();
            jpegEncoderDual.reset();
        }

        {
            std::lock_guard<std::mutex> lock(latestFrameMutex);
            frameReady = false;
        }
    }

    multiParams->setParameter(Camera::IMX708, Param::FRAME_DURATION_ENABLED, "false", ParamMode::STREAMING);
    multiParams->setParameter(Camera::IMX219, Param::FRAME_DURATION_ENABLED, "false", ParamMode::STREAMING);
    multiParams->setParameter(Camera::IMX708, Param::FRAME_DURATION_US, "0", ParamMode::STREAMING);
    multiParams->setParameter(Camera::IMX219, Param::FRAME_DURATION_US, "0", ParamMode::STREAMING);


    if (distanceRanging && distanceSensor && distanceSensor->isInitialized())
    {
        LOG_INFO << "Stopping distance ranging" << std::endl;
        distanceSensor->stopRanging();
        distanceRanging = false;
    }
    if (imuSensor && imuSensor->isStreaming())
    {
        LOG_INFO << "Stopping IMU streaming (client disconnect)" << std::endl;
        imuSensor->stop();
    }

    if (ledGpio) 
    {
        ledGpio->torchOff(LedGpioController::Group::ALL);
        ledGpio->strobeOff(LedGpioController::Group::ALL);
    }
    ledGpioMode = LedGpioMode::OFF;

     if (strobeController)
        strobeController->setEnabled(false);

    // Turn off any I2C LEDs that were selected and clear the selection
    if (!selectedLeds.empty() && ledManager && ledManager->isInitialized())
    {
        for (const auto& sel : selectedLeds)
            ledManager->turnOff(static_cast<LedMgr::LedId>(sel.ledId));
    }
    selectedLeds.clear();

    intervalShooting = false;
    if (intervalStillWorker.joinable())
    {
        intervalStillWorker.join();
    }   

      auto resetVBlank = [](CameraBase* cam) {
        if (!cam) return;
        auto timing = cam->getSensorTiming();
        if (timing && timing->vblankMin > 0)
        {
            auto result = cam->setVBlank(timing->vblankMin);
            if (!result)
            {
                LOG_ERROR << "Failed to reset VBlank: " << cam->getLastError() << std::endl;
            }
            else
            {
                LOG_INFO << "Reset VBlank to " << timing->vblankMin << " lines" << std::endl;
            }
        }
    };
    resetVBlank(imx708Camera);
    resetVBlank(imx219Camera);

    clearImageData();
    LOG_INFO << "Cleanup complete" << std::endl;
}

void CommandHandler::setStreamFrameCallback(StreamFrameCallback callback)
{
    streamCallback = std::move(callback);
}

void CommandHandler::clearImageData()
{
    lastImageData.clear();
    lastImageModality.clear();
}

// ---------------------------------------------------------------------------
// ledStateToJson()
//
// Serialises a vector of LedState into a compact JSON array.
// e.g. [{"led_id":"led_0","active":true,"drive_ma":94.5}]
// ---------------------------------------------------------------------------
std::string CommandHandler::ledStateToJson(const std::vector<LedState>& states)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << "[";
    for (size_t i = 0; i < states.size(); ++i)
    {
        if (i) oss << ",";
        oss << R"({"led_id":")" << states[i].led_id << R"(")"
            << R"(,"active":)"   << (states[i].active ? "true" : "false")
            << R"(,"drive_ma":)" << states[i].drive_ma
            << "}";
    }

    if (!states.empty()) oss << ",";
    // Always append a GPIO trigger entry so the client has unambiguous
    // record of what the GPIO lines were doing at the moment of capture.
    std::string gpioModeStr = (ledGpioMode == CommandHandler::LedGpioMode::TORCH)  ? "torch"  :
                              (ledGpioMode == CommandHandler::LedGpioMode::STROBE) ? "strobe" : "off";

    oss << R"({"led_id":"gpio_trigger")"
        << R"(,"active":)"              << ((gpioModeStr  != "off") ? "true" : "false")
        << R"(,"drive_ma":0)"           // GPIO is digital — mA not applicable
        << R"(,"led_gpio_mode":")"      << gpioModeStr << R"(")"
        << R"(,"pre_frame_delay_ms":)"  << CommandHandler::ledPreFrameDelay_ms
        << R"(,"post_capture_off":)"    << (CommandHandler::ledPostCaptureOff ? "true" : "false")
        << "}";

    oss << "]";
    return oss.str();
}


std::vector<uint8_t> CommandHandler::matToJpeg(const cv::Mat &image, int quality)
{
    std::vector<uint8_t> buffer;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
    cv::imencode(".jpg", image, buffer, params);
    return buffer;
}

std::string CommandHandler::buildJsonError(const std::string &msg)
{
    return R"({"type":"error","message":")" + msg + R"("})";
}

std::string CommandHandler::buildJsonSuccess(const std::string &msg)
{
    return R"({"type":"status","message":")" + msg + R"("})";
}

} // namespace sanuwave
