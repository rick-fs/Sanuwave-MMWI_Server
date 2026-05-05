#ifndef CAMERA_FACTORY_H
#define CAMERA_FACTORY_H

#include "camera_base.h"
#include <memory>
#include <libcamera/libcamera.h>

namespace sanuwave
{

class CameraFactory
{
public:
    // Factory method to create appropriate camera based on detected sensor
    static std::unique_ptr<CameraBase> create(libcamera::CameraManager* manager, int index = 0);
    
    // Get sensor type without creating camera instance
    static std::string detectSensorType(libcamera::CameraManager* manager, int index = 0);
    
    static void applyImx219TuningFile();  

private:
    CameraFactory() = delete;  // Prevent instantiation
};

} // namespace sanuwave

#endif // CAMERA_FACTORY_H
