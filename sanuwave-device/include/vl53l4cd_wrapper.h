// vl53l4cd_wrapper.h
#ifndef VL53L4CD_WRAPPER_H
#define VL53L4CD_WRAPPER_H

#include "ToFSensor.h"
#include <cstdint>
#include <string>
#include <functional>
#include <mutex>

namespace sanuwave {

/**
 * @brief Wrapper for the VL53L4CD Time-of-Flight sensor.
 *
 * Delegates to ToFSensor::getInstance(). I2cMgr must be open
 * before calling init().
 */
class VL53L4CDWrapper {
public:
    enum class RangingMode {
        SHORT = 1,
        LONG  = 2
    };

    struct Config {
        uint8_t      i2cAddress              = 0x29;
        RangingMode  mode                    = RangingMode::SHORT;
        uint32_t     timingBudget_ms         = 50;
        uint32_t     intermeasurementPeriod_ms = 0;   // 0 = continuous
        std::function<void(const std::string&)> errorCallback;
    };

    struct Measurement {
        uint16_t distance_mm       = 0;
        uint32_t signal_per_spad   = 0;
        uint32_t ambient_per_spad  = 0;
        uint16_t num_spads         = 0;
        uint8_t  range_status      = 255;
        bool     valid             = false;
    };

    VL53L4CDWrapper()  = default;
    ~VL53L4CDWrapper() = default;

    // Non-copyable
    VL53L4CDWrapper(const VL53L4CDWrapper&)            = delete;
    VL53L4CDWrapper& operator=(const VL53L4CDWrapper&) = delete;

    bool init();
    bool init(const Config& config);
    void shutdown();

    bool isInitialized() const { return initialized; }
    bool isRanging()     const { return ranging; }

    bool startRanging();
    bool stopRanging();
    bool dataReady();

    Measurement getMeasurement(uint32_t timeout_ms = 1000);
    uint16_t    getDistance(uint32_t timeout_ms = 1000);
    float       getDistanceCm(uint32_t timeout_ms = 1000);
    float       getDistanceM(uint32_t timeout_ms = 1000);

    bool setTimingBudget(uint32_t budget_ms);
    bool setIntermeasurementPeriod(uint32_t period_ms);
    bool setRangingMode(RangingMode mode);
    bool setOffset(int16_t offset_mm);
    bool setCrosstalk(uint16_t xtalk);
    bool setSigmaThreshold(uint16_t sigma_mm);
    bool setSignalThreshold(uint16_t signal_kcps);

    uint16_t getSensorId();
    bool     calibrateOffset(int16_t targetDistance_mm, int16_t nbSamples = 20);
    bool     calibrateCrosstalk(int16_t targetDistance_mm, int16_t nbSamples = 20);

    void setErrorCallback(std::function<void(const std::string&)> cb) { errorCallback = cb; }
    std::string getLastError() const { return lastError; }

    static const char* getRangeStatusString(uint8_t status);

private:
    void setError(const std::string& error);

    Config      config;
    bool        initialized = false;
    bool        ranging     = false;
    std::string lastError;
    std::function<void(const std::string&)> errorCallback;
    mutable std::mutex mutex;
};

} // namespace sanuwave

#endif // VL53L4CD_WRAPPER_H
