// sensor_timing_calculator.h
#ifndef SENSOR_TIMING_CALCULATOR_H_
#define SENSOR_TIMING_CALCULATOR_H_

#include <cstdint>
#include <optional>
#include <string>

namespace sanuwave
{

struct CalculatedTiming
{
    int activeWidth = 0;
    int activeHeight = 0;
    int hblank = 0;
    int vblank = 0;
    int64_t pixelRate = 0;
    double lineTime_us = 0.0;
    double frameTime_us = 0.0;
    double rollingShutter_us = 0.0;
    double maxFrameRate = 0.0;
    bool valid = false;
};

class SensorTimingCalculator
{
  public:
    // Sensor-specific constants from datasheets
    struct SensorParams
    {
        int64_t pixelRate; // Hz
        int hblankMin;     // pixels
        int hblankTypical; // pixels (used for calculations)
        int vblankMin;     // lines
        int maxWidth;
        int maxHeight;
    };

    // Known sensor parameters
    static constexpr SensorParams IMX708_PARAMS = {.pixelRate = 450000000, // 450 MHz
                                                   .hblankMin = 756,       // From driver
                                                   .hblankTypical = 756,
                                                   .vblankMin = 40,
                                                   .maxWidth = 4608,
                                                   .maxHeight = 2592};

    static constexpr SensorParams IMX219_PARAMS = {.pixelRate = 182400000, // 182.4 MHz
                                                   .hblankMin = 680,       // From driver
                                                   .hblankTypical = 680,
                                                   .vblankMin = 4,
                                                   .maxWidth = 3280,
                                                   .maxHeight = 2464};

    // Calculate timing for a given resolution
    static CalculatedTiming calculate(const std::string &sensorName, int width, int height);

    // Calculate with explicit parameters
    static CalculatedTiming calculate(const SensorParams &params, int width, int height);

    // Get sensor params by name
    static std::optional<SensorParams> getSensorParams(const std::string &name);
};

} // namespace sanuwave

#endif // SENSOR_TIMING_CALCULATOR_H
