// Copyright 2026 Sanuwave Medical LLC.
//
// Portions of this code were generated with Claude.ai and
// reviewed and edited.

#ifndef SENSOR_CALIBRATION_STORE_H
#define SENSOR_CALIBRATION_STORE_H

#include <string>
#include <array>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <QString>
#include <QJsonObject>

namespace sanuwave
{

/// Color correction matrix for a specific illuminant
struct ColorCalibration
{
    int colorTemp = 6500;              // Color temperature in Kelvin
    std::array<double, 9> ccm;         // 3x3 camera RGB -> sRGB (row-major)
    std::array<double, 9> colorMatrix; // 3x3 XYZ -> camera (for DNG)
    bool valid = false;
};

/// Complete calibration data for one sensor
struct SensorCalibration
{
    std::string sensorModel;           // "imx708" or "imx219"
    std::string cameraModel;           // "Sony IMX708" for DNG metadata
    
    // Black/white levels (10-bit sensor space)
    uint16_t blackLevel = 64;
    uint16_t whiteLevel = 1023;
    uint16_t bitsPerSample = 10;
    
    // CFA pattern: 0=R, 1=G, 2=B
    std::array<uint8_t, 4> cfaPattern = {0, 1, 1, 2};  // RGGB default
    
    // Color calibration at multiple illuminants
    ColorCalibration d65;              // ~6500K daylight
    ColorCalibration tl84;             // ~4000K fluorescent (optional)
    ColorCalibration incandescent;     // ~2850K tungsten (optional)
    
    // AWB gains for neutral gray under D65
    double neutralR = 1.0;
    double neutralB = 1.0;
    
    bool isValid() const { return !sensorModel.empty() && d65.valid; }
};

/// Singleton store for sensor calibration data
/// Loaded from RPi tuning JSON files or received from server
class SensorCalibrationStore
{
public:
    static SensorCalibrationStore& instance();
    
    // Delete copy/move
    SensorCalibrationStore(const SensorCalibrationStore&) = delete;
    SensorCalibrationStore& operator=(const SensorCalibrationStore&) = delete;
    
    /// Load calibration from RPi tuning JSON file
    /// @param sensorModel "imx708" or "imx219"
    /// @param jsonPath Path to the tuning JSON file
    /// @return true if loaded successfully
    bool loadFromTuningFile(const std::string& sensorModel, const QString& jsonPath);
    
    /// Load calibration from server-provided JSON object
    /// Called when server sends calibration data at connection time
    bool loadFromServerJson(const std::string& sensorModel, const QJsonObject& json);
    
    /// Register hardcoded fallback calibration (used if no JSON available)
    void registerFallback(const std::string& sensorModel, const SensorCalibration& cal);
    
    /// Get calibration for a sensor
    /// @param sensorModel "imx708" or "imx219"
    /// @return Calibration data, or nullopt if not found
    std::optional<SensorCalibration> getCalibration(const std::string& sensorModel) const;
    
    /// Get calibration by image dimensions (fallback identification)
    /// IMX708 max width = 4608, IMX219 max width = 3280
    std::optional<SensorCalibration> getCalibrationByWidth(int width) const;
    
    /// Check if calibration is loaded for a sensor
    bool hasCalibration(const std::string& sensorModel) const;
    
    /// Clear all loaded calibrations
    void clear();
    
    /// Initialize with hardcoded fallback values
    /// Called at startup before attempting to load JSON files
    void initializeDefaults();

private:
    SensorCalibrationStore() = default;
    
    /// Parse the rpi.ccm section from tuning JSON
    bool parseCCMSection(const QJsonObject& ccmObj, SensorCalibration& cal);
    
    /// Parse the rpi.black_level section
    bool parseBlackLevel(const QJsonObject& blObj, SensorCalibration& cal);
    
    /// Convert camera->sRGB CCM to XYZ->camera matrix for DNG
    static std::array<double, 9> ccmToColorMatrix(const std::array<double, 9>& ccm);
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SensorCalibration> calibrations_;
};

} // namespace sanuwave

#endif // SENSOR_CALIBRATION_STORE_H
