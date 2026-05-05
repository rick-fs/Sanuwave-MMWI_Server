// sensor_timing_calculator.cpp
#include "sensor_timing_calculator.h"
#include "logger.h"
#include <algorithm>

namespace sanuwave
{

std::optional<SensorTimingCalculator::SensorParams> SensorTimingCalculator::getSensorParams(
    const std::string &name)
{
    if (name == "imx708" || name == "rgb")
        return IMX708_PARAMS;
    else if (name == "imx219" || name == "arducam")
        return IMX219_PARAMS;

    return std::nullopt;
}

CalculatedTiming SensorTimingCalculator::calculate(const std::string &sensorName, int width,
                                                   int height)
{
    auto params = getSensorParams(sensorName);
    if (!params)
    {
        LOG_ERROR << "Unknown sensor for timing calculation: " << sensorName << std::endl;
        return CalculatedTiming{};
    }

    return calculate(*params, width, height);
}

CalculatedTiming SensorTimingCalculator::calculate(const SensorParams &params, int width,
                                                   int height)
{
    CalculatedTiming timing;

    // Validate dimensions
    if (width <= 0 || height <= 0 || width > params.maxWidth || height > params.maxHeight)
    {
        LOG_ERROR << "Invalid dimensions for timing calculation: " << width << "x" << height
                  << std::endl;
        return timing;
    }

    timing.activeWidth = width;
    timing.activeHeight = height;
    timing.pixelRate = params.pixelRate;

    // HBlank scales with resolution in some modes, but we use typical/min
    // For binned/scaled modes, hblank may differ - this is an approximation
    timing.hblank = params.hblankTypical;
    timing.vblank = params.vblankMin;

    // Line length in pixels
    int lineLength = width + timing.hblank;

    // Line time in microseconds
    // lineTime = lineLength / pixelRate * 1e6
    timing.lineTime_us = (static_cast<double>(lineLength) * 1000000.0) / params.pixelRate;

    // Rolling shutter duration = time for all active lines to be exposed
    // This is the critical value for LED synchronization
    timing.rollingShutter_us = timing.lineTime_us * height;

    // Frame time (total including vblank)
    int frameHeight = height + timing.vblank;
    timing.frameTime_us = timing.lineTime_us * frameHeight;

    // Max theoretical frame rate
    if (timing.frameTime_us > 0)
    {
        timing.maxFrameRate = 1000000.0 / timing.frameTime_us;
    }

    timing.valid = true;

    LOG_INFO << "Calculated timing for " << width << "x" << height << ":"
             << " line=" << timing.lineTime_us << "us"
             << " rolling=" << timing.rollingShutter_us << "us"
             << " (" << (timing.rollingShutter_us / 1000.0) << "ms)"
             << " frame=" << timing.frameTime_us << "us"
             << " maxFPS=" << timing.maxFrameRate << std::endl;

    return timing;
}

} // namespace sanuwave
