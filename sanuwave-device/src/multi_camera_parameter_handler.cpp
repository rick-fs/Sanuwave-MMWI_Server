// multi_camera_parameter_handler.cpp
// Implementation of per-camera parameter management
#include "multi_camera_parameter_handler.h"
#include "camera_base.h"
#include "thermal_camera.h"
#include "vl53l4cd_wrapper.h"
#include "protocol_constants.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include "logger.h"

namespace sanuwave
{

namespace Camera = protocol::Camera;
namespace Param = protocol::Param;
namespace ParamMode = protocol::ParamMode;
namespace Colormap = protocol::Colormap;

// ============================================================================
// PARAMETER STRUCT IMPLEMENTATIONS
// ============================================================================

void RgbCameraParams::reset()
{
    *this = RgbCameraParams();
}

std::string RgbCameraParams::toJson() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{"
        << "\"" << Param::EXPOSURE_TIME_US << "\":" << exposureTime_us << ","
        << "\"" << Param::AUTO_EXPOSURE << "\":" << (autoExposure ? "true" : "false") << ","
        << "\"" << Param::EV_COMPENSATION << "\":" << evCompensation << ","
        << "\"" << Param::AUTO_ANALOG_GAIN << "\":" << (autoAnalogGain ? "true" : "false") << ","
        << "\"" << Param::ANALOG_GAIN << "\":" << analogGain << ","
        << "\"" << Param::DIGITAL_GAIN << "\":" << digitalGain << ","
        << "\"iso\":" << iso << ","
        << "\"" << Param::AUTO_WHITE_BALANCE << "\":" << (autoWhiteBalance ? "true" : "false") << ","
        << "\"awb_mode\":\"" << awbMode << "\","
        << "\"" << Param::RED_GAIN << "\":" << redGain << ","
        << "\"" << Param::BLUE_GAIN << "\":" << blueGain << ","
        << "\"" << Param::AUTO_FOCUS << "\":" << (autoFocus ? "true" : "false") << ","
        << "\"" << Param::LENS_POSITION << "\":" << lensPosition << ","
        << "\"" << Param::DENOISE_MODE << "\":\"" << denoiseMode << "\","
        << "\"" << Param::HDR_MODE << "\":" << (hdrMode ? "true" : "false") << ","
        << "\"" << Param::RAW_MODE << "\":" << (rawMode ? "true" : "false") << ","
        << "\"" << Param::RAW_BIT_DEPTH << "\":" << rawBitDepth << "}";
    return oss.str();
}

void ArducamCameraParams::reset()
{
    *this = ArducamCameraParams();
}

std::string ArducamCameraParams::toJson() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{"
        << "\"" << Param::EXPOSURE_TIME_US << "\":" << exposureTime_us << ","
        << "\"" << Param::AUTO_EXPOSURE << "\":" << (autoExposure ? "true" : "false") << ","
        << "\"" << Param::EV_COMPENSATION << "\":" << evCompensation << ","
        << "\"" << Param::AUTO_ANALOG_GAIN << "\":" << (autoAnalogGain ? "true" : "false") << ","
        << "\"" << Param::ANALOG_GAIN << "\":" << analogGain << ","
        << "\"" << Param::DIGITAL_GAIN << "\":" << digitalGain << ","
        << "\"iso\":" << iso << ","
        << "\"" << Param::AUTO_WHITE_BALANCE << "\":" << (autoWhiteBalance ? "true" : "false") << ","
        << "\"awb_mode\":\"" << awbMode << "\","
        << "\"" << Param::RED_GAIN << "\":" << redGain << ","
        << "\"" << Param::BLUE_GAIN << "\":" << blueGain << ","
        << "\"" << Param::AUTO_FOCUS << "\":" << (autoFocus ? "true" : "false") << ","
        << "\"" << Param::LENS_POSITION << "\":" << lensPosition << ","
        << "\"" << Param::DENOISE_MODE << "\":\"" << denoiseMode << "\","
        << "\"binning_mode\":\"" << binningMode << "\","
        << "\"" << Param::RAW_MODE << "\":" << (rawMode ? "true" : "false") << ","
        << "\"" << Param::RAW_BIT_DEPTH << "\":" << rawBitDepth
        << "}";
    return oss.str();
}

void ThermalCameraParams::reset()
{
    *this = ThermalCameraParams();
}

std::string ThermalCameraParams::toJson() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{"
        << "\"" << Param::EMISSIVITY << "\":" << emissivity << ","
        << "\"reflected_temp_c\":" << reflectedTemp_C << ","
        << "\"auto_range\":" << (autoRange ? "true" : "false") << ","
        << "\"" << Param::MIN_TEMP << "\":" << minTemp_C << ","
        << "\"" << Param::MAX_TEMP << "\":" << maxTemp_C << ","
        << "\"" << Param::COLORMAP << "\":\"" << colormap << "\","
        << "\"" << Param::NUC_ENABLED << "\":" << (applyNUC ? "true" : "false") << ","
        << "\"smoothing_factor\":" << smoothingFactor << ","
        << "\"enable_roi\":" << (enableRoi ? "true" : "false") << ","
        << "\"roi_x\":" << roiX << ","
        << "\"roi_y\":" << roiY << ","
        << "\"roi_width\":" << roiWidth << ","
        << "\"roi_height\":" << roiHeight << ","
        << "\"" << Param::ALARM_ENABLED << "\":" << (enableTempAlarm ? "true" : "false") << ","
        << "\"" << Param::ALARM_TEMP << "\":" << alarmThreshold_C << "}";
    return oss.str();
}

void DepthSensorParams::reset()
{
    *this = DepthSensorParams();
}

std::string DepthSensorParams::toJson() const
{
    std::ostringstream oss;
    oss << "{"
        << "\"ranging_mode\":" << rangingMode << ","
        << "\"" << Param::TIMING_BUDGET << "\":" << timingBudget_ms << ","
        << "\"" << Param::INTER_MEASUREMENT << "\":" << intermeasurementPeriod_ms << ","
        << "\"" << Param::SIGMA_THRESHOLD << "\":" << sigmaThreshold_mm << ","
        << "\"" << Param::SIGNAL_THRESHOLD << "\":" << signalThreshold_kcps << ","
        << "\"offset_calibration_mm\":" << offsetCalibration_mm << ","
        << "\"crosstalk_calibration_kcps\":" << crosstalkCalibration_kcps << "}";
    return oss.str();
}

// ============================================================================
// MULTI-CAMERA PARAMETER HANDLER
// ============================================================================

MultiCameraParameterHandler::MultiCameraParameterHandler()
{
}

// ============================================================================
// INTERNAL HELPER METHODS - Get correct parameter reference by mode
// ============================================================================

RgbCameraParams& MultiCameraParameterHandler::getRgbParamsRef(const std::string& mode)
{
    if (mode == ParamMode::STREAMING)
        return rgbStreamingParams;
    return rgbCaptureParams;
}

const RgbCameraParams& MultiCameraParameterHandler::getRgbParamsRef(const std::string& mode) const
{
    if (mode == ParamMode::STREAMING)
        return rgbStreamingParams;
    return rgbCaptureParams;
}

ArducamCameraParams& MultiCameraParameterHandler::getArducamParamsRef(const std::string& mode)
{
    if (mode == ParamMode::STREAMING)
        return arducamStreamingParams;
    return arducamCaptureParams;
}

const ArducamCameraParams& MultiCameraParameterHandler::getArducamParamsRef(const std::string& mode) const
{
    if (mode == ParamMode::STREAMING)
        return arducamStreamingParams;
    return arducamCaptureParams;
}

ThermalCameraParams& MultiCameraParameterHandler::getThermalParamsRef(const std::string& mode)
{
    if (mode == ParamMode::STREAMING)
        return thermalStreamingParams;
    return thermalCaptureParams;
}

const ThermalCameraParams& MultiCameraParameterHandler::getThermalParamsRef(const std::string& mode) const
{
    if (mode == ParamMode::STREAMING)
        return thermalStreamingParams;
    return thermalCaptureParams;
}

// ============================================================================
// THERMAL CAMERA METHODS - WITH MODE SUPPORT
// ============================================================================

bool MultiCameraParameterHandler::setThermalParameter(const std::string& param,
                                                      const std::string& value,
                                                      const std::string& mode)
{
    try
    {
        ThermalCameraParams& params = getThermalParamsRef(mode);

        if (param == Param::EMISSIVITY)
            params.emissivity = stringToFloat(value);
        else if (param == "reflected_temp_c")
            params.reflectedTemp_C = stringToFloat(value);
        else if (param == "auto_range")
            params.autoRange = stringToBool(value);
        else if (param == Param::MIN_TEMP)
            params.minTemp_C = stringToFloat(value);
        else if (param == Param::MAX_TEMP)
            params.maxTemp_C = stringToFloat(value);
        else if (param == Param::COLORMAP)
            params.colormap = value;
        else if (param == Param::NUC_ENABLED)
            params.applyNUC = stringToBool(value);
        else if (param == "smoothing_factor")
            params.smoothingFactor = stringToFloat(value);
        else if (param == "enable_roi")
            params.enableRoi = stringToBool(value);
        else if (param == "roi_x")
            params.roiX = stringToInt(value);
        else if (param == "roi_y")
            params.roiY = stringToInt(value);
        else if (param == "roi_width")
            params.roiWidth = stringToInt(value);
        else if (param == "roi_height")
            params.roiHeight = stringToInt(value);
        else if (param == Param::ALARM_ENABLED)
            params.enableTempAlarm = stringToBool(value);
        else if (param == Param::ALARM_TEMP)
            params.alarmThreshold_C = stringToFloat(value);
        else
            return false;

        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string MultiCameraParameterHandler::getThermalParameters(const std::string& mode) const
{
    const ThermalCameraParams& params = getThermalParamsRef(mode);
    return params.toJson();
}

ThermalCameraParams MultiCameraParameterHandler::getThermalSettings(const std::string& mode) const
{
    return getThermalParamsRef(mode);
}

void MultiCameraParameterHandler::applyThermalSettings(ThermalCamera* camera, const std::string& mode) const
{
    if (!camera)
        return;

    const ThermalCameraParams& params = getThermalParamsRef(mode);

    camera->setEmissivity(params.emissivity);
    camera->setReflectedTemperature(params.reflectedTemp_C);
    camera->setAutoRange(params.autoRange);
    if (!params.autoRange)
        camera->setTemperatureRange(params.minTemp_C, params.maxTemp_C);
    camera->setColormap(params.colormap);
    camera->setNUCEnabled(params.applyNUC);
    camera->setSmoothingFactor(params.smoothingFactor);

    if (params.enableRoi)
        camera->setROI(params.roiX, params.roiY, params.roiWidth, params.roiHeight);
    else
        camera->clearROI();

    camera->setTemperatureAlarm(params.enableTempAlarm, params.alarmThreshold_C);

    LOG_DEBUG << "Applied thermal settings (mode=" << mode << ")" << std::endl;
    LOG_DEBUG << "  Emissivity: " << params.emissivity << std::endl;
    LOG_DEBUG << "  Temp range: " << params.minTemp_C << "°C - " << params.maxTemp_C << "°C" << std::endl;
    LOG_DEBUG << "  Colormap: " << params.colormap << std::endl;
    LOG_DEBUG << "  NUC: " << (params.applyNUC ? "enabled" : "disabled") << std::endl;
}

void MultiCameraParameterHandler::resetThermalSettings(const std::string& mode)
{
    getThermalParamsRef(mode).reset();
}

// ============================================================================
// RGB CAMERA METHODS - WITH MODE SUPPORT
// ============================================================================

bool MultiCameraParameterHandler::setRgbParameter(const std::string& param,
                                                  const std::string& value,
                                                  const std::string& mode)
{
    try
    {
        RgbCameraParams& params = getRgbParamsRef(mode);

        if (param == Param::EXPOSURE_TIME_US)
        {
            params.exposureTime_us = stringToInt(value);
            LOG_INFO << "  -> exposureTime_us set to " << params.exposureTime_us << std::endl;
        }
        else if (param == Param::AUTO_EXPOSURE)
        {
            params.autoExposure = stringToBool(value);
            LOG_INFO << "  -> autoExposure set to " << (params.autoExposure ? "true" : "false") << std::endl;
        }
        else if (param == Param::EV_COMPENSATION)
            params.evCompensation = stringToFloat(value);
        else if (param == Param::AUTO_ANALOG_GAIN)
            params.autoAnalogGain = stringToBool(value);
        else if (param == Param::ANALOG_GAIN)
        {
            params.autoAnalogGain = false;
            params.analogGain = stringToFloat(value);
        }
        else if (param == Param::DIGITAL_GAIN)
            params.digitalGain = stringToFloat(value);
        else if (param == "iso")
        {
            params.iso = stringToInt(value);
            params.analogGain = isoToAnalogGain(params.iso, Camera::IMX708);
        }
        else if (param == Param::AUTO_WHITE_BALANCE)
            params.autoWhiteBalance = stringToBool(value);
        else if (param == "awb_mode")
            params.awbMode = value;
        else if (param == Param::RED_GAIN)
            params.redGain = stringToFloat(value);
        else if (param == Param::BLUE_GAIN)
            params.blueGain = stringToFloat(value);
        else if (param == Param::AUTO_FOCUS)
            params.autoFocus = stringToBool(value);
        else if (param == Param::LENS_POSITION)
            params.lensPosition = stringToFloat(value);
        else if (param == Param::DENOISE_MODE)
            params.denoiseMode = value;
        else if (param == Param::HDR_MODE)
            params.hdrMode = stringToBool(value);
        else if (param == Param::RAW_MODE)
        {
            params.rawMode = stringToBool(value);
            LOG_INFO << "  -> rawMode set to " << (params.rawMode ? "true" : "false") << std::endl;
        }
        else if (param == Param::RAW_BIT_DEPTH)
        {
            int bitDepth = stringToInt(value);
            if (bitDepth == 8 || bitDepth == 10 || bitDepth == 12 || bitDepth == 16)
                params.rawBitDepth = bitDepth;
            else
            {
                params.rawBitDepth = 10;
                LOG_WARNING << "Invalid raw bit depth " << bitDepth << ", defaulting to 10" << std::endl;
            }
            LOG_INFO << "  -> rawBitDepth set to " << params.rawBitDepth << std::endl;
        }
         else if (param == Param::FRAME_DURATION_ENABLED)
        {
            params.frameDurationEnabled = stringToBool(value);
            LOG_INFO << "  -> frameDurationEnabled set to "
                     << (params.frameDurationEnabled ? "true" : "false") << std::endl;
        }
        else if (param == Param::FRAME_DURATION_US)
        {
            params.frameDuration_us = static_cast<int64_t>(std::stoll(value));
            LOG_INFO << "  -> frameDuration_us set to " << params.frameDuration_us << std::endl;
        }
        else
            return false;

        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string MultiCameraParameterHandler::getRgbParameters(const std::string& mode) const
{
    const RgbCameraParams& params = getRgbParamsRef(mode);
    return params.toJson();
}

CaptureSettings MultiCameraParameterHandler::getRgbSettings(const std::string& mode) const
{
    const RgbCameraParams& params = getRgbParamsRef(mode);

    CaptureSettings settings;
    settings.exposureTime_us = params.exposureTime_us;
    settings.autoExposure = params.autoExposure;
    settings.evCompensation = params.evCompensation;
    settings.autoAnalogGain = params.autoAnalogGain;
    settings.analogGain = params.analogGain;
    settings.digitalGain = params.digitalGain;
    settings.autoWhiteBalance = params.autoWhiteBalance;
    settings.awbMode = params.awbMode;
    settings.redGain = params.redGain;
    settings.blueGain = params.blueGain;
    settings.autoFocus = params.autoFocus;
    settings.lensPosition = params.lensPosition;
    settings.denoiseMode = params.denoiseMode;
    settings.hdrMode = params.hdrMode;
    settings.rawMode = params.rawMode;
    settings.rawBitDepth = params.rawBitDepth;
    settings.frameDurationEnabled = params.frameDurationEnabled;
    settings.frameDuration_us = params.frameDuration_us;
    return settings;
}

void MultiCameraParameterHandler::resetRgbSettings(const std::string& mode)
{
    getRgbParamsRef(mode).reset();
}

// ============================================================================
// ARDUCAM METHODS - WITH MODE SUPPORT
// ============================================================================

bool MultiCameraParameterHandler::setArducamParameter(const std::string& param,
                                                      const std::string& value,
                                                      const std::string& mode)
{
    try
    {
        ArducamCameraParams& params = getArducamParamsRef(mode);

        if (param == Param::EXPOSURE_TIME_US)
            params.exposureTime_us = stringToInt(value);
        else if (param == Param::AUTO_EXPOSURE)
        {
            params.autoExposure = stringToBool(value);
            // The RPi IPA couples AE and AGC — AeEnable=false locks both.
            // Keep autoAnalogGain in sync so applyGainControls() reflects reality.
            if (!params.autoExposure)
                params.autoAnalogGain = false;
            LOG_INFO << "  -> autoExposure set to "
                    << (params.autoExposure ? "true" : "false")
                    << (params.autoExposure ? "" : " (autoAnalogGain also locked)")
                    << std::endl;
        }
                    
        else if (param == Param::EV_COMPENSATION)
            params.evCompensation = stringToFloat(value);
        else if (param == Param::AUTO_ANALOG_GAIN)
            params.autoAnalogGain = stringToBool(value);
        else if (param == Param::ANALOG_GAIN)
        {
            params.autoAnalogGain = false;
            params.analogGain = stringToFloat(value);
        }
        else if (param == Param::DIGITAL_GAIN)
            params.digitalGain = stringToFloat(value);
        else if (param == "iso")
        {
            params.iso = stringToInt(value);
            params.analogGain = isoToAnalogGain(params.iso, Camera::IMX219);
        }
        else if (param == Param::AUTO_WHITE_BALANCE)
            params.autoWhiteBalance = stringToBool(value);
        else if (param == "awb_mode")
            params.awbMode = value;
        else if (param == Param::RED_GAIN)
            params.redGain = stringToFloat(value);
        else if (param == Param::BLUE_GAIN)
            params.blueGain = stringToFloat(value);
        else if (param == Param::AUTO_FOCUS)
            params.autoFocus = stringToBool(value);
        else if (param == Param::LENS_POSITION)
            params.lensPosition = stringToFloat(value);
        else if (param == Param::DENOISE_MODE)
            params.denoiseMode = value;
        else if (param == "binning_mode")
            params.binningMode = value;
        else if (param == Param::RAW_MODE)
        {
            params.rawMode = stringToBool(value);
            LOG_INFO << "  -> rawMode set to " << (params.rawMode ? "true" : "false") << std::endl;
        }
        else if (param == Param::RAW_BIT_DEPTH)
        {
            int bitDepth = stringToInt(value);
            if (bitDepth == 8 || bitDepth == 10 || bitDepth == 12 || bitDepth == 16)
                params.rawBitDepth = bitDepth;
            else
            {
                params.rawBitDepth = 10;
                LOG_WARNING << "Invalid raw bit depth " << bitDepth << ", defaulting to 10" << std::endl;
            }
            LOG_INFO << "  -> rawBitDepth set to " << params.rawBitDepth << std::endl;
        }
       else if (param == Param::FRAME_DURATION_ENABLED)
        {
            params.frameDurationEnabled = stringToBool(value);
            LOG_INFO << "  -> frameDurationEnabled set to "
                     << (params.frameDurationEnabled ? "true" : "false") << std::endl;
        }
        else if (param == Param::FRAME_DURATION_US)
        {
            params.frameDuration_us = static_cast<int64_t>(std::stoll(value));
            LOG_INFO << "  -> frameDuration_us set to " << params.frameDuration_us << std::endl;
        }
        else
            return false;

        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string MultiCameraParameterHandler::getArducamParameters(const std::string& mode) const
{
    const ArducamCameraParams& params = getArducamParamsRef(mode);
    return params.toJson();
}

CaptureSettings MultiCameraParameterHandler::getArducamSettings(const std::string& mode) const
{
    const ArducamCameraParams& params = getArducamParamsRef(mode);

    CaptureSettings settings;
    settings.exposureTime_us = params.exposureTime_us;
    settings.autoExposure = params.autoExposure;
    settings.evCompensation = params.evCompensation;
    settings.autoAnalogGain = params.autoAnalogGain;
    settings.analogGain = params.analogGain;
    settings.digitalGain = params.digitalGain;
    settings.autoWhiteBalance = params.autoWhiteBalance;
    settings.awbMode = params.awbMode;
    settings.redGain = params.redGain;
    settings.blueGain = params.blueGain;
    settings.autoFocus = params.autoFocus;
    settings.lensPosition = params.lensPosition;
    settings.denoiseMode = params.denoiseMode;
    settings.binningMode = params.binningMode;
    settings.frameDurationEnabled = params.frameDurationEnabled;
    settings.frameDuration_us = params.frameDuration_us;
    return settings;
}

void MultiCameraParameterHandler::resetArducamSettings(const std::string& mode)
{
    getArducamParamsRef(mode).reset();
}

// ============================================================================
// DEPTH SENSOR METHODS (NO MODE - SINGLE SET)
// ============================================================================

bool MultiCameraParameterHandler::setDepthParameter(const std::string& param,
                                                    const std::string& value)
{
    LOG_INFO << param << " " << value << std::endl;

    try
    {
        if (param == "ranging_mode")
            depthParams.rangingMode = stringToInt(value);
        else if (param == Param::TIMING_BUDGET)
            depthParams.timingBudget_ms = stringToInt(value);
        else if (param == Param::INTER_MEASUREMENT)
            depthParams.intermeasurementPeriod_ms = stringToInt(value);
        else if (param == Param::SIGMA_THRESHOLD)
            depthParams.sigmaThreshold_mm = stringToInt(value);
        else if (param == Param::SIGNAL_THRESHOLD)
            depthParams.signalThreshold_kcps = stringToInt(value);
        else if (param == "offset_calibration_mm")
            depthParams.offsetCalibration_mm = stringToInt(value);
        else if (param == "crosstalk_calibration_kcps")
            depthParams.crosstalkCalibration_kcps = stringToInt(value);
        else
            return false;

        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string MultiCameraParameterHandler::getDepthParameters() const
{
    return depthParams.toJson();
}

void MultiCameraParameterHandler::applyDepthSettings(VL53L4CDWrapper* sensor) const
{
    if (!sensor || !sensor->isInitialized())
    {
        LOG_WARNING << "Cannot apply depth settings - sensor not available or initialized" << std::endl;
        return;
    }

    LOG_DEBUG << "Applying depth sensor settings..." << std::endl;

    if (depthParams.rangingMode == 1)
        sensor->setRangingMode(VL53L4CDWrapper::RangingMode::SHORT);
    else if (depthParams.rangingMode == 2)
        sensor->setRangingMode(VL53L4CDWrapper::RangingMode::LONG);

    sensor->setTimingBudget(depthParams.timingBudget_ms);
    sensor->setIntermeasurementPeriod(depthParams.intermeasurementPeriod_ms);

    if (depthParams.sigmaThreshold_mm > 0)
        sensor->setSigmaThreshold(depthParams.sigmaThreshold_mm);
    if (depthParams.signalThreshold_kcps > 0)
        sensor->setSignalThreshold(depthParams.signalThreshold_kcps);
    if (depthParams.offsetCalibration_mm != 0)
        sensor->setOffset(depthParams.offsetCalibration_mm);
    if (depthParams.crosstalkCalibration_kcps != 0)
        sensor->setCrosstalk(depthParams.crosstalkCalibration_kcps);

    LOG_DEBUG << "Depth sensor settings applied successfully" << std::endl;
}

void MultiCameraParameterHandler::resetDepthSettings()
{
    depthParams.reset();
}

// ============================================================================
// UNIFIED OPERATIONS - WITH MODE SUPPORT
// ============================================================================

bool MultiCameraParameterHandler::setParameter(const std::string& camera,
                                               const std::string& param,
                                               const std::string& value,
                                               const std::string& mode)
{
    if (camera == Camera::IMX708)
        return setRgbParameter(param, value, mode);
    else if (camera == Camera::IMX219)
        return setArducamParameter(param, value, mode);
    else if (camera == Camera::THERMAL)
        return setThermalParameter(param, value, mode);
    else if (camera == Camera::DEPTH)
        return setDepthParameter(param, value);
    return false;
}

std::string MultiCameraParameterHandler::getParameters(const std::string& camera,
                                                       const std::string& mode) const
{
    if (camera == Camera::IMX708)
        return getRgbParameters(mode);
    else if (camera == Camera::IMX219)
        return getArducamParameters(mode);
    else if (camera == Camera::THERMAL)
        return getThermalParameters(mode);
    else if (camera == Camera::DEPTH)
        return getDepthParameters();
    return "{}";
}

void MultiCameraParameterHandler::resetSettings(const std::string& camera,
                                                const std::string& mode)
{
    if (camera == Camera::IMX708)
        resetRgbSettings(mode);
    else if (camera == Camera::IMX219)
        resetArducamSettings(mode);
    else if (camera == Camera::THERMAL)
        resetThermalSettings(mode);
    else if (camera == Camera::DEPTH)
        resetDepthSettings();
}

void MultiCameraParameterHandler::resetAllSettings()
{
    rgbCaptureParams.reset();
    rgbStreamingParams.reset();
    arducamCaptureParams.reset();
    arducamStreamingParams.reset();
    thermalCaptureParams.reset();
    thermalStreamingParams.reset();
    depthParams.reset();
}

std::string MultiCameraParameterHandler::getAllParameters() const
{
    std::ostringstream oss;
    oss << "{"
        << "\"" << Camera::IMX708 << "\":{\"" << ParamMode::CAPTURE << "\":" << getRgbParameters(ParamMode::CAPTURE)
        << ",\"" << ParamMode::STREAMING << "\":" << getRgbParameters(ParamMode::STREAMING) << "},"
        << "\"" << Camera::IMX219 << "\":{\"" << ParamMode::CAPTURE << "\":" << getArducamParameters(ParamMode::CAPTURE)
        << ",\"" << ParamMode::STREAMING << "\":" << getArducamParameters(ParamMode::STREAMING) << "},"
        << "\"" << Camera::THERMAL << "\":{\"" << ParamMode::CAPTURE << "\":" << getThermalParameters(ParamMode::CAPTURE)
        << ",\"" << ParamMode::STREAMING << "\":" << getThermalParameters(ParamMode::STREAMING) << "},"
        << "\"" << Camera::DEPTH << "\":" << getDepthParameters() << "}";
    return oss.str();
}

// ============================================================================
// HELPER METHODS
// ============================================================================

float MultiCameraParameterHandler::stringToFloat(const std::string& str) const
{
    return std::stof(str);
}

int32_t MultiCameraParameterHandler::stringToInt(const std::string& str) const
{
    return std::stoi(str);
}

bool MultiCameraParameterHandler::stringToBool(const std::string& str) const
{
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower == "true" || lower == "1" || lower == "yes" || lower == "on");
}

float MultiCameraParameterHandler::isoToAnalogGain(int32_t iso, const std::string& sensor) const
{
    if (iso <= 0)
        return 0.0f;

    float gain = static_cast<float>(iso) / 100.0f;

    if (sensor == Camera::IMX708)
        gain = std::clamp(gain, 1.0f, 8.57f);
    else if (sensor == Camera::IMX219)
        gain = std::clamp(gain, 1.0f, 10.66f);

    return gain;
}

int32_t MultiCameraParameterHandler::analogGainToIso(float gain, const std::string& /*sensor*/) const
{
    if (gain <= 0.0f)
        return 0;
    return static_cast<int32_t>(gain * 100.0f);
}

} // namespace sanuwave
