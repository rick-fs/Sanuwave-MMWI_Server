// VD6283TXWrapper.h
#ifndef VD6283TX_WRAPPER_H
#define VD6283TX_WRAPPER_H

#include "VD6283TX.h"
#include "PCA9545A.h"
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>

namespace sanuwave {

/**
 * @brief Ambient light measurement data structure
 */
struct ALSData {
    uint32_t red;
    uint32_t visible;
    uint32_t blue;
    uint32_t green;
    uint32_t ir;
    uint32_t clear;
    bool     valid;
    uint64_t timestamp;  // ms since epoch

    ALSData() : red(0), visible(0), blue(0), green(0),
                ir(0), clear(0), valid(false), timestamp(0) {}
};

/**
 * @brief Wrapper for the VD6283TX ambient light sensor.
 *
 * Owns mux selection (PCA9545A mux at 0x70, channel 0).
 * Provides single-shot and continuous streaming interfaces.
 * I2cMgr must be open before calling init().
 */
class VD6283TXWrapper {
public:
    using ALSCallback = std::function<void(const ALSData&)>;

    /**
     * @param mux         PCA9545A instance managing the bus the sensor is on.
     * @param muxChannel  Channel on that mux (0-based). Default 0 (bit 0x01).
     */
    explicit VD6283TXWrapper(PCA9545A* mux, uint8_t muxChannel = 0);
    ~VD6283TXWrapper();

    // Non-copyable, non-movable
    VD6283TXWrapper(const VD6283TXWrapper&)            = delete;
    VD6283TXWrapper& operator=(const VD6283TXWrapper&) = delete;

    /**
     * @brief Initialise the sensor.
     * Selects the mux channel, verifies device ID, applies default config.
     * @param i2cAddress 7-bit I2C address (default 0x20).
     * @return true on success.
     */
    bool init(uint8_t i2cAddress = VD6283TX::DEFAULT_I2C_ADDR);

    /**
     * @brief Stop all operations and place sensor in idle state.
     */
    void shutdown();

    bool isInitialized() const { return initialized; }

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /**
     * @brief Set exposure time in milliseconds.
     * Must not be called while streaming.
     */
    bool setExposureMs(float ms);

    /**
     * @brief Set gain for all channels.
     * Must not be called while streaming.
     */
    bool setGainAll(VD6283TX::Gain gain);

    /**
     * @brief Set gain for a single channel.
     * Must not be called while streaming.
     */
    bool setGain(VD6283TX::Channel ch, VD6283TX::Gain gain);

    // -------------------------------------------------------------------------
    // Single-shot
    // -------------------------------------------------------------------------

    /**
     * @brief Trigger a blocking single-shot measurement.
     * Waits for the exposure to complete based on the configured exposure time,
     * then polls isDataReady() until data is available or timeout elapses.
     * @param data        Output data.
     * @param timeoutMs   Maximum wait time in milliseconds (on top of exposure).
     * @return true on success.
     */
    bool readSingle(ALSData& data, uint32_t timeoutMs = 500);

    /**
     * @brief Return the cached result from the last successful measurement.
     */
    bool getLastMeasurement(ALSData& data);

    // -------------------------------------------------------------------------
    // Streaming
    // -------------------------------------------------------------------------

    /**
     * @brief Start continuous measurement on a background thread.
     * @param intervalMs  Polling interval between reads (ms).
     * @param callback    Called on the streaming thread for each valid sample.
     * @return true if started successfully.
     */
    bool startStreaming(int intervalMs, ALSCallback callback);

    /**
     * @brief Stop continuous measurement and join the streaming thread.
     */
    bool stopStreaming();

    bool isStreaming() const { return streaming; }

    // -------------------------------------------------------------------------
    // Error
    // -------------------------------------------------------------------------
    std::string getLastError() const { return lastError; }

private:
    bool selectMux();
    void streamingLoop(int intervalMs);
    void setError(const std::string& error);

    PCA9545A*  mux;
    uint8_t    muxChannel;

    bool       initialized;
    std::atomic<bool> streaming;

    mutable std::mutex sensorMutex;
    mutable std::mutex dataMutex;

    ALSData     latestData;
    ALSCallback dataCallback;
    std::thread streamThread;

    std::string lastError;
};

} // namespace sanuwave

#endif // VD6283TX_WRAPPER_H
