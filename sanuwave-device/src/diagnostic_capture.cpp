// ============================================================================
// diagnostic_capture.cpp
// Server-side diagnostic raw capture implementation
// ============================================================================

#include "command_handler.h"
#include <unistd.h>
#include "protocol_constants.h"
#include "logger.h"
#include <sstream>
#include <cstring>
#include <chrono>
#include "LedMgr.h"

namespace sanuwave
{

using namespace protocol;


// ============================================================================
// CALLBACK SETTER
// ============================================================================

void CommandHandler::setDiagFrameCallback(
    std::function<void(const std::string&, const uint8_t*, size_t)> callback)
{
    diagFrameCallback = std::move(callback);
}

// ============================================================================
// CAMERA LOOKUP
// ============================================================================

CameraBase* CommandHandler::getDiagCamera(const std::string& cameraName) const
{
    if (cameraName == Camera::IMX708)
        return imx708Camera;
    if (cameraName == Camera::IMX219)
        return imx219Camera;
    // Thermal handled separately — not a CameraBase
    return nullptr;
}

bool CommandHandler::isDiagCameraStreaming(const std::string& cameraName) const
{
    // Check global streaming state first
    if (streaming.load())
    {
        // Any camera streaming blocks all diagnostic captures
        return true;
    }
    
    // Also check individual camera streaming state
    if (cameraName == Camera::IMX708 && imx708Camera && imx708Camera->isStreaming())
        return true;
    if (cameraName == Camera::IMX219 && imx219Camera && imx219Camera->isStreaming())
        return true;
    
    return false;
}

// ============================================================================
// ERROR RESPONSE BUILDER
// ============================================================================

std::string CommandHandler::buildDiagError(const std::string& camera, DiagError code)
{
    std::ostringstream json;
    json << R"({"type":")" << DiagResponseType::RAW_ERROR << R"(",)"
         << R"("camera":")" << camera << R"(",)"
         << R"("error_code":)" << static_cast<int>(code) << ","
         << R"("error_message":")" << diagErrorToString(code) << R"("})";
    return json.str();
}

// ============================================================================
// RAW HEADER BUILDER — libcamera cameras (IMX708, IMX219)
// ============================================================================

std::string CommandHandler::buildDiagRawHeader(
    const std::string& camera,
    uint8_t frameIndex, uint8_t frameCount,
    uint32_t width, uint32_t height,
    const cv::Mat& rawFrame,
    const FrameMetadata& metadata,
    const CaptureSettings& /*settings*/)
{
    // Both IMX708 and IMX219 output BGGR after orientation/flip on Pi 5.
    // PiSP Frontend always outputs 16-bit unpacked regardless of sensor native depth.
    int bayerPattern = static_cast<int>(BayerPattern::BGGR);
    int bitsPerPixel = 16;      // actual data width in the buffer
    int sensorBitDepth = 10;    // native sensor ADC depth (informational)
    std::string pixelFormat = "SBGGR16";

    if (camera == Camera::IMX219)
    {
        // IMX219: 10-bit native, BGGR, 16-bit unpacked
        pixelFormat = "SBGGR16";
        sensorBitDepth = 10;
    }
    else if (camera == Camera::IMX708)
    {
        // IMX708: 10-bit native, BGGR, 16-bit unpacked
        pixelFormat = "SBGGR16";
        sensorBitDepth = 10;
    }

    size_t dataSize = rawFrame.total() * rawFrame.elemSize();

    std::ostringstream json;
    json << R"({"type":")" << DiagResponseType::RAW_FRAME << R"(",)"
         << R"("camera":")" << camera << R"(",)"
         << R"(")" << DiagResponse::FRAME_INDEX << R"(":)" << (int)frameIndex << ","
         << R"(")" << DiagResponse::FRAME_COUNT << R"(":)" << (int)frameCount << ","
         << R"("width":)" << width << ","
         << R"("height":)" << height << ","
         << R"(")" << DiagResponse::BITS_PER_PIXEL << R"(":)" << bitsPerPixel << ","
         << R"("sensor_bit_depth":)" << sensorBitDepth << ","
         << R"(")" << DiagResponse::BAYER_PATTERN << R"(":)" << bayerPattern << ","
         << R"(")" << DiagResponse::PIXEL_FORMAT << R"(":")" << pixelFormat << R"(",)"
         << R"(")" << DiagResponse::DATA_SIZE << R"(":)" << dataSize << ",";

    // Metadata block
    json << R"(")" << DiagResponse::METADATA << R"(":{)"
         << R"(")" << DiagMeta::ACTUAL_EXPOSURE_US << R"(":)" << metadata.exposureTime_us << ","
         << R"(")" << DiagMeta::ACTUAL_ANALOG_GAIN << R"(":)" << metadata.analogGain << ","
         << R"(")" << DiagMeta::ACTUAL_DIGITAL_GAIN << R"(":)" << metadata.digitalGain << ","
         << R"(")" << DiagMeta::COLOUR_GAINS << R"(":[)" << metadata.redGain << "," << metadata.blueGain << "],"
         << R"(")" << DiagMeta::COLOUR_TEMPERATURE << R"(":)" << metadata.colourTemperature << ","
         << R"(")" << DiagMeta::SENSOR_TIMESTAMP_NS << R"(":)" << metadata.timestamp_ns << ","
         << R"(")" << DiagMeta::LENS_SHADING_APPLIED << R"(":false,)";

    // Black levels
    if (metadata.blackLevelsValid)
    {
        json << R"(")" << DiagMeta::SENSOR_BLACK_LEVELS << R"(":[)"
             << metadata.sensorBlackLevels[0] << ","
             << metadata.sensorBlackLevels[1] << ","
             << metadata.sensorBlackLevels[2] << ","
             << metadata.sensorBlackLevels[3] << "],";
    }
    else
    {
        json << R"(")" << DiagMeta::SENSOR_BLACK_LEVELS << R"(":[0,0,0,0],)";
    }

    json << R"(")" << DiagMeta::AE_ENABLED << R"(":)" << (metadata.aeEnabled ? "true" : "false") << ","
         << R"(")" << DiagMeta::AWB_ENABLED << R"(":)" << (metadata.awbEnabled ? "true" : "false") << ","
         << R"("hblank":)" << metadata.hblank << ","
         << R"("vblank":)" << metadata.vblank << ","
         << R"("frame_duration_us":)" << metadata.frameDuration_us
         << "},";

    // Sensor info block
    CameraBase* cam = getDiagCamera(camera);
    json << R"(")" << DiagResponse::SENSOR_INFO << R"(":{)"
         << R"(")" << DiagSensorInfo::NAME << R"(":")" << camera << R"(",)"
         << R"(")" << DiagSensorInfo::NATIVE_BIT_DEPTH << R"(":)" << sensorBitDepth << ","
         << R"(")" << DiagSensorInfo::ACTIVE_AREA_WIDTH << R"(":)"
         << (cam ? cam->getMaxResolution().width : (int)width) << ","
         << R"(")" << DiagSensorInfo::ACTIVE_AREA_HEIGHT << R"(":)"
         << (cam ? cam->getMaxResolution().height : (int)height)
         << "},";

    // Size field (matches existing protocol pattern for binary payload)
    json << R"("size":)" << dataSize << "}";

    return json.str();
}

// ============================================================================
// LEPTON RAW HEADER BUILDER
// ============================================================================

std::string CommandHandler::buildLeptonDiagRawHeader(
    uint8_t frameIndex, uint8_t frameCount,
    uint32_t width, uint32_t height,
    size_t dataSize)
{
    std::ostringstream json;
    json << R"({"type":")" << DiagResponseType::RAW_FRAME << R"(",)"
         << R"("camera":")" << Camera::THERMAL << R"(",)"
         << R"(")" << DiagResponse::FRAME_INDEX << R"(":)" << (int)frameIndex << ","
         << R"(")" << DiagResponse::FRAME_COUNT << R"(":)" << (int)frameCount << ","
         << R"("width":)" << width << ","
         << R"("height":)" << height << ","
         << R"(")" << DiagResponse::BITS_PER_PIXEL << R"(":16,)"   // CV_16UC1 centi-Kelvin
         << R"(")" << DiagResponse::BAYER_PATTERN << R"(":255,)"   // NONE
         << R"(")" << DiagResponse::PIXEL_FORMAT << R"(":"CENTIKELVIN_16",)"
         << R"(")" << DiagResponse::DATA_SIZE << R"(":)" << dataSize << ",";
    
    // Metadata — Lepton-specific
    // TODO: Extract real telemetry from thermalCamera when API is available
    json << R"(")" << DiagResponse::METADATA << R"(":{)"
         << R"(")" << DiagLeptonMeta::FPA_TEMPERATURE_K << R"(":0,)"
         << R"(")" << DiagLeptonMeta::AUX_TEMPERATURE_K << R"(":0,)"
         << R"(")" << DiagLeptonMeta::AGC_ENABLED << R"(":false,)"
         << R"(")" << DiagLeptonMeta::RADIOMETRY_ENABLED << R"(":true)"
         << "},";
    
    // Sensor info
    json << R"(")" << DiagResponse::SENSOR_INFO << R"(":{)"
         << R"(")" << DiagSensorInfo::NAME << R"(":"lepton3",)"
         << R"(")" << DiagSensorInfo::NATIVE_BIT_DEPTH << R"(":14,)"
         << R"(")" << DiagSensorInfo::ACTIVE_AREA_WIDTH << R"(":160,)"
         << R"(")" << DiagSensorInfo::ACTIVE_AREA_HEIGHT << R"(":120)"
         << "},";
    
    json << R"("size":)" << dataSize << "}";
    
    return json.str();
}

// ============================================================================
// MAIN HANDLER: diag_raw_capture
// ============================================================================

std::string CommandHandler::handleDiagRawCapture(
    const std::map<std::string, std::string>& p)
{
    ParamExtractor params(p);
    std::string camera = params.getString(Param::CAMERA, Camera::IMX708);

    LOG_INFO << "Diagnostic raw capture requested for camera: " << camera << std::endl;

    // Gate: check streaming state
    if (isDiagCameraStreaming(camera))
    {
        LOG_WARNING << "Diagnostic capture rejected: camera " << camera
                    << " is streaming" << std::endl;
        return buildDiagError(camera, DiagError::CAMERA_BUSY_STREAMING);
    }

    // Parse diagnostic capture parameters
    uint32_t exposureUs  = params.getInt(DiagParam::EXPOSURE_US, 0);
    float    analogGain  = params.getFloat(DiagParam::ANALOG_GAIN, 0.0f);
    float    digitalGain = params.getFloat(DiagParam::DIGITAL_GAIN, 0.0f);
    bool     disableAwb  = params.getBool(DiagParam::DISABLE_AWB, true);
    bool     disableAe   = params.getBool(DiagParam::DISABLE_AE, true);
    bool     disableDn   = params.getBool(DiagParam::DISABLE_DENOISE, true);
    int      frameCount  = params.getInt(DiagParam::FRAME_COUNT, 1);
    int      ledDelayMs  = params.getInt(DiagParam::LED_PRE_CAPTURE_DELAY_MS, 0);

    frameCount = std::clamp(frameCount, 1, 10);
    if (ledDelayMs < 0)   ledDelayMs = 0;
    if (ledDelayMs > 500) ledDelayMs = 500;

    // Parse LED selection directly from capture params — no prior LED_SELECT needed
    std::vector<LedSelection> diagLeds;
    {
        auto ids        = params.getIntList(Param::LED_IDS);
        auto brightness = params.getUInt8List(Param::LED_BRIGHTNESSES);
        for (size_t i = 0; i < ids.size(); ++i)
            diagLeds.push_back({ids[i], i < brightness.size() ? brightness[i] : uint8_t(128)});
    }

    // Handle Lepton thermal separately
    if (camera == Camera::THERMAL)
    {
        if (!thermalCamera)
            return buildDiagError(camera, DiagError::CAMERA_NOT_FOUND);

        for (int i = 0; i < frameCount; i++)
        {
            cv::Mat rawFrame = thermalCamera->captureRaw();
            if (rawFrame.empty())
                return buildDiagError(camera, DiagError::CAPTURE_FAILED);

            size_t dataSize = rawFrame.total() * rawFrame.elemSize();
            std::string header = buildLeptonDiagRawHeader(
                i, frameCount, rawFrame.cols, rawFrame.rows, dataSize);

            if (diagFrameCallback)
                diagFrameCallback(header, rawFrame.data, dataSize);
        }

        return buildJsonSuccess("Diagnostic thermal capture complete");
    }

    // libcamera-based cameras (IMX708, IMX219)
    CameraBase* cam = getDiagCamera(camera);
    if (!cam)
        return buildDiagError(camera, DiagError::CAMERA_NOT_FOUND);

    if (!cam->isInitialized())
        return buildDiagError(camera, DiagError::CAMERA_NOT_FOUND);

    // Build CaptureSettings for raw diagnostic capture
    CaptureSettings diagSettings;
    diagSettings.rawMode     = true;
    diagSettings.rawBitDepth = 10;
    diagSettings.width       = cam->getMaxResolution().width;
    diagSettings.height      = cam->getMaxResolution().height;

    if (disableAe)
    {
        diagSettings.autoExposure    = false;
        diagSettings.exposureTime_us = (exposureUs > 0) ? exposureUs : 10000;
    }
    else
    {
        diagSettings.autoExposure = true;
    }

    if (analogGain > 0.0f)
    {
        diagSettings.autoAnalogGain = false;
        diagSettings.analogGain     = analogGain;
    }
    else if (disableAe)
    {
        diagSettings.autoAnalogGain = false;
        diagSettings.analogGain     = 1.0f;
    }

    diagSettings.digitalGain = (digitalGain > 0.0f) ? digitalGain : 1.0f;

    if (disableAwb)
    {
        diagSettings.autoWhiteBalance = false;
        diagSettings.redGain          = 1.0f;
        diagSettings.blueGain         = 1.0f;
    }

    if (disableDn)
        diagSettings.denoiseMode = "off";

    diagSettings.autoFocus = false;

    LOG_INFO << "Diagnostic capture settings: " << diagSettings << std::endl;

    // Arm LEDs and assert GPIO
    bool ledActive = false;
    if (!diagLeds.empty() && ledManager && ledManager->isInitialized() && ledGpio)
    {
        using Clock = std::chrono::steady_clock;
        using Ms    = std::chrono::milliseconds;

        auto tTotal = Clock::now();

        for (const auto& sel : diagLeds)
        {
            auto t0 = Clock::now();
            ledManager->setFlashTorchBrightness(static_cast<LedMgr::LedId>(sel.ledId), sel.brightness);
            ledManager->setTorchMode(static_cast<LedMgr::LedId>(sel.ledId));            auto t1 = Clock::now();
            LOG_INFO << "Diag LED arm: id=" << sel.ledId
                     << " brightness=" << (int)sel.brightness
                     << " took " << std::chrono::duration_cast<Ms>(t1 - t0).count()
                     << " ms" << std::endl;
        }

        auto tArmed = Clock::now();
        LOG_INFO << "All LEDs armed in "
                 << std::chrono::duration_cast<Ms>(tArmed - tTotal).count()
                 << " ms — asserting GPIO now" << std::endl;

        ledGpio->torchOn(LedGpioController::Group::ALL);
        LOG_INFO << "Diag LED: torch ON, settling " << ledDelayMs << " ms" << std::endl;

        if (ledDelayMs > 0)
            usleep(ledDelayMs * 1000);

        ledActive = true;
    }

    // Capture frame(s)
    for (int i = 0; i < frameCount; i++)
    {
        FrameMetadata metadata;
        cv::Mat rawFrame = cam->captureStill(diagSettings, &metadata);

        if (rawFrame.empty())
        {
            LOG_ERROR << "Diagnostic capture failed on frame " << i << std::endl;
            if (ledActive)
            {
                ledGpio->torchOff(LedGpioController::Group::ALL);
                for (const auto& sel : diagLeds)
                    ledManager->turnOff(static_cast<LedMgr::LedId>(sel.ledId));
            }
            return buildDiagError(camera, DiagError::CAPTURE_FAILED);
        }

        LOG_INFO << "Diagnostic frame " << i << ": "
                 << rawFrame.cols << "x" << rawFrame.rows
                 << " type=" << rawFrame.type()
                 << " elemSize=" << rawFrame.elemSize()
                 << " total=" << rawFrame.total() << std::endl;

        std::string header = buildDiagRawHeader(
            camera, i, frameCount,
            rawFrame.cols, rawFrame.rows,
            rawFrame, metadata, diagSettings);

        if (diagFrameCallback)
        {
            size_t dataSize = rawFrame.total() * rawFrame.elemSize();
            if (!rawFrame.isContinuous())
            {
                cv::Mat contiguous = rawFrame.clone();
                diagFrameCallback(header, contiguous.data, dataSize);
            }
            else
            {
                diagFrameCallback(header, rawFrame.data, dataSize);
            }
        }
    }

    // Turn LEDs off after all frames captured
    if (ledActive)
    {
        ledGpio->torchOff(LedGpioController::Group::ALL);
        LOG_INFO << "Diag LED: torch OFF" << std::endl;
        for (const auto& sel : diagLeds)
            ledManager->turnOff(static_cast<LedMgr::LedId>(sel.ledId));
    }

    return buildJsonSuccess("Diagnostic raw capture complete: "
                           + std::to_string(frameCount) + " frame(s)");
}

// ============================================================================
// ROI HANDLER: diag_raw_roi_capture
// ============================================================================

std::string CommandHandler::handleDiagRawRoiCapture(
    const std::map<std::string, std::string>& p)
{
    ParamExtractor params(p);
    std::string camera = params.getString(Param::CAMERA, Camera::IMX708);
    
    // Gate: check streaming state
    if (isDiagCameraStreaming(camera))
        return buildDiagError(camera, DiagError::CAMERA_BUSY_STREAMING);
    
    // Parse ROI parameters
   // uint32_t roiX      = params.getInt(DiagParam::ROI_X, 0);
   // uint32_t roiY      = params.getInt(DiagParam::ROI_Y, 0);
    uint32_t roiWidth  = params.getInt(DiagParam::ROI_WIDTH, 0);
    uint32_t roiHeight = params.getInt(DiagParam::ROI_HEIGHT, 0);
    
    if (roiWidth == 0 || roiHeight == 0)
        return buildDiagError(camera, DiagError::INVALID_PARAMETERS);
    
    // Capture full frame first, then crop
    // (Hardware ROI on libcamera is complex and sensor-dependent,
    //  software crop of raw data is simpler and diagnostic-appropriate)
    std::string fullResult = handleDiagRawCapture(p);
    
    // The full capture already sent via diagFrameCallback.
    // For ROI, we'd capture full, crop, then send the cropped version.
    // 
    // TODO: Implement proper ROI extraction from raw Bayer data.
    // This requires careful handling to preserve the Bayer pattern alignment:
    //   - ROI x,y must be aligned to 2-pixel boundaries (Bayer grid)
    //   - Width/height must be even
    //
    // For now, the client can do ROI extraction from the full frame.
    // This is acceptable since this is a diagnostic tool, not real-time.
    
    LOG_WARNING << "ROI capture: sending full frame, client-side crop recommended" << std::endl;
    return fullResult;
}

} // namespace sanuwave
