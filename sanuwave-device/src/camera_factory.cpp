// camera_factory.cpp
// Factory for creating camera instances based on detected sensor

#include "camera_factory.h"
#include "camera_imx219.h"
#include "camera_imx708.h"
#include "logger.h"
#include <algorithm>
#include <filesystem>
#include <unistd.h>

namespace sanuwave
{

namespace
{

// Returns the directory containing the running executable
std::filesystem::path getExeDir()
{
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len < 0)
        return {};
    buf[len] = '\0';
    return std::filesystem::path(buf).parent_path();
}


} // namespace

void CameraFactory::applyImx219TuningFile()
{
    static constexpr const char* TUNING_FILENAME = "imx219_noir_af.json";

    // Installed location (from .deb package)
    std::filesystem::path installedPath = "/usr/local/share/sanuwave/tuning/" + std::string(TUNING_FILENAME);

    // Fallback: next to executable (for development builds)
    std::filesystem::path devPath = getExeDir() / TUNING_FILENAME;

    std::filesystem::path tuningPath;
    if (std::filesystem::exists(installedPath))
        tuningPath = installedPath;
    else if (std::filesystem::exists(devPath))
        tuningPath = devPath;

    if (!tuningPath.empty())
    {
        LOG_INFO << "CameraFactory: Using IMX219 tuning file: " << tuningPath << std::endl;
        setenv("LIBCAMERA_RPI_TUNING_FILE", tuningPath.c_str(), 1);
    }
    else
    {
        LOG_WARNING << "CameraFactory: IMX219 tuning file not found at "
                    << installedPath << " or " << devPath
                    << ", using libcamera default" << std::endl;
    }
}


std::string CameraFactory::detectSensorType(libcamera::CameraManager* manager, int index)
{
    if (!manager)
    {
        LOG_ERROR << "CameraFactory: Null camera manager" << std::endl;
        return "unknown";
    }

    if (manager->cameras().empty())
    {
        LOG_ERROR << "CameraFactory: No cameras detected" << std::endl;
        return "none";
    }

    if (index >= static_cast<int>(manager->cameras().size()))
    {
        LOG_ERROR << "CameraFactory: Camera index out of range: " << index << std::endl;
        return "invalid_index";
    }

    std::shared_ptr<libcamera::Camera> camera = manager->cameras()[index];
    std::string cameraId = camera->id();
    
    // Convert to lowercase for case-insensitive comparison
    std::string lowerCameraId = cameraId;
    std::transform(lowerCameraId.begin(), lowerCameraId.end(), lowerCameraId.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    // Detect sensor type from camera ID
    if (lowerCameraId.find("imx708") != std::string::npos)
    {
        return "imx708";
    }
    else if (lowerCameraId.find("imx219") != std::string::npos)
    {
        return "imx219";
    }
    
    LOG_WARNING << "CameraFactory: Unknown sensor type for camera: " << cameraId << std::endl;
    return "unknown";
}

std::unique_ptr<CameraBase> CameraFactory::create(libcamera::CameraManager* manager, int index)
{
    std::string sensorType = detectSensorType(manager, index);
    
    std::unique_ptr<CameraBase> camera;
    
    if (sensorType == "imx708")
    {
        LOG_INFO << "CameraFactory: Creating IMX708 camera instance" << std::endl;
        camera = std::make_unique<CameraIMX708>();
    }
    else if (sensorType == "imx219")
    {
        LOG_INFO << "CameraFactory: Creating IMX219 camera instance" << std::endl;
        camera = std::make_unique<CameraIMX219>();
    }
    else
    {
        LOG_ERROR << "CameraFactory: Cannot create camera for sensor type: " << sensorType << std::endl;
        return nullptr;
    }
    
    // Initialize the camera
    if (camera && !camera->init(manager, index))
    {
        LOG_ERROR << "CameraFactory: Failed to initialize camera" << std::endl;
        return nullptr;
    }
    
    return camera;
}

} // namespace sanuwave
