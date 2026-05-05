#include "temp_monitor.h"
#include "logger.h"

namespace sanuwave
{

TemperatureMonitor::TemperatureMonitor(SensorInfo& sensorInfo, int pollIntervalSeconds)
    : sensorInfo(sensorInfo), pollInterval_s(pollIntervalSeconds)
{
}

TemperatureMonitor::~TemperatureMonitor()
{
    stop();
}

void TemperatureMonitor::start()
{
    if (running)
        return;

    if (!sensorInfo.isOpen())
    {
        LOG_WARNING << "TemperatureMonitor: SensorInfo not open, cannot start" << std::endl;
        return;
    }

    running = true;
    worker  = std::thread(&TemperatureMonitor::pollLoop, this);
    LOG_INFO << "TemperatureMonitor started (interval=" << pollInterval_s << "s)" << std::endl;
}

void TemperatureMonitor::stop()
{
    running = false;
    if (worker.joinable())
        worker.join();
}

void TemperatureMonitor::pollLoop()
{
    LOG_INFO << "TemperatureMonitor poll thread running" << std::endl;

    while (running)
    {
        auto reading = sensorInfo.getTemperature();

        if (reading)
        {
            if (reading->reliable)
            {
                std::lock_guard<std::mutex> lock(mutex);
                cached      = reading;
                lastReadTime = std::chrono::steady_clock::now();
                LOG_TRACE << "TemperatureMonitor: " << (int)reading->celsius << "C" << std::endl;
            }
            else
            {
                LOG_WARNING << "TemperatureMonitor: unreliable read, retaining cached value"
                            << std::endl;
            }
        }
        else
        {
            LOG_WARNING << "TemperatureMonitor: read failed entirely" << std::endl;
        }

        // Sleep in small increments for responsive shutdown
        for (int i = 0; i < pollInterval_s * 10 && running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO << "TemperatureMonitor poll thread exiting" << std::endl;
}

std::optional<CachedTemperature> TemperatureMonitor::getLatest()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!cached)
        return std::nullopt;

    CachedTemperature result;
    result.celsius  = cached->celsius;
    result.reliable = cached->reliable;
    result.age_s    = static_cast<int>(
                          std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::steady_clock::now() - lastReadTime).count());
    return result;
}

} // namespace sanuwave
