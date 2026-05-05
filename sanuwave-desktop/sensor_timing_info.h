// sensor_timing_info.h
#ifndef SENSOR_TIMING_INFO_H_
#define SENSOR_TIMING_INFO_H_

#include <cstdint>

struct SensorTimingInfo
{
    int32_t hblank = 0;
    int32_t vblank = 0;
    int64_t pixelRate = 0;
    double lineTime_us = 0.0;
    double frameTime_us = 0.0;
    double rollingShutter_us = 0.0;
    int activeWidth = 0;
    int activeHeight = 0;
    bool valid = false;

    void clear()
    {
        *this = SensorTimingInfo{};
    }
};

#endif // SENSORTIMING_INFO_H_