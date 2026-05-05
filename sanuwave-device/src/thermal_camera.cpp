// src/thermal_camera.cpp
#include "thermal_camera.h"
#include "lepton3.h"
#include "logger.h"
#include <algorithm>
#include <cmath>

namespace sanuwave
{

ThermalCamera::ThermalCamera()
    : lepton(std::make_unique<Lepton3>())
{
}

ThermalCamera::~ThermalCamera()
{
    shutdown();
}

bool ThermalCamera::init(int i2cBus, const std::string& spiDevice)
{
    LOG_INFO << "Initializing thermal camera on I2C bus " << i2cBus 
             << ", SPI: " << spiDevice << std::endl;
    
    if (!lepton->begin(i2cBus, spiDevice))
    {
        setError("Failed to initialize Lepton3: " + lepton->getLastError());
        return false;
    }
    
    initialized = true;
    LOG_INFO << "Thermal camera initialized: " << WIDTH << "x" << HEIGHT << std::endl;
    
    return true;
}

bool ThermalCamera::init(const std::string& device)
{
    // Parse "bus:spidev" format or use defaults
    int i2cBus = 1;
    std::string spiDevice = "/dev/spidev0.0";
    
    if (!device.empty())
    {
        size_t colonPos = device.find(':');
        if (colonPos != std::string::npos)
        {
            i2cBus = std::stoi(device.substr(0, colonPos));
            spiDevice = device.substr(colonPos + 1);
        }
        else
        {
            // Assume it's just the SPI device
            spiDevice = device;
        }
    }
    
    return init(i2cBus, spiDevice);
}

void ThermalCamera::shutdown()
{
    if (initialized)
    {
        LOG_INFO << "Shutting down thermal camera..." << std::endl;
        lepton->end();
        lastTemperatureMap.release();
        lastRawFrame.release();
        smoothedTemperatureMap.release();
        initialized = false;
        LOG_INFO << "Thermal camera shutdown complete" << std::endl;
    }
}

// ============================================================================
// SETTINGS API IMPLEMENTATION
// ============================================================================

void ThermalCamera::setEmissivity(float emissivity)
{
    settings.emissivity = std::clamp(emissivity, 0.1f, 1.0f);
    LOG_DEBUG << "Thermal emissivity set to: " << settings.emissivity << std::endl;
}

void ThermalCamera::setReflectedTemperature(float tempC)
{
    settings.reflectedTemp_C = tempC;
    LOG_DEBUG << "Reflected temperature set to: " << tempC << "°C" << std::endl;
}

void ThermalCamera::setTemperatureRange(float minC, float maxC)
{
    if (minC < maxC)
    {
        settings.minTemp_C = minC;
        settings.maxTemp_C = maxC;
        settings.autoRange = false;
        LOG_DEBUG << "Temperature range set to: " << minC << "°C - " << maxC << "°C" << std::endl;
    }
}

void ThermalCamera::setAutoRange(bool enabled)
{
    settings.autoRange = enabled;
    LOG_DEBUG << "Auto range " << (enabled ? "enabled" : "disabled") << std::endl;
}

void ThermalCamera::setColormap(Colormap colormap)
{
    settings.colormap = colormap;
    LOG_DEBUG << "Colormap set to: " << getColormapName() << std::endl;
}

void ThermalCamera::setColormap(const std::string& colormapName)
{
    std::string lower = colormapName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "ironbow")
        settings.colormap = Colormap::IRONBOW;
    else if (lower == "rainbow")
        settings.colormap = Colormap::RAINBOW;
    else if (lower == "grayscale" || lower == "gray")
        settings.colormap = Colormap::GRAYSCALE;
    else if (lower == "hot")
        settings.colormap = Colormap::HOT;
    else if (lower == "jet")
        settings.colormap = Colormap::JET;
    else if (lower == "inferno")
        settings.colormap = Colormap::INFERNO;
    else if (lower == "plasma")
        settings.colormap = Colormap::PLASMA;
    else
    {
        LOG_WARNING << "Unknown colormap: " << colormapName << ", using IRONBOW" << std::endl;
        settings.colormap = Colormap::IRONBOW;
    }
    
    LOG_DEBUG << "Colormap set to: " << getColormapName() << std::endl;
}

std::string ThermalCamera::getColormapName() const
{
    switch (settings.colormap)
    {
        case Colormap::IRONBOW: return "ironbow";
        case Colormap::RAINBOW: return "rainbow";
        case Colormap::GRAYSCALE: return "grayscale";
        case Colormap::HOT: return "hot";
        case Colormap::JET: return "jet";
        case Colormap::INFERNO: return "inferno";
        case Colormap::PLASMA: return "plasma";
        default: return "unknown";
    }
}

void ThermalCamera::performFFC()
{
    if (!initialized)
    {
        LOG_WARNING << "Cannot perform FFC - camera not initialized" << std::endl;
        return;
    }
    
    LOG_INFO << "Performing Flat-Field Correction..." << std::endl;
    
    if (lepton->runFFC())
    {
        LOG_INFO << "FFC complete" << std::endl;
    }
    else
    {
        LOG_ERROR << "FFC failed: " << lepton->getLastError() << std::endl;
    }
}

void ThermalCamera::setSmoothingFactor(float factor)
{
    settings.smoothingFactor = std::clamp(factor, 0.0f, 1.0f);
    LOG_DEBUG << "Smoothing factor set to: " << settings.smoothingFactor << std::endl;
}

void ThermalCamera::setROI(int x, int y, int width, int height)
{
    settings.enableRoi = true;
    settings.roiX = x;
    settings.roiY = y;
    settings.roiWidth = width;
    settings.roiHeight = height;
    LOG_DEBUG << "ROI set to: (" << x << "," << y << ") " << width << "x" << height << std::endl;
}

void ThermalCamera::clearROI()
{
    settings.enableRoi = false;
    LOG_DEBUG << "ROI cleared" << std::endl;
}

cv::Rect ThermalCamera::getROI() const
{
    if (settings.enableRoi)
    {
        return cv::Rect(settings.roiX, settings.roiY, settings.roiWidth, settings.roiHeight);
    }
    return cv::Rect();
}

void ThermalCamera::setTemperatureAlarm(bool enabled, float thresholdC)
{
    settings.enableTempAlarm = enabled;
    settings.alarmThreshold_C = thresholdC;
    alarmTriggered = false;
    LOG_DEBUG << "Temperature alarm " << (enabled ? "enabled" : "disabled") 
              << " at " << thresholdC << "°C" << std::endl;
}

void ThermalCamera::setAlarmCallback(std::function<void(float maxTemp, cv::Point location)> callback)
{
    alarmCallback = callback;
}

void ThermalCamera::applySettings(const Settings& newSettings)
{
    settings = newSettings;
    LOG_DEBUG << "Applied thermal camera settings" << std::endl;
}

void ThermalCamera::resetSettings()
{
    settings = Settings();
    alarmTriggered = false;
    LOG_DEBUG << "Thermal camera settings reset to defaults" << std::endl;
}

// ============================================================================
// CAPTURE METHODS
// ============================================================================

cv::Mat ThermalCamera::captureRaw()
{
    if (!initialized)
    {
        setError("Thermal camera not initialized");
        return cv::Mat();
    }
    
    if (!lepton->captureFrame(rawBuffer.data()))
    {
        setError("Failed to capture frame: " + lepton->getLastError());
        return cv::Mat();
    }
    
    // Convert uint16_t buffer to cv::Mat
    cv::Mat raw(HEIGHT, WIDTH, CV_16UC1, rawBuffer.data());
    lastRawFrame = raw.clone();
    
    return lastRawFrame;
}

cv::Mat ThermalCamera::captureTemperatureMap()
{
    cv::Mat raw = captureRaw();
    
    if (raw.empty())
    {
        return cv::Mat();
    }
    
    cv::Mat temperatures(HEIGHT, WIDTH, CV_32F);
    
    // Convert raw values to temperature
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            uint16_t rawValue = raw.at<uint16_t>(y, x);
            float tempC = rawToTemperature(rawValue);
            tempC = applyEmissivityCorrection(tempC);
            temperatures.at<float>(y, x) = tempC;
        }
    }
    
    // Apply smoothing if enabled
    if (settings.smoothingFactor > 0.0f)
    {
        temperatures = applySmoothing(temperatures);
    }
    
    lastTemperatureMap = temperatures.clone();
    
    // Check temperature alarm
    auto stats = getStats();
    checkTemperatureAlarm(stats);
    
    return temperatures;
}

cv::Mat ThermalCamera::captureThermalVisualization(int scale)
{
    cv::Mat temps = captureTemperatureMap();
    
    if (temps.empty())
    {
        return cv::Mat();
    }
    
    // Apply ROI if enabled
    cv::Mat displayTemps = temps;
    if (settings.enableRoi)
    {
        cv::Rect roi(settings.roiX, settings.roiY, settings.roiWidth, settings.roiHeight);
        roi &= cv::Rect(0, 0, temps.cols, temps.rows);
        if (roi.width > 0 && roi.height > 0)
        {
            displayTemps = temps(roi);
        }
    }
    
    // Determine temperature range for normalization
    float minT = settings.minTemp_C;
    float maxT = settings.maxTemp_C;
    
    if (settings.autoRange)
    {
        double minVal, maxVal;
        cv::minMaxLoc(displayTemps, &minVal, &maxVal);
        minT = static_cast<float>(minVal);
        maxT = static_cast<float>(maxVal);
        
        float margin = (maxT - minT) * 0.05f;
        minT -= margin;
        maxT += margin;
    }
    
    // Normalize to 0-255
    cv::Mat normalized;
    displayTemps.convertTo(normalized, CV_32F);
    normalized = (normalized - minT) / (maxT - minT) * 255.0f;
    cv::threshold(normalized, normalized, 255, 255, cv::THRESH_TRUNC);
    cv::threshold(normalized, normalized, 0, 0, cv::THRESH_TOZERO);
    normalized.convertTo(normalized, CV_8U);
    
    // Apply colormap
    cv::Mat colored = applyColormap(normalized);
    
    // Scale for visibility (4x for 160x120 -> 640x480)
    cv::Mat resized;
    if (scale <= 1)
    {
        resized = colored;  // Native resolution, no scaling
    }
    else
    {
        cv::resize(colored, resized, cv::Size(temps.cols * scale, temps.rows * scale), 
                   0, 0, cv::INTER_NEAREST);
    }
    
    // Get stats and overlay info
    auto stats = getStats();
    if (scale >= 2)
    {
        // Draw crosshair at hottest point
        cv::Point hotScaled(stats.maxLoc.x * scale + scale/2, stats.maxLoc.y * scale + scale/2);
        cv::drawMarker(resized, hotScaled, cv::Scalar(0, 0, 255), 
                    cv::MARKER_CROSS, 20, 2);
        
        // Draw crosshair at coldest point
        cv::Point coldScaled(stats.minLoc.x * scale + scale/2, stats.minLoc.y * scale + scale/2);
        cv::drawMarker(resized, coldScaled, cv::Scalar(255, 0, 0), 
                    cv::MARKER_CROSS, 15, 1);
    
        // Add text overlays
        char tempText[64];
        snprintf(tempText, sizeof(tempText), "Max: %.1fC", stats.maxTemp);
        cv::putText(resized, tempText, cv::Point(10, 30), 
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
        
        snprintf(tempText, sizeof(tempText), "Min: %.1fC", stats.minTemp);
        cv::putText(resized, tempText, cv::Point(10, 60), 
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    
        snprintf(tempText, sizeof(tempText), "Avg: %.1fC", stats.avgTemp);
        cv::putText(resized, tempText, cv::Point(10, 90), 
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    
        // Show alarm status if enabled
        if (settings.enableTempAlarm)
        {
            if (alarmTriggered)
            {
                cv::putText(resized, "! ALARM !", cv::Point(resized.cols - 120, 30), 
                            cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
            }
            snprintf(tempText, sizeof(tempText), "Alarm: %.1fC", settings.alarmThreshold_C);
            cv::putText(resized, tempText, cv::Point(10, resized.rows - 10), 
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 200, 200), 1);
        }
    
    // Show emissivity
        snprintf(tempText, sizeof(tempText), "e=%.2f", settings.emissivity);
        cv::putText(resized, tempText, cv::Point(resized.cols - 80, resized.rows - 10), 
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 200, 200), 1);
    }
    return resized;
}

// ============================================================================
// HELPER METHODS
// ============================================================================

float ThermalCamera::getPixelTemperature(int x, int y)
{
    if (lastTemperatureMap.empty())
    {
        captureTemperatureMap();
    }
    
    if (lastTemperatureMap.empty() || 
        x < 0 || x >= lastTemperatureMap.cols ||
        y < 0 || y >= lastTemperatureMap.rows)
    {
        return 0.0f;
    }
    
    return lastTemperatureMap.at<float>(y, x);
}

ThermalCamera::ThermalStats ThermalCamera::getStats()
{
    ThermalStats stats;
    stats.minTemp = 0.0f;
    stats.maxTemp = 0.0f;
    stats.avgTemp = 0.0f;
    stats.minLoc = cv::Point(0, 0);
    stats.maxLoc = cv::Point(0, 0);
    
    if (lastTemperatureMap.empty())
    {
        return stats;
    }
    
    double minVal, maxVal;
    cv::minMaxLoc(lastTemperatureMap, &minVal, &maxVal, 
                  &stats.minLoc, &stats.maxLoc);
    
    stats.minTemp = static_cast<float>(minVal);
    stats.maxTemp = static_cast<float>(maxVal);
    stats.avgTemp = static_cast<float>(cv::mean(lastTemperatureMap)[0]);
    
    return stats;
}

float ThermalCamera::rawToTemperature(uint16_t rawValue)
{
    // Lepton 3 raw values are in centi-Kelvin
    // Temperature(K) = rawValue / 100
    // Temperature(C) = Temperature(K) - 273.15
    float tempKelvin = rawValue / 100.0f;
    float tempCelsius = tempKelvin - 273.15f;
    
    return tempCelsius;
}

float ThermalCamera::applyEmissivityCorrection(float rawTempC)
{
    if (settings.emissivity >= 0.99f)
    {
        return rawTempC;
    }
    
    // Emissivity correction formula
    // T_object = (T_apparent^4 - (1 - e) * T_reflected^4) / e)^0.25
    float tApparentK = rawTempC + 273.15f;
    float tReflectedK = settings.reflectedTemp_C + 273.15f;
    
    float tObjectK = std::pow(
        (std::pow(tApparentK, 4.0f) - (1.0f - settings.emissivity) * std::pow(tReflectedK, 4.0f)) 
        / settings.emissivity, 
        0.25f
    );
    
    return tObjectK - 273.15f;
}

cv::Mat ThermalCamera::applyColormap(const cv::Mat& normalized)
{
    cv::Mat colored;
    int cvColormap;
    
    switch (settings.colormap)
    {
        case Colormap::RAINBOW:
            cvColormap = cv::COLORMAP_RAINBOW;
            break;
        case Colormap::GRAYSCALE:
            cv::cvtColor(normalized, colored, cv::COLOR_GRAY2BGR);
            return colored;
        case Colormap::HOT:
            cvColormap = cv::COLORMAP_HOT;
            break;
        case Colormap::JET:
            cvColormap = cv::COLORMAP_JET;
            break;
        case Colormap::INFERNO:
            cvColormap = cv::COLORMAP_INFERNO;
            break;
        case Colormap::PLASMA:
            cvColormap = cv::COLORMAP_PLASMA;
            break;
        case Colormap::IRONBOW:
        default:
            cvColormap = cv::COLORMAP_INFERNO;
            break;
    }
    
    cv::applyColorMap(normalized, colored, cvColormap);
    return colored;
}

void ThermalCamera::checkTemperatureAlarm(const ThermalStats& stats)
{
    if (!settings.enableTempAlarm)
    {
        alarmTriggered = false;
        return;
    }
    
    bool wasTriggered = alarmTriggered;
    alarmTriggered = (stats.maxTemp >= settings.alarmThreshold_C);
    
    if (alarmTriggered && !wasTriggered && alarmCallback)
    {
        alarmCallback(stats.maxTemp, stats.maxLoc);
    }
}

cv::Mat ThermalCamera::applySmoothing(const cv::Mat& temps)
{
    if (settings.smoothingFactor <= 0.0f)
    {
        return temps;
    }
    
    if (smoothedTemperatureMap.empty() || smoothedTemperatureMap.size() != temps.size())
    {
        smoothedTemperatureMap = temps.clone();
        return temps;
    }
    
    float alpha = 1.0f - settings.smoothingFactor;
    cv::Mat result;
    cv::addWeighted(temps, alpha, smoothedTemperatureMap, settings.smoothingFactor, 0, result);
    smoothedTemperatureMap = result.clone();
    
    return result;
}

void ThermalCamera::setError(const std::string& error)
{
    lastError = error;
    LOG_ERROR << "Thermal camera error: " << error << std::endl;
    
    if (errorCallback)
    {
        errorCallback(error);
    }
}

} // namespace sanuwave