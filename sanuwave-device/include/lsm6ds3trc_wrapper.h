// lsm6ds3trc_wrapper.h
// Server-side wrapper for the LSM6DS3TR-C IMU.
//
// Sits between the command handler (synchronous string-returning calls) and
// the chip driver (synchronous register-level calls). Owns:
//   - the chip driver (Lsm6ds3trc) with its bus
//   - a worker thread that drains the FIFO at a fixed cadence while
//     streaming, and emits each batch as an "imu_data" JSON line through
//     a notify callback (typically wired to TCPServer::sendJsonNotification
//     in main.cpp — same pattern UVBF unsolicited notifications use)
//   - small thread-safe counters (samples emitted, FIFO overruns, bus errors)
//
// Decoupling from TCPServer (via std::function) lets main.cpp construct
// this wrapper before TCPServer, which it must — CommandHandler is built
// first and takes the wrapper as a parameter, then TCPServer is created,
// then the notify callback is injected.
//
// Lifecycle mirrors the other wrappers (AS7331Wrapper, VL53L4CDWrapper,
// VD6283TXWrapper):
//   - init() probes WHO_AM_I and configures with last-applied or default
//     settings; succeeds or fails synchronously
//   - isInitialized() / getLastError() for the command handler to query
//   - configure(...) updates the active config; safe to call between starts
//   - start() launches the worker thread; stop() joins it
//   - stop() must be called from CommandHandler::onClientDisconnect()
//     alongside the camera/distance teardown — this matches the
//     existing pattern for any background activity tied to a client session.
//
// Threading: public methods are safe to call from the command-handler
// thread; the worker thread runs internally. start() is idempotent (no-op
// if already running), as is stop().

#ifndef LSM6DS3TRC_WRAPPER_H
#define LSM6DS3TRC_WRAPPER_H

#include "Lsm6ds3trc.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace sanuwave {

class Lsm6ds3trcWrapper {
public:
    // Callback that delivers a JSON-line string to the wire. main.cpp
    // injects a lambda that forwards to TCPServer::sendJsonNotification —
    // exact same pattern as CommandHandler::setNotifyCallback (UVBF
    // unsolicited JSON) and ::setStreamFrameCallback (RGB streaming
    // binary frames). Decoupling the wrapper from TCPServer means main
    // can construct this wrapper before TCPServer (resolving the
    // declaration-order problem with CommandHandler).
    using NotifyCallback = std::function<void(const std::string&)>;

    Lsm6ds3trcWrapper();
    ~Lsm6ds3trcWrapper();

    Lsm6ds3trcWrapper(const Lsm6ds3trcWrapper&)            = delete;
    Lsm6ds3trcWrapper& operator=(const Lsm6ds3trcWrapper&) = delete;

    // Inject the notify callback after TCPServer is constructed. Safe to
    // call before init() / start(). If unset when start() runs, the
    // wrapper logs a warning and silently drops the data — better than
    // crashing.
    void setNotifyCallback(NotifyCallback cb);

    // Construct the I2C bus (today's only impl) with the given slave
    // address, hand it to the chip driver, and run init(). Default address
    // 0x6A matches the Adafruit/SparkFun Qwiic dev board's SA0-low strap.
    bool init(uint8_t i2cAddress = 0x6A);

    bool        isInitialized() const { return m_initialized.load(); }
    std::string getLastError()  const;

    // Apply a configuration. Safe to call while not streaming. If called
    // while streaming, the wrapper stops the worker, applies, and restarts
    // — caller will see one short gap in the data stream.
    bool configure(const Lsm6ds3trc::Config& cfg);

    // Start the worker thread. No-op if already running.
    bool start();

    // Stop the worker thread and join it. No-op if not running.
    // MUST be called from CommandHandler::onClientDisconnect() — see file
    // comment.
    void stop();

    bool isStreaming() const { return m_streaming.load(); }

    // One-shot debug helpers; safe whether streaming or not. Synchronous.
    bool softReset();
    bool readRegister (uint8_t reg, uint8_t& outValue);
    bool writeRegister(uint8_t reg, uint8_t value);

    // Snapshot of counters. Cheap; takes the state mutex.
    struct Stats {
        uint64_t samplesEmitted = 0;
        uint64_t batchesEmitted = 0;
        uint64_t fifoOverruns   = 0;
        uint64_t busErrors      = 0;
    };
    Stats stats() const;

    // Service interval — how often the worker drains the FIFO when no INT
    // line is wired. Default 100 ms; bounded by FIFO fill time at the
    // chosen ODR. Must be set before start().
    void setServiceIntervalMs(int ms);

private:
    void workerLoop();

    // Build and send one imu_data message from a batch of samples.
    // Called only from the worker thread.
    void emitDataBatch(const std::vector<Lsm6ds3trc::Sample>& batch);

    // Build and send one imu_event message.
    void emitEvent(const Lsm6ds3trc::Event& ev);

    // Notify callback — set by main.cpp after TCPServer is constructed.
    // Protected by m_callbackMutex because the worker thread calls it
    // while the main thread may still be wiring it up at startup.
    mutable std::mutex          m_callbackMutex;
    NotifyCallback              m_notifyCallback;

    // Owned.
    std::unique_ptr<Lsm6ds3trc> m_chip;

    // Last-applied config — kept so configure() can echo it on the wire
    // and the worker thread can pull scale factors without locking the
    // chip object.
    mutable std::mutex          m_stateMutex;
    Lsm6ds3trc::Config          m_config;
    std::string                 m_lastError;
    Stats                       m_stats;

    // Worker thread + sync.
    std::thread                 m_worker;
    std::atomic<bool>           m_streaming{false};
    std::atomic<bool>           m_initialized{false};
    std::condition_variable     m_workerCv;
    std::mutex                  m_workerMutex;

    // Worker drain cadence. 16ms ≈ 60Hz, smooth enough for live numeric
    // readout without taxing the I2C bus. Each drain is one bus read so
    // the cost is negligible. Configurable via setServiceIntervalMs.
    int                         m_serviceIntervalMs = 16;
};

} // namespace sanuwave

#endif // LSM6DS3TRC_WRAPPER_H
