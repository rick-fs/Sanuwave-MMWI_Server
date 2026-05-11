// src/main.cpp
#include "I2cMgr.h"
#include "as7331_wrapper.h"
#include "vd6283tx_wrapper.h"
#include "camera_base.h"
#include "camera_factory.h"
#include "camera_imx219.h"
#include "camera_imx708.h"
#include "camera_manager.h"
#include "command_handler.h"
#include "logger.h"
#include "lsm6ds3trc_wrapper.h"
#include "multi_camera_parameter_handler.h"
#include "path_utils.h"
#include "tcp_server.h"
#include "thermal_camera.h"
#include "vl53l4cd_wrapper.h"
#include "version.h"
#include "camera_types.h"
#include "protocol_constants.h"
#include "led_gpio_controller.h"
#include "temp_monitor.h"
#include "PCA9545A.h"
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <signal.h>
#include <thread>
#include "LedMgr.h"

#define NO_TIMESTAMP
#define REDIRECT_CERR
// Global flag for graceful shutdown
std::atomic<bool> g_running(true);

void signalHandler(int signal)
{
    LOG_INFO << "Shutdown signal received " << signal << std::endl;
    g_running = false;
}



int main()
{
    try
    {
        // Setup signal handlers
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);

        // Initialize logger
        std::string logPath = PathUtils::getLogDirectory("sanuwave", "/var/log/sanuwave");
#ifdef NO_TIMESTAMP
        std::string fullLogPath =
            Logger::getInstance().setLogFileWithNoTimestamp(logPath, "sanuwave_server");
#else
        std::string fullLogPath =
            Logger::getInstance().setLogFileWithTimestamp(logPath, "sanuwave_server");
#endif
        std::cout << fullLogPath << std::endl;
        Logger::getInstance().setLogLevel(LogLevel::DEBUG);
#ifdef REDIRECT_CERR
        // Redirect cerr to the log file so driver output is captured
        Logger::getInstance().redirectCerrToLog();

        std::cerr << "cerr redirect test" << std::endl;
#endif
        LOG_INFO << "=== Sanuwave Medical Imaging Server ===" << std::endl;
        LOG_INFO << SANUWAVE_VERSION_STRING << std::endl;
        LOG_INFO << "OpenCV Version: " << CV_VERSION << std::endl;

        // ================================================================
        // Initialize shared I2C bus (used by all I2C sensors)
        // ================================================================
        LOG_INFO << "" << std::endl;
        LOG_INFO << "Opening shared I2C bus 1..." << std::endl;
        if (!I2cMgr::getInstance().open())
        {
            LOG_ERROR << "Failed to open I2C bus 1" << std::endl;
            return EXIT_FAILURE;
        }
   
        sanuwave::CameraFactory::applyImx219TuningFile();

        // Initialize Camera Manager
        sanuwave::CameraManager &cameraManager = sanuwave::CameraManager::getInstance();

        int ret = cameraManager.start();
        if (ret)
        {
            LOG_ERROR << "Failed to start camera manager: " << ret << std::endl;
            return EXIT_FAILURE;
        }

        int numCameras = 0;
        if (cameraManager.getManager())
        {
            numCameras = cameraManager.getManager()->cameras().size();
            LOG_INFO << "Detected " << numCameras << " camera(s)" << std::endl;
        }

        LOG_INFO << "Initializing primary RGB camera..." << std::endl;
        std::unique_ptr<sanuwave::CameraBase> rgbCamera =
            cameraManager.createCameraBySensorType(sanuwave::SensorType::IMX708);

        if (!rgbCamera)
        {
            LOG_ERROR << "Failed to create RGB camera" << std::endl;
            return EXIT_FAILURE;
        }

        LOG_INFO << "RGB Camera: " << rgbCamera->getCameraId() << std::endl;
        LOG_INFO << "  Sensor: " << rgbCamera->getSensorType() << std::endl;
        LOG_INFO << "  Max resolution: " << rgbCamera->getMaxResolution().width << "x"
                 << rgbCamera->getMaxResolution().height << std::endl;

        if (sanuwave::CameraIMX708 *imx708 = dynamic_cast<sanuwave::CameraIMX708 *>(rgbCamera.get()))
        {
            LOG_INFO << "  HDR supported: " << (imx708->isHdrSupported() ? "Yes" : "No")
                     << std::endl;
        }

        std::unique_ptr<sanuwave::CameraBase> arducamCamera =
            cameraManager.createCameraBySensorType(sanuwave::SensorType::IMX219);

        if (arducamCamera)
        {
            LOG_INFO << "Secondary Camera: " << arducamCamera->getCameraId() << std::endl;
            LOG_INFO << "  Sensor: " << arducamCamera->getSensorType() << std::endl;
            LOG_INFO << "  Max resolution: " << arducamCamera->getMaxResolution().width << "x"
                     << arducamCamera->getMaxResolution().height << std::endl;
        }
        else
        {
            LOG_WARNING << "Secondary camera initialization failed" << std::endl;
            LOG_INFO << "Continuing without secondary camera..." << std::endl;
        }

        // ================================================================
        // Initialize Thermal Camera (Lepton 3 via I2C + SPI)
        // ================================================================
        LOG_INFO << "Initializing thermal camera (Lepton 3)..." << std::endl;
        sanuwave::ThermalCamera thermalCamera;
        bool thermalAvailable = false;

        if (!thermalCamera.init(1, "/dev/spidev0.0"))
        {
            LOG_WARNING << "Thermal camera not available: " << thermalCamera.getLastError()
                        << std::endl;
            LOG_WARNING << "Continuing without thermal imaging..." << std::endl;
        }
        else
        {
            LOG_INFO << "Thermal camera initialized successfully (160x120)" << std::endl;
            thermalAvailable = true;
        }

        // ================================================================
        // Initialize I2C sensors
        // ================================================================

        // Distance Sensor (VL53L4CD)
        LOG_INFO << "Checking for VL53L4CD distance sensor on I2C bus 1..." << std::endl;
        sanuwave::VL53L4CDWrapper distanceSensor;
        bool distanceAvailable = false;

        LOG_INFO << "VL53L4CD distance sensor ready" << std::endl;
        if (distanceSensor.init())
        {
            LOG_INFO << "VL53L4CD distance sensor initialized" << std::endl;
            distanceAvailable = true;
        }
        else
        {
            LOG_WARNING << "VL53L4CD distance sensor initialization failed" << std::endl;
            LOG_WARNING << "Continuing without distance sensing..." << std::endl;
        }

        // UV Sensor (AS7331 at 0x74)
        LOG_INFO << "Checking for AS7331 UV sensor on I2C bus 1 (0x74)..." << std::endl;
        sanuwave::AS7331Wrapper uvSensor;
        LOG_INFO << "AS7331 UV sensor ready (will be initialized on demand)" << std::endl;

        // ALS Sensor (VD6283TX at 0x20, behind mux 0x70 channel 0)
        LOG_INFO << "Checking for VD6283TX ALS sensor (mux 0x70 ch0)..." << std::endl;
        PCA9545A mux0x70(&I2cMgr::getInstance(), PCA9545A::DEFAULT_ADDRESS);
        mux0x70.init();
        sanuwave::VD6283TXWrapper alsSensor(&mux0x70, 0);
        LOG_INFO << "VD6283TX ALS sensor ready (will be initialized on demand)" << std::endl;

        // IMU Sensor (LSM6DS3TR-C at 0x6A on I2C bus 1)
        // Probe at startup so the part is in a known-good state before any
        // client connects. If init fails the wrapper stays unconfigured;
        // a subsequent imu_init from the client will retry. Streaming
        // does not begin until the client sends imu_start.
        LOG_INFO << "Checking for LSM6DS3TR-C IMU on I2C bus 1 (0x6A)..." << std::endl;
        sanuwave::Lsm6ds3trcWrapper imuSensor;
        if (imuSensor.init())
        {
            LOG_INFO << "LSM6DS3TR-C IMU initialized" << std::endl;
        }
        else
        {
            LOG_WARNING << "LSM6DS3TR-C IMU initialization failed: "
                        << imuSensor.getLastError() << std::endl;
            LOG_WARNING << "Continuing without IMU (client may retry via imu_init)..." << std::endl;
        }

        // LED Manager
        LOG_INFO << "Initializing LED manager..." << std::endl;
        LedMgr& ledMgr = LedMgr::getInstance();
        bool ledAvailable = false;

        if (ledMgr.initialize())
        {
            LOG_INFO << "LED manager initialized (32 LEDs)" << std::endl;
            ledAvailable = true;
        }
        else
        {
            LOG_WARNING << "LED manager initialization failed" << std::endl;
            LOG_WARNING << "Continuing without LED control..." << std::endl;
        }

        // Initialize LED GPIO controller
        sanuwave::LedGpioController ledGpioController;
        bool ledGpioAvailable = ledGpioController.initialize();
        if (ledGpioAvailable)
            LOG_INFO << "LED GPIO controller initialized" << std::endl;
        else
            LOG_WARNING << "LED GPIO controller not available" << std::endl;

        LOG_INFO << "Initializing sensor temperature monitor..." << std::endl;
        std::unique_ptr<sanuwave::TemperatureMonitor> tempMonitor;
        sanuwave::SensorInfo& si = rgbCamera->getSensorInfo();
        if (si.isOpen())
        {
            tempMonitor = std::make_unique<sanuwave::TemperatureMonitor>(si);
            tempMonitor->start();
            LOG_INFO << "Temperature monitor started" << std::endl;
        }
        else
        {
            LOG_WARNING << "SensorInfo not open, temperature monitoring unavailable" << std::endl;
        }

        // ================================================================
        // Initialize parameter handler and command handler
        // ================================================================
        LOG_INFO << "" << std::endl;
        LOG_INFO << "Initializing multi-camera parameter system..." << std::endl;
        std::unique_ptr<sanuwave::MultiCameraParameterHandler> multiParams =
            std::make_unique<sanuwave::MultiCameraParameterHandler>();
        LOG_INFO << "Multi-camera parameter system initialized" << std::endl;

        LOG_INFO << "" << std::endl;
        LOG_INFO << "Creating command handler..." << std::endl;
        sanuwave::CommandHandler commandHandler(
            rgbCamera.get(), &thermalCamera, arducamCamera.get(),
            distanceAvailable ? &distanceSensor : nullptr,
            &uvSensor, &alsSensor,
            &imuSensor,
            ledAvailable ? &ledMgr : nullptr,
            &ledGpioController,
            tempMonitor.get(),
            std::move(multiParams)
        );

        // ================================================================
        // Create and configure TCP server
        // ================================================================
        LOG_INFO << "Starting TCP server on port 8080..." << std::endl;
        sanuwave::TCPServer server(8080);

        commandHandler.setStreamFrameCallback(
            [&server](const std::vector<uint8_t>& frameData,
                      const sanuwave::StreamFrameMeta& meta)
            { server.broadcastStreamFrame(frameData, meta); });

        LOG_INFO << "Stream frame callback configured" << std::endl;

        commandHandler.setDiagFrameCallback(
            [&server](const std::string& headerJson, const uint8_t* data, size_t dataSize)
            {
                LOG_DEBUG << "Sending diagnostic frame: " << headerJson
                          << ", data size: " << dataSize << std::endl;
                server.sendDiagFrame(headerJson, data, dataSize);
            });
        LOG_INFO << "Diagnostic frame callback configured" << std::endl;

        // ── UVBF: unsolicited JSON notifications (progress, errors, complete) ──
        commandHandler.setNotifyCallback(
            [&server](const std::string& json)
            {
                server.sendJsonNotification(json);
            });

        // ── UVBF: binary DNG frame transfers ────────────────────────────────
        commandHandler.setSendDngCallback(
            [&server](const std::string& headerJson,
                      const uint8_t*    data,
                      size_t            dataSize)
            {
                LOG_DEBUG << "Sending UVBF DNG frame: " << headerJson
                          << " (" << dataSize << " bytes)" << std::endl;
                server.sendDiagFrame(headerJson, data, dataSize);
            });

        // ── IMU: unsolicited JSON notifications (sample batches, events) ───
        // Same shape as UVBF notifications above. The IMU wrapper builds
        // the JSON line on its worker thread and hands it to this lambda;
        // sendJsonNotification broadcasts to all connected clients.
        imuSensor.setNotifyCallback(
            [&server](const std::string& json)
            {
                server.sendJsonNotification(json);
            });
        LOG_INFO << "IMU notify callback configured" << std::endl;

        server.setCommandCallback(
            [&](const std::string &command) -> std::string
            {
                LOG_DEBUG << "Handle command " << command << std::endl;
                std::string response = commandHandler.handleCommand(command);
                LOG_DEBUG << "Response: " << response << std::endl;

                if (response.find("ready_to_send") != std::string::npos)
                {
                    auto imageData = commandHandler.getLastImageData();
                    auto modality  = commandHandler.getLastImageModality();

                    if (!imageData.empty())
                    {
                        LOG_DEBUG << "Sending " << modality << " image ("
                                  << imageData.size() << " bytes)" << std::endl;
                        server.sendImage(imageData, modality);
                        commandHandler.clearImageData();
                    }
                }
                return response;
            });

        server.setDisconnectCallback([&commandHandler]()
        {
            commandHandler.onClientDisconnect();
        });

        if (!server.start())
        {
            LOG_ERROR << "Failed to start TCP server" << std::endl;
            return EXIT_FAILURE;
        }

        LOG_INFO << "Server started successfully on port 8080" << std::endl;
        LOG_INFO << "Waiting for client connections..." << std::endl;
        LOG_INFO << "" << std::endl;
        LOG_INFO << "Press Ctrl+C to stop" << std::endl;
        LOG_INFO << "" << std::endl;

        // ================================================================
        // Main loop
        // ================================================================
        int lastClientCount = 0;
        while (g_running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            int clientCount = server.getClientCount();
            if (clientCount != lastClientCount)
            {
                if (clientCount > 0)
                    LOG_INFO << "Active clients: " << clientCount << std::endl;
                else
                    LOG_INFO << "No active clients" << std::endl;
                lastClientCount = clientCount;
            }

            if (commandHandler.isDistanceRanging())
                LOG_TRACE << "Distance sensor streaming active" << std::endl;

            if (commandHandler.isStreaming())
                LOG_TRACE << "Video streaming active: "
                          << commandHandler.getStreamingModality() << std::endl;

            if (imuSensor.isStreaming())
                LOG_TRACE << "IMU streaming active" << std::endl;
        }

        // ================================================================
        // Shutdown (reverse order of initialization)
        // ================================================================
        LOG_INFO << "" << std::endl;
        LOG_INFO << "=== Shutting Down ===" << std::endl;

        if (distanceSensor.isInitialized())
        {
            if (distanceSensor.isRanging())
            {
                LOG_INFO << "Stopping distance sensor..." << std::endl;
                distanceSensor.stopRanging();
            }
            LOG_INFO << "Shutting down distance sensor..." << std::endl;
            distanceSensor.shutdown();
        }

        if (uvSensor.isInitialized())
        {
            LOG_INFO << "Shutting down UV sensor..." << std::endl;
            uvSensor.shutdown();
        }

        if (alsSensor.isInitialized())
        {
            if (alsSensor.isStreaming())
            {
                LOG_INFO << "Stopping ALS sensor streaming..." << std::endl;
                alsSensor.stopStreaming();
            }
            LOG_INFO << "Shutting down ALS sensor..." << std::endl;
            alsSensor.shutdown();
        }

        // IMU: stop the worker thread before tearing down the TCP server,
        // so the worker can't try to push to a half-closed server.
        if (imuSensor.isStreaming())
        {
            LOG_INFO << "Stopping IMU streaming..." << std::endl;
            imuSensor.stop();
        }
        // Drop the notify callback so the wrapper holds no reference to
        // `server` once we start tearing it down. (Belt and braces — stop()
        // already joined the worker, so there should be no pending sends.)
        imuSensor.setNotifyCallback(nullptr);

        if (ledAvailable && ledMgr.isInitialized())
        {
            LOG_INFO << "Shutting down LED manager..." << std::endl;
            for (int i = 0; i < 32; ++i)
                ledMgr.turnOff(static_cast<LedMgr::LedId>(i));
        }

        if (tempMonitor)
        {
            LOG_INFO << "Stopping temperature monitor..." << std::endl;
            tempMonitor->stop();
        }

        LOG_INFO << "Stopping TCP server..." << std::endl;
        server.stop();

        if (thermalAvailable && thermalCamera.isReady())
        {
            LOG_INFO << "Shutting down thermal camera..." << std::endl;
            thermalCamera.shutdown();
        }

        LOG_INFO << "Shutting down cameras..." << std::endl;

        LOG_INFO << "Closing shared I2C bus..." << std::endl;
        I2cMgr::getInstance().close();

        LOG_INFO << "" << std::endl;
        LOG_INFO << "Server stopped. Goodbye!" << std::endl;
    }
    catch (const std::exception &ex)
    {
        LOG_ERROR << "Fatal exception: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
