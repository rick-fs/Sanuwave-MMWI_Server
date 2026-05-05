#ifndef CAMERA_IMX219_H
#define CAMERA_IMX219_H

#include "camera_base.h"

namespace sanuwave
{

class CameraIMX219 : public CameraBase
{
public:
    CameraIMX219() = default;
    virtual ~CameraIMX219() = default;
    
    // Sensor identification
    std::string getSensorType() const override;
    cv::Size getDefaultResolution() const override;
    
    // Parameter validation (IMX219-specific ranges)
    int32_t validateExposureTime(int32_t exposure_us) const override;
    float validateAnalogGain(float gain) const override;
    float validateDigitalGain(float gain) const override;
    
    // Specialized capture methods
    cv::Mat captureRGB();
    cv::Mat captureHighRes();
    cv::Mat captureMediumRes();
    cv::Mat captureBinned();
    cv::Mat captureRAW();


    void applyFocusControls(libcamera::ControlList &controls, 
                        const CaptureSettings &settings) const override;
    void applyExposureControls(libcamera::ControlList &controls, 
                           const CaptureSettings &settings) const override;
    void applyGainControls(libcamera::ControlList &controls,
                       const CaptureSettings &settings) const override;
    // Capability queries
    std::vector<std::string> getBinningModes() const;
    int getNativeBitDepth() const override { return 10; }

protected:
    libcamera::PixelFormat getRawPixelFormat(int bitDepth) const override;
    bool detectFocusActuator();

    bool init(libcamera::CameraManager *cameraManager, int index) override;
  private:
    bool hasFocusActuator = false;
};

} // namespace sanuwave
#endif // CAMERA_IMX219_H