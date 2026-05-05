// include/camera_manager.h
#pragma once

#include <libcamera/libcamera.h>
#include <memory>
#include "camera_base.h"
namespace sanuwave
{
    /**
     * Singleton manager for libcamera CameraManager
     * Handles initialization and cleanup of the camera system
     */
    class CameraManager
    {
    public:
        // Get singleton instance
        static CameraManager& getInstance();
        
        // No copy/move
        CameraManager(const CameraManager&) = delete;
        CameraManager& operator=(const CameraManager&) = delete;
        CameraManager(CameraManager&&) = delete;
        CameraManager& operator=(CameraManager&&) = delete;
        
        /**
         * Start the camera manager
         * @return 0 on success, error code otherwise
         */
        int start();
        
        /**
         * Stop the camera manager
         */
        void stop();
        
        /**
         * Get the libcamera CameraManager pointer
         * @return Pointer to CameraManager, or nullptr if not started
         */
        libcamera::CameraManager* getManager();
        
        /**
         * Check if manager is running
         */
        bool isStarted() const { return started; }
        
        // Find camera index by sensor type, returns -1 if not found
        int findCameraBySensorType(const std::string& sensorType);

        std::unique_ptr<CameraBase> createCameraBySensorType(const std::string &sensorType);

      private:
        CameraManager();
        ~CameraManager();
        
        std::unique_ptr<libcamera::CameraManager> manager;
        bool started;
    };
}