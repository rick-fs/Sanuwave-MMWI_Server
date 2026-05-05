// include/thermal_camera.h
#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <functional>
#include <memory>
#include <array>

// Forward declaration
class Lepton3;

namespace sanuwave
{
    class ThermalCamera
    {
    public:
        // Lepton 3 specifications
        static constexpr int WIDTH = 160;
        static constexpr int HEIGHT = 120;
        
        ThermalCamera();
        ~ThermalCamera();
        
        // No copy/move
        ThermalCamera(const ThermalCamera&) = delete;
        ThermalCamera& operator=(const ThermalCamera&) = delete;
        
        // Initialize with I2C bus and SPI device
        bool init(int i2cBus, const std::string& spiDevice);
        
        // Legacy init - parses "bus:spidev" format or uses defaults
        bool init(const std::string& device = "");
        
        void shutdown();
        
        cv::Mat captureRaw();
        cv::Mat captureTemperatureMap();
        cv::Mat captureThermalVisualization(int scale = 4);
        
        float getPixelTemperature(int x, int y);
        
        struct ThermalStats
        {
            float minTemp;
            float maxTemp;
            float avgTemp;
            cv::Point minLoc;
            cv::Point maxLoc;
        };
        
        ThermalStats getStats();
        
        bool isReady() const { return initialized; }
        
        void setErrorCallback(std::function<void(const std::string&)> callback)
        {
            errorCallback = callback;
        }
        
        std::string getLastError() const { return lastError; }
        
        // ========== Settings API ==========
        
        // Emissivity (0.1 - 1.0, default 0.95)
        void setEmissivity(float emissivity);
        float getEmissivity() const { return settings.emissivity; }
        
        // Reflected temperature for emissivity compensation
        void setReflectedTemperature(float tempC);
        float getReflectedTemperature() const { return settings.reflectedTemp_C; }
        
        // Temperature display range
        void setTemperatureRange(float minC, float maxC);
        float getMinTemperature() const { return settings.minTemp_C; }
        float getMaxTemperature() const { return settings.maxTemp_C; }
        void setAutoRange(bool enabled);
        bool isAutoRangeEnabled() const { return settings.autoRange; }
        
        // Colormap selection
        enum class Colormap
        {
            IRONBOW,
            RAINBOW,
            GRAYSCALE,
            HOT,
            JET,
            INFERNO,
            PLASMA
        };
        void setColormap(Colormap colormap);
        void setColormap(const std::string& colormapName);
        Colormap getColormap() const { return settings.colormap; }
        std::string getColormapName() const;
        
        // Flat-Field Correction (FFC)
        void performFFC();
        
        // NUC (legacy API - Lepton uses hardware FFC instead)
        void setNUCEnabled(bool enabled) { (void)enabled; /* No-op: Lepton uses FFC */ }
        bool isNUCEnabled() const { return false; }
        
        // Smoothing/filtering
        void setSmoothingFactor(float factor);  // 0.0 = none, 1.0 = max
        float getSmoothingFactor() const { return settings.smoothingFactor; }
        
        // Region of Interest
        void setROI(int x, int y, int width, int height);
        void clearROI();
        bool hasROI() const { return settings.enableRoi; }
        cv::Rect getROI() const;
        
        // Temperature alarm
        void setTemperatureAlarm(bool enabled, float thresholdC);
        bool isAlarmEnabled() const { return settings.enableTempAlarm; }
        float getAlarmThreshold() const { return settings.alarmThreshold_C; }
        bool isAlarmTriggered() const { return alarmTriggered; }
        void setAlarmCallback(std::function<void(float maxTemp, cv::Point location)> callback);
        
        // Apply all settings from external struct
        struct Settings
        {
            float emissivity = 0.95f;
            float reflectedTemp_C = 23.0f;
            bool autoRange = false;
            float minTemp_C = 20.0f;
            float maxTemp_C = 40.0f;
            Colormap colormap = Colormap::IRONBOW;
            bool applyNUC = false;  // Legacy - ignored, Lepton uses hardware FFC
            float smoothingFactor = 0.0f;
            bool enableRoi = false;
            int roiX = 0;
            int roiY = 0;
            int roiWidth = 80;
            int roiHeight = 60;
            bool enableTempAlarm = false;
            float alarmThreshold_C = 38.0f;
        };
        
        void applySettings(const Settings& newSettings);
        Settings getCurrentSettings() const { return settings; }
        void resetSettings();
        
    private:
        std::unique_ptr<Lepton3> lepton;
        std::array<uint16_t, WIDTH * HEIGHT> rawBuffer;
        
        cv::Mat lastTemperatureMap;
        cv::Mat lastRawFrame;
        cv::Mat smoothedTemperatureMap;
        
        bool initialized = false;
        std::string lastError;
        std::function<void(const std::string&)> errorCallback;
        std::function<void(float, cv::Point)> alarmCallback;
        
        Settings settings;
        bool alarmTriggered = false;
        
        void setError(const std::string& error);
        float rawToTemperature(uint16_t rawValue);
        float applyEmissivityCorrection(float rawTempC);
        cv::Mat applyColormap(const cv::Mat& normalized);
        void checkTemperatureAlarm(const ThermalStats& stats);
        cv::Mat applySmoothing(const cv::Mat& temps);
    };
}