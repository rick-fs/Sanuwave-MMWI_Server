// multi_camera_parameter_handler.h
#ifndef MULTI_CAMERA_PARAMETER_HANDLER_H
#define MULTI_CAMERA_PARAMETER_HANDLER_H

#include <string>
#include <cstdint>
#include "camera_base.h"
#include "vl53l4cd_wrapper.h"

namespace sanuwave
{

class ThermalCamera;

// ============================================================================
// RGB Camera Parameters
// ============================================================================
struct RgbCameraParams
{
    int32_t exposureTime_us = 10000;
    bool autoExposure = true;
    float evCompensation = 0.0f;
    bool autoAnalogGain = true;
    float analogGain = 1.0f;
    float digitalGain = 1.0f;
    int32_t iso = 0;
    bool autoWhiteBalance = true;
    std::string awbMode = "auto";
    float redGain = 1.0f;
    float blueGain = 1.0f;
    bool autoFocus = true;
    float lensPosition = 0.0f;
    int32_t brightness = 0;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float sharpness = 1.0f;
    std::string denoiseMode = "auto";
    bool hdrMode = false;
    bool rawMode = false;
    int rawBitDepth = 10;
    bool frameDurationEnabled = false;
    int64_t frameDuration_us = 0;
    void reset();
    std::string toJson() const;
};

// ============================================================================
// Arducam Camera Parameters
// ============================================================================
struct ArducamCameraParams
{
    int32_t exposureTime_us = 10000;
    bool autoExposure = true;
    float evCompensation = 0.0f;
    bool autoAnalogGain = true;
    float analogGain = 1.0f;
    float digitalGain = 1.0f;
    int32_t iso = 0;
    bool autoWhiteBalance = true;
    std::string awbMode = "auto";
    float redGain = 1.0f;
    float blueGain = 1.0f;
    bool autoFocus = false;
    float lensPosition = 0.0f;
    std::string denoiseMode = "auto";
    std::string binningMode = "none";
    bool rawMode = false;
    int rawBitDepth = 10;
    bool frameDurationEnabled = false;
    int64_t frameDuration_us = 0;
    void reset();
    std::string toJson() const;
};

// ============================================================================
// Thermal Camera Parameters
// ============================================================================
struct ThermalCameraParams
{
    float emissivity = 0.95f;
    float reflectedTemp_C = 23.0f;
    bool autoRange = false;
    float minTemp_C = 20.0f;
    float maxTemp_C = 40.0f;
    std::string colormap = "ironbow";
    bool applyNUC = true;
    float smoothingFactor = 0.0f;
    bool enableRoi = false;
    int32_t roiX = 0;
    int32_t roiY = 0;
    int32_t roiWidth = 32;
    int32_t roiHeight = 24;
    bool enableTempAlarm = false;
    float alarmThreshold_C = 38.0f;
    void reset();
    std::string toJson() const;
};

// ============================================================================
// Depth Sensor Parameters
// ============================================================================
struct DepthSensorParams
{
    int32_t rangingMode = 1;
    int32_t timingBudget_ms = 50;
    int32_t intermeasurementPeriod_ms = 100;
    int32_t sigmaThreshold_mm = 15;
    int32_t signalThreshold_kcps = 1024;
    int32_t offsetCalibration_mm = 0;
    int32_t crosstalkCalibration_kcps = 0;
    void reset();
    std::string toJson() const;
};

// ============================================================================
// Multi-Camera Parameter Handler
// ============================================================================
class MultiCameraParameterHandler
{
public:
    MultiCameraParameterHandler();

    bool setRgbParameter(const std::string& param, const std::string& value,
                         const std::string& mode = "capture");
    std::string getRgbParameters(const std::string& mode = "capture") const;
    CaptureSettings getRgbSettings(const std::string& mode = "capture") const;
    void resetRgbSettings(const std::string& mode = "capture");
    CaptureSettings getRgbCaptureSettings()    const { return getRgbSettings("capture"); }
    CaptureSettings getRgbStreamingSettings()  const { return getRgbSettings("streaming"); }

    bool setArducamParameter(const std::string& param, const std::string& value,
                             const std::string& mode = "capture");
    std::string getArducamParameters(const std::string& mode = "capture") const;
    CaptureSettings getArducamSettings(const std::string& mode = "capture") const;
    void resetArducamSettings(const std::string& mode = "capture");
    CaptureSettings getArducamCaptureSettings()   const { return getArducamSettings("capture"); }
    CaptureSettings getArducamStreamingSettings() const { return getArducamSettings("streaming"); }

    bool setThermalParameter(const std::string& param, const std::string& value,
                             const std::string& mode = "capture");
    std::string getThermalParameters(const std::string& mode = "capture") const;
    ThermalCameraParams getThermalSettings(const std::string& mode = "capture") const;
    void applyThermalSettings(ThermalCamera* camera, const std::string& mode = "capture") const;
    void resetThermalSettings(const std::string& mode = "capture");
    ThermalCameraParams getThermalCaptureSettings()    const { return getThermalSettings("capture"); }
    ThermalCameraParams getThermalStreamingSettings()  const { return getThermalSettings("streaming"); }

    bool setDepthParameter(const std::string& param, const std::string& value);
    std::string getDepthParameters() const;
    void applyDepthSettings(VL53L4CDWrapper* sensor) const;
    void resetDepthSettings();

    bool setParameter(const std::string& camera, const std::string& param,
                      const std::string& value, const std::string& mode = "capture");
    std::string getParameters(const std::string& camera,
                              const std::string& mode = "capture") const;
    void resetSettings(const std::string& camera, const std::string& mode = "capture");
    void resetAllSettings();
    std::string getAllParameters() const;

private:
    RgbCameraParams     rgbCaptureParams;
    RgbCameraParams     rgbStreamingParams;
    ArducamCameraParams arducamCaptureParams;
    ArducamCameraParams arducamStreamingParams;
    ThermalCameraParams thermalCaptureParams;
    ThermalCameraParams thermalStreamingParams;
    DepthSensorParams   depthParams;

    RgbCameraParams&       getRgbParamsRef(const std::string& mode);
    const RgbCameraParams& getRgbParamsRef(const std::string& mode) const;
    ArducamCameraParams&       getArducamParamsRef(const std::string& mode);
    const ArducamCameraParams& getArducamParamsRef(const std::string& mode) const;
    ThermalCameraParams&       getThermalParamsRef(const std::string& mode);
    const ThermalCameraParams& getThermalParamsRef(const std::string& mode) const;

    float   stringToFloat(const std::string& str) const;
    int32_t stringToInt(const std::string& str) const;
    bool    stringToBool(const std::string& str) const;
    float   isoToAnalogGain(int32_t iso, const std::string& sensor) const;
    int32_t analogGainToIso(float gain, const std::string& sensor) const;
};

} // namespace sanuwave

#endif // MULTI_CAMERA_PARAMETER_HANDLER_H
