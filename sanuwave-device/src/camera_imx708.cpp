// camera_imx708.cpp
// IMX708 sensor specific implementation

#include "camera_imx708.h"
#include "logger.h"
#include <algorithm>

namespace sanuwave
{

// ============================================================================
// SENSOR IDENTIFICATION
// ============================================================================

std::string CameraIMX708::getSensorType() const
{
    if (cameraId.find("imx708") != std::string::npos ||
        cameraId.find("IMX708") != std::string::npos)
    {
        return "imx708";
    }
    else if (cameraId.find("imx219") != std::string::npos ||
             cameraId.find("IMX219") != std::string::npos)
    {
        return "imx219";
    }
    
    return "unknown";
}

cv::Size CameraIMX708::getDefaultResolution() const
{
    return cv::Size(4608, 2592); // 12MP
}

// ============================================================================
// PARAMETER VALIDATION - IMX708 SPECIFIC RANGES
// ============================================================================

int32_t CameraIMX708::validateExposureTime(int32_t exposure_us) const
{
    if (exposure_us <= 0) return 0; // Auto
    
    // IMX708: 1-1000000 µs typical range
    return std::clamp(exposure_us, 1, 1000000);
}

float CameraIMX708::validateAnalogGain(float gain) const
{
    if (gain <= 0.0f) return 0.0f; // Auto
    
    // IMX708: 1.0-8.57x (register 112-960)
    return std::clamp(gain, 1.0f, 8.57f);
}

float CameraIMX708::validateDigitalGain(float gain) const
{
    if (gain <= 0.0f) return 1.0f; // Minimum
    
    // IMX708: 1.0-256x (register 256-65535)
    return std::clamp(gain, 1.0f, 256.0f);
}

// ============================================================================
// RAW PIXEL FORMAT SELECTION
// ============================================================================

libcamera::PixelFormat CameraIMX708::getRawPixelFormat([[maybe_unused]] int bitDepth) const
{
    LOG_INFO << "IMX708: Using SBGGR10 (10-bit packed)" << std::endl;
    return libcamera::formats::SBGGR10;
}

void CameraIMX708::applyDirectDigitalGain(float gain) const
{
    if (!timingHelper.isOpen())
        return;

    float validated = validateDigitalGain(gain);
    int32_t regValue = static_cast<int32_t>(validated * 256.0f);
    regValue = std::max(regValue, 256);

    auto result = const_cast<SensorInfo&>(timingHelper)
    .setDigitalGain(regValue);

    if (result)
    {
        LOG_INFO << "IMX708: Direct V4L2 digital gain: " << validated
                 << "x (register=" << *result << ")" << std::endl;
    }
    else
    {
        LOG_WARNING << "IMX708: Failed to set V4L2 digital gain" << std::endl;
    }
}

// ============================================================================
// SPECIALIZED CAPTURE METHODS
// ============================================================================

cv::Mat CameraIMX708::capture3D()
{
    CaptureSettings settings;
    settings.width = maxResolution.width;
    settings.height = maxResolution.height;
    settings.autoExposure = true;
    settings.autoWhiteBalance = true;
    settings.autoFocus = true;
    
    return captureStill(settings, nullptr);
}

// ============================================================================
// CAPABILITY QUERY METHODS
// ============================================================================

bool CameraIMX708::isHdrSupported() const
{
    // IMX708 supports HDR mode
    return getSensorType() == "imx708";
}

} // namespace sanuwave
