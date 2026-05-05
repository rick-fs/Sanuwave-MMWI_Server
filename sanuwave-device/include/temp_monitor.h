#pragma once

#include "sensor_info.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

namespace sanuwave
{

struct CachedTemperature
{
    int8_t  celsius   = 0;
    bool    reliable  = false;
    int     age_s     = 0;   // seconds since last successful read (filled on query)
};

class TemperatureMonitor
{
public:
    explicit TemperatureMonitor(SensorInfo& sensorInfo, int pollIntervalSeconds = 5);
    ~TemperatureMonitor();

    // Non-copyable, non-movable (owns a thread)
    TemperatureMonitor(const TemperatureMonitor&)            = delete;
    TemperatureMonitor& operator=(const TemperatureMonitor&) = delete;

    void start();
    void stop();

    bool isRunning() const { return running; }

    // Returns nullopt if no reliable reading has been obtained yet.
    // age_s is filled with seconds since the last successful read.
    std::optional<CachedTemperature> getLatest();

private:
    void pollLoop();

    SensorInfo&              sensorInfo;
    int                      pollInterval_s;

    std::thread              worker;
    std::atomic<bool>        running{false};

    std::mutex               mutex;
    std::optional<TemperatureReading>          cached;
    std::chrono::steady_clock::time_point      lastReadTime;
};

} // namespace sanuwave
