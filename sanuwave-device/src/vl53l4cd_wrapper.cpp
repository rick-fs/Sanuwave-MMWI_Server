// vl53l4cd_wrapper.cpp
#include "vl53l4cd_wrapper.h"
#include "logger.h"
#include <chrono>
#include <thread>

namespace sanuwave {

bool VL53L4CDWrapper::init()
{
    return init(Config{});
}

bool VL53L4CDWrapper::init(const Config& cfg)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (initialized)
    {
        LOG_WARNING << "VL53L4CD already initialized" << std::endl;
        return true;
    }

    config        = cfg;
    errorCallback = cfg.errorCallback;

    try
    {
        ToFSensor::getInstance().init(config.i2cAddress, config.timingBudget_ms);
    }
    catch (const std::exception& e)
    {
        setError(std::string("Init failed: ") + e.what());
        return false;
    }

    initialized = true;
    LOG_INFO << "VL53L4CD initialized (addr=0x" << std::hex
             << static_cast<int>(config.i2cAddress) << std::dec
             << ", budget=" << config.timingBudget_ms << "ms)" << std::endl;
    return true;
}

void VL53L4CDWrapper::shutdown()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (ranging)
    {
        try { ToFSensor::getInstance().stopRanging(); }
        catch (...) {}
        ranging = false;
    }
    initialized = false;
    LOG_INFO << "VL53L4CD shutdown" << std::endl;
}

bool VL53L4CDWrapper::startRanging()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!initialized) { setError("Not initialized"); return false; }
    if (ranging) return true;

    try
    {
        ToFSensor::getInstance().startRanging();
        ranging = true;
        return true;
    }
    catch (const std::exception& e)
    {
        setError(std::string("startRanging failed: ") + e.what());
        return false;
    }
}

bool VL53L4CDWrapper::stopRanging()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!ranging) return true;

    try
    {
        ToFSensor::getInstance().stopRanging();
        ranging = false;
        return true;
    }
    catch (const std::exception& e)
    {
        setError(std::string("stopRanging failed: ") + e.what());
        return false;
    }
}

bool VL53L4CDWrapper::dataReady()
{
    if (!initialized || !ranging) return false;
    try   { return ToFSensor::getInstance().isDataReady(); }
    catch (...) { return false; }
}

VL53L4CDWrapper::Measurement VL53L4CDWrapper::getMeasurement(uint32_t timeout_ms)
{
    Measurement m;

    if (!initialized) { setError("Not initialized"); return m; }
    if (!ranging && !startRanging()) return m;

    // Poll for data ready
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (!dataReady())
    {
        if (timeout_ms > 0 && std::chrono::steady_clock::now() >= deadline)
        {
            setError("Timeout waiting for measurement");
            return m;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    try
    {
        ToFSensor::Results r = ToFSensor::getInstance().getResult();
        ToFSensor::getInstance().clearInterrupt();

        m.distance_mm      = r.distance_mm;
        m.signal_per_spad  = r.signal_per_spad_kcps;
        m.ambient_per_spad = r.ambient_per_spad_kcps;
        m.num_spads        = r.number_of_spad;
        m.range_status     = r.range_status;
        m.valid            = (r.range_status == 0);
    }
    catch (const std::exception& e)
    {
        setError(std::string("getResult failed: ") + e.what());
    }

    return m;
}

uint16_t VL53L4CDWrapper::getDistance(uint32_t timeout_ms)
{
    Measurement m = getMeasurement(timeout_ms);
    return m.valid ? m.distance_mm : 0;
}

float VL53L4CDWrapper::getDistanceCm(uint32_t timeout_ms)
{
    return getDistance(timeout_ms) / 10.0f;
}

float VL53L4CDWrapper::getDistanceM(uint32_t timeout_ms)
{
    return getDistance(timeout_ms) / 1000.0f;
}

bool VL53L4CDWrapper::setRangingMode(RangingMode mode)
{
    config.mode = mode;
    // Map to timing budget: SHORT=20ms, LONG=50ms
    uint32_t budget = (mode == RangingMode::SHORT) ? 20u : 50u;
    return setTimingBudget(budget);
}

bool VL53L4CDWrapper::setTimingBudget(uint32_t budget_ms)
{
    if (!initialized) { setError("Not initialized"); return false; }
    try
    {
        ToFSensor::getInstance().setRangeTiming(budget_ms,
                                                config.intermeasurementPeriod_ms);
        config.timingBudget_ms = budget_ms;
        return true;
    }
    catch (const std::exception& e)
    {
        setError(std::string("setTimingBudget failed: ") + e.what());
        return false;
    }
}

bool VL53L4CDWrapper::setIntermeasurementPeriod(uint32_t period_ms)
{
    if (!initialized) { setError("Not initialized"); return false; }
    try
    {
        ToFSensor::getInstance().setRangeTiming(config.timingBudget_ms, period_ms);
        config.intermeasurementPeriod_ms = period_ms;
        return true;
    }
    catch (const std::exception& e)
    {
        setError(std::string("setIntermeasurementPeriod failed: ") + e.what());
        return false;
    }
}

bool VL53L4CDWrapper::setOffset(int16_t offset_mm)
{
    if (!initialized) { setError("Not initialized"); return false; }
    try   { ToFSensor::getInstance().setOffset(offset_mm); return true; }
    catch (const std::exception& e) { setError(e.what()); return false; }
}

bool VL53L4CDWrapper::setCrosstalk(uint16_t xtalk)
{
    if (!initialized) { setError("Not initialized"); return false; }
    try   { ToFSensor::getInstance().setXtalk(xtalk); return true; }
    catch (const std::exception& e) { setError(e.what()); return false; }
}

bool VL53L4CDWrapper::setSigmaThreshold(uint16_t sigma_mm)
{
    if (!initialized) { setError("Not initialized"); return false; }
    try   { ToFSensor::getInstance().setSigmaThreshold(sigma_mm); return true; }
    catch (const std::exception& e) { setError(e.what()); return false; }
}

bool VL53L4CDWrapper::setSignalThreshold(uint16_t signal_kcps)
{
    if (!initialized) { setError("Not initialized"); return false; }
    try   { ToFSensor::getInstance().setSignalThreshold(signal_kcps); return true; }
    catch (const std::exception& e) { setError(e.what()); return false; }
}

uint16_t VL53L4CDWrapper::getSensorId()
{
    if (!initialized) return 0;
    try   { return ToFSensor::getInstance().getSensorId(); }
    catch (...) { return 0; }
}

bool VL53L4CDWrapper::calibrateOffset(int16_t targetDistance_mm, int16_t nbSamples)
{
    if (!initialized) { setError("Not initialized"); return false; }
    try
    {
        ToFSensor::getInstance().calibrateOffset(targetDistance_mm, nbSamples);
        return true;
    }
    catch (const std::exception& e) { setError(e.what()); return false; }
}

bool VL53L4CDWrapper::calibrateCrosstalk(int16_t targetDistance_mm, int16_t nbSamples)
{
    if (!initialized) { setError("Not initialized"); return false; }
    try
    {
        ToFSensor::getInstance().calibrateXtalk(targetDistance_mm, nbSamples);
        return true;
    }
    catch (const std::exception& e) { setError(e.what()); return false; }
}

void VL53L4CDWrapper::setError(const std::string& error)
{
    lastError = error;
    LOG_ERROR << "VL53L4CD: " << error << std::endl;
    if (errorCallback) errorCallback(error);
}

const char* VL53L4CDWrapper::getRangeStatusString(uint8_t status)
{
    switch (status)
    {
        case 0:  return "Valid";
        case 1:  return "Sigma fail";
        case 2:  return "Signal fail";
        case 4:  return "Out of bounds fail";
        case 7:  return "Wrap target fail";
        case 12: return "Crosstalk fail";
        case 13: return "Synchronization fail";
        case 18: return "Min range fail";
        default: return "Unknown";
    }
}

} // namespace sanuwave
