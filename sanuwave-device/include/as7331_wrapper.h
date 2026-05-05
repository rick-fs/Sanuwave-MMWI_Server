// as7331_wrapper.h
#ifndef AS7331_WRAPPER_H
#define AS7331_WRAPPER_H

#include "AS7331.h"
#include <string>
#include <mutex>
#include <cstdint>
#include <chrono>

namespace sanuwave {

/**
 * @brief UV measurement data structure
 */
struct UVData {
    float    uva;        // UVA irradiance in µW/cm²
    float    uvb;        // UVB irradiance in µW/cm²
    float    uvc;        // UVC irradiance in µW/cm²
    float    temp_c;     // Die temperature in °C
    bool     valid;      // Data validity flag
    uint64_t timestamp;  // Measurement timestamp (ms since epoch)

    UVData() : uva(0), uvb(0), uvc(0), temp_c(0), valid(false), timestamp(0) {}
};

/**
 * @brief Wrapper for the AS7331 singleton.
 *
 * Provides a simple measurement interface over AS7331::getInstance().
 * I2cMgr must be open before calling init().
 */
class AS7331Wrapper {
public:
    AS7331Wrapper();
    ~AS7331Wrapper();

    // Non-copyable, non-movable
    AS7331Wrapper(const AS7331Wrapper&)            = delete;
    AS7331Wrapper& operator=(const AS7331Wrapper&) = delete;

    /**
     * @brief Initialise the UV sensor.
     * @param i2cAddress I2C address (default 0x74)
     * @return true if successful
     */
    bool init(uint8_t i2cAddress = AS7331::DEFAULT_I2C_ADDR);

    /**
     * @brief Power down and mark as uninitialised.
     */
    void shutdown();

    bool isInitialized() const { return initialized; }

    /**
     * @brief Reconfigure gain, integration time and measurement mode.
     */
    bool configure(AS7331::Gain     gain     = AS7331::Gain::GAIN_64,
                   AS7331::IntTime  intTime  = AS7331::IntTime::TIME_64MS,
                   AS7331::MeasMode measMode = AS7331::MeasMode::CMD);

    /**
     * @brief Trigger a blocking single-shot measurement.
     * Irradiance values are in µW/cm².
     */
    bool takeMeasurement(float& uva, float& uvb, float& uvc, float& temp);

    /**
     * @brief Trigger a measurement and return structured UV data.
     */
    UVData readUVData();

    /**
     * @brief Return the cached result from the last successful measurement.
     */
    bool getLastMeasurement(float& uva, float& uvb, float& uvc, float& temp);

    /**
     * @brief Set measurement mode by name.
     * Valid values: "cmd", "command", "cont", "continuous", "syns", "synd", "sync"
     */
    bool setMode(const std::string& mode);

    /**
     * @brief Set gain by numeric string: "1", "2", "4" … "2048"
     */
    bool setGain(const std::string& gain);

    /**
     * @brief Set integration time by string: "1ms", "2ms" … "1024ms"
     */
    bool setIntegrationTime(const std::string& time);

    std::string getLastError() const { return lastError; }

private:
    bool initialized;

    mutable std::mutex sensorMutex;
    mutable std::mutex dataMutex;

    float  lastUVA;
    float  lastUVB;
    float  lastUVC;
    float  lastTemp;
    bool   dataValid;

    std::string lastError;
};

} // namespace sanuwave

#endif // AS7331_WRAPPER_H
