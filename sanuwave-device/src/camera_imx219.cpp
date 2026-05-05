// camera_imx219.cpp
// IMX219 sensor specific implementation

#include "camera_imx219.h"
#include "logger.h"
#include <algorithm>

namespace sanuwave
{

// ============================================================================
// SENSOR IDENTIFICATION
// ============================================================================

std::string CameraIMX219::getSensorType() const
{
    if (cameraId.find("imx219") != std::string::npos ||
        cameraId.find("IMX219") != std::string::npos)
    {
        return "imx219";
    }

    return "unknown";
}

cv::Size CameraIMX219::getDefaultResolution() const
{
    return cv::Size(3280, 2464); // 8MP
}

// ============================================================================
// PARAMETER VALIDATION - IMX219 SPECIFIC RANGES
// ============================================================================

int32_t CameraIMX219::validateExposureTime(int32_t exposure_us) const
{
    if (exposure_us <= 0) return 0; // Auto

    // IMX219: 4-1000000 µs typical range
    return std::clamp(exposure_us, 4, 1000000);
}

float CameraIMX219::validateAnalogGain(float gain) const
{
    if (gain <= 0.0f) return 0.0f; // Auto

    // IMX219: 1.0-10.66x (register 0-232)
    return std::clamp(gain, 1.0f, 10.66f);
}

float CameraIMX219::validateDigitalGain(float gain) const
{
    if (gain <= 0.0f) return 1.0f; // Minimum

    // IMX219: 1.0-16x (register 256-4095)
    return std::clamp(gain, 1.0f, 16.0f);
}

// ============================================================================
// RAW PIXEL FORMAT SELECTION
// ============================================================================

libcamera::PixelFormat CameraIMX219::getRawPixelFormat([[maybe_unused]] int bitDepth) const
{
    // IMX219 uses BGGR Bayer pattern
    LOG_INFO << "IMX219: Using SBGGR16 (16-bit container for 10-bit data)" << std::endl;
    return libcamera::formats::SBGGR16;
}

bool CameraIMX219::detectFocusActuator()
{
    if (!camera)
        return false;

    for (const auto& [id, info] : camera->controls())
    {
        if (id->name() == "LensPosition")
        {
            LOG_INFO << "IMX219: LensPosition control found — motorized focus available"
                     << " (range " << info.min().toString()
                     << " - " << info.max().toString() << ")" << std::endl;
            return true;
        }
    }
    LOG_INFO << "IMX219: No LensPosition control — fixed-focus module" << std::endl;
    return false;
}

bool CameraIMX219::init(libcamera::CameraManager* cameraManager, int index)
{
    if (!CameraBase::init(cameraManager, index))
        return false;

    hasFocusActuator = detectFocusActuator();
    return true;
}

// ============================================================================
// SPECIALIZED CAPTURE METHODS
// ============================================================================

cv::Mat CameraIMX219::captureRGB()
{
    CaptureSettings settings;
    settings.width = maxResolution.width;
    settings.height = maxResolution.height;
    settings.autoExposure = true;
    settings.autoWhiteBalance = true;

    return captureStill(settings, nullptr);
}

cv::Mat CameraIMX219::captureHighRes()
{
    CaptureSettings settings;
    settings.width = 3280;  // Full 8MP
    settings.height = 2464;
    settings.autoExposure = true;
    settings.autoWhiteBalance = true;

    return captureStill(settings, nullptr);
}

cv::Mat CameraIMX219::captureMediumRes()
{
    CaptureSettings settings;
    settings.width = 1920;  // 1080p
    settings.height = 1080;
    settings.autoExposure = true;
    settings.autoWhiteBalance = true;

    return captureStill(settings, nullptr);
}

cv::Mat CameraIMX219::captureBinned()
{
    CaptureSettings settings;
    settings.width = 1640;  // 2x2 binned
    settings.height = 1232;
    settings.autoExposure = true;
    settings.autoWhiteBalance = true;

    return captureStill(settings, nullptr);
}

void CameraIMX219::applyFocusControls([[maybe_unused]] libcamera::ControlList &controls,
                                      const CaptureSettings &settings) const
{
    if (!hasFocusActuator)
    {
        if (settings.autoFocus)
            LOG_WARNING << "IMX219: AutoFocus requested but no focus actuator detected — ignoring" << std::endl;
        if (settings.lensPosition != 0.0f)
            LOG_WARNING << "IMX219: LensPosition " << settings.lensPosition
                        << " requested but no focus actuator — ignoring" << std::endl;
        return;
    }

    // Motorized variant (e.g. B0190) — use base class VCM path
    CameraBase::applyFocusControls(controls, settings);
}

void CameraIMX219::applyExposureControls(libcamera::ControlList &controls,
                                         const CaptureSettings &settings) const
{
    if (!settings.autoExposure)
    {
        controls.set(libcamera::controls::AeEnable, false);
        controls.set(libcamera::controls::ExposureTimeMode,
                     libcamera::controls::ExposureTimeModeManual);

        if (settings.exposureTime_us > 0)
        {
            int32_t validated = validateExposureTime(settings.exposureTime_us);
            LOG_INFO << "IMX219: Set ExposureTime to " << validated
                     << " us (manual mode)" << std::endl;
            controls.set(libcamera::controls::ExposureTime, validated);
        }
        else
        {
            int32_t defaultExposure = getDefaultExposureTime();
            controls.set(libcamera::controls::ExposureTime, defaultExposure);
        }
    }
    else
    {
        controls.set(libcamera::controls::AeEnable, true);
        controls.set(libcamera::controls::ExposureTimeMode,
                     libcamera::controls::ExposureTimeModeAuto);

        if (settings.exposureTime_us > 0)
        {
            int32_t validated = validateExposureTime(settings.exposureTime_us);
            controls.set(libcamera::controls::ExposureTime, validated);
        }

        if (std::abs(settings.evCompensation) > 0.01f)
        {
            float evClamped = std::clamp(settings.evCompensation, -2.0f, 2.0f);
            controls.set(libcamera::controls::ExposureValue, evClamped);
            LOG_INFO << "IMX219: EV compensation = " << evClamped << std::endl;
        }
    }
}

void CameraIMX219::applyGainControls(libcamera::ControlList &/*controls*/,
                                     const CaptureSettings &settings) const
{
    // --- Analog gain ---
    // The RPi IPA owns AnalogueGain when AeEnable=true, and may override it
    // even when AeEnable=false depending on IPA version. Bypass the IPA
    // entirely by writing directly to the sensor register via V4L2,
    // same approach as digital gain.
    //
    // IMX219 kernel driver (imx219.c) register encoding:
    //   IMX219_REG_ANALOG_GAIN (0x0157), 8-bit, range 0-232
    //   Gain = 256 / (256 - register_value)
    //   => register_value = 256 - (256 / gain)
    //   e.g. 1x -> reg 0, 2x -> reg 128, ~10.66x -> reg 232

    if (!settings.autoAnalogGain)
    {
        float validated = validateAnalogGain(settings.analogGain);
        int32_t analogReg = static_cast<int32_t>(256.0f - (256.0f / validated));
        analogReg = std::clamp(analogReg, 0, 232);

        LOG_INFO << "IMX219: Setting analog gain " << validated
                 << "x (register=" << analogReg << ") via V4L2" << std::endl;

        auto result = timingHelper.setAnalogGain(analogReg);
        if (!result)
            LOG_WARNING << "IMX219: Failed to set analog gain: "
                        << timingHelper.getLastError() << std::endl;
    }
    else
    {
        // Auto — let the IPA manage it. AeEnable=true (set in
        // applyExposureControls) already hands both exposure and gain
        // to the IPA, so nothing to do here.
        LOG_TRACE << "IMX219: Analog gain set to auto (IPA managed)" << std::endl;
    }

    // --- Digital gain ---
    // IMX219_REG_DIGITAL_GAIN (0x0158), 16-bit, range 0x0100-0x0fff
    // Encoding: 256 = 1x, 512 = 2x, etc.
    // IPA intercepts the libcamera DigitalGain control on PiSP, so write
    // directly to the sensor register via V4L2.
    float validatedDigital = validateDigitalGain(settings.digitalGain);
    int32_t digitalReg = std::clamp(
        static_cast<int32_t>(validatedDigital * 256.0f), 256, 4095);

    LOG_INFO << "IMX219: Setting digital gain " << validatedDigital
             << "x (register=" << digitalReg << ") via V4L2" << std::endl;

    auto result = timingHelper.setDigitalGain(digitalReg);
    if (!result)
        LOG_WARNING << "IMX219: Failed to set digital gain: "
                    << timingHelper.getLastError() << std::endl;
}

cv::Mat CameraIMX219::captureRAW()
{
    CaptureSettings settings;
    settings.width = maxResolution.width;
    settings.height = maxResolution.height;
    settings.rawMode = true;
    settings.rawBitDepth = 10;
    settings.autoExposure = true;
    settings.autoWhiteBalance = true;

    return captureStill(settings, nullptr);
}

// ============================================================================
// CAPABILITY QUERY METHODS
// ============================================================================

std::vector<std::string> CameraIMX219::getBinningModes() const
{
    // IMX219 specific binning modes
    return {
        "none",
        "2x2_normal",
        "2x2_special"
    };
}

} // namespace sanuwave
