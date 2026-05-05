#ifndef CAMERA_IMX708_H
#define CAMERA_IMX708_H

#include "camera_base.h"

namespace sanuwave
{

class CameraIMX708 : public CameraBase
{
public:
    CameraIMX708() = default;
    virtual ~CameraIMX708() = default;
    void applyDirectDigitalGain(float gain) const override;
    // Sensor identification
    std::string getSensorType() const override;
    cv::Size getDefaultResolution() const override;
    
    // Parameter validation (IMX708-specific ranges)
    int32_t validateExposureTime(int32_t exposure_us) const override;
    float validateAnalogGain(float gain) const override;
    float validateDigitalGain(float gain) const override;
    
    // Specialized capture methods
    cv::Mat capture3D();
    
    // Capability queries
    bool isHdrSupported() const;
    int getNativeBitDepth() const override { return 10; }

protected:
    // Raw pixel format selection (IMX708 uses RGGB pattern)
    libcamera::PixelFormat getRawPixelFormat(int bitDepth) const override;
};

} // namespace sanuwave
#endif // CAMERA_IMX708_H