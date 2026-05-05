// src/camera_manager.cpp
#include "camera_manager.h"
#include "logger.h"
#include <iostream>
#include <libcamera/camera.h>
#include "camera_factory.h"
namespace sanuwave
{

CameraManager::CameraManager()
    : started(false)
{
}

CameraManager::~CameraManager()
{
    stop();
}

CameraManager& CameraManager::getInstance()
{
    static CameraManager instance;
    return instance;
}

int CameraManager::start()
{
    if (started)
    {
        LOG_WARNING << "CameraManager already running" << std::endl;
        return 0;
    }
    
    LOG_INFO << "Starting CameraManager..." << std::endl;
    
    // Create camera manager
    manager = std::make_unique<libcamera::CameraManager>();
    
    // Start the camera manager
    int ret = manager->start();
    if (ret)
    {
        LOG_ERROR << "Failed to start CameraManager: " << ret << std::endl;
        manager.reset();
        return ret;
    }
    
    started = true;
    
    LOG_INFO << "CameraManager started successfully" << std::endl;
    LOG_INFO << "Available cameras: " << manager->cameras().size() << std::endl;
    auto cameras = manager->cameras();
    
    // List available cameras
    for (const auto& camera : cameras)
    {
        LOG_INFO << "  - " << camera->id() << std::endl;
    }
    
    return 0;
}

void CameraManager::stop()
{
    if (!started)
    {
        return;
    }
    
    LOG_INFO << "Stopping CameraManager..." << std::endl;
    
    if (manager)
    {
        manager->stop();
        manager.reset();
    }
    
    started = false;
    
    LOG_INFO << "CameraManager stopped" << std::endl;
}

int CameraManager::findCameraBySensorType(const std::string& sensorType)
{
    if (!manager)
    {
        return -1;
    }
    
    int numCameras = static_cast<int>(manager->cameras().size());
    
    for (int i = 0; i < numCameras; i++)
    {
        std::string detected = CameraFactory::detectSensorType(manager.get(), i);
        if (detected == sensorType)
        {
            return i;
        }
    }
    
    return -1;
}

std::unique_ptr<CameraBase> CameraManager::createCameraBySensorType(const std::string& sensorType)
{
    int index = CameraManager::findCameraBySensorType(sensorType);
    
    if (index < 0)
    {
        LOG_WARNING << "CameraManager: No camera found with sensor type: " << sensorType << std::endl;
        return nullptr;
    }
    
    LOG_INFO << "CameraManager: Found " << sensorType << " at index " << index << std::endl;
    return CameraFactory::create(manager.get(), index);
}

libcamera::CameraManager* CameraManager::getManager()
{
    if (!started || !manager)
    {
        LOG_ERROR << "CameraManager not running" << std::endl;
        return nullptr;
    }
    
    return manager.get();
}

} // namespace sanuwave