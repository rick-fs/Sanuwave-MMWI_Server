#ifndef SENSOR_TIMING_H_
#define SENSOR_TIMING_H_

// sensor_timing.h
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sanuwave
{


struct TemperatureReading
{
    int8_t celsius = 0;
    bool reliable = false;  // false if reads disagreed or value out of plausible range
    int readAttempts = 0;   // diagnostic: how many atomic reads were performed
};

static constexpr int8_t IMX708_TEMP_MIN = -10;
static constexpr int8_t IMX708_TEMP_MAX = 100;

struct SensorTiming
{
    int32_t hblank = 0; // horizontal blanking (pixels)
    int32_t vblank = 0; // vertical blanking (lines)
    int32_t hblankMin = 0;
    int32_t hblankMax = 0;
    int32_t vblankMin = 0;
    int32_t vblankMax = 0;
    int64_t pixelRate = 0; // pixels per second

    // Derived values (call calculate() after populating above)
    double lineTime_us = 0.0;       // microseconds per line
    double frameTime_us = 0.0;      // microseconds per frame
    double rollingShutter_us = 0.0; // rolling shutter window

    int activeWidth = 0;
    int activeHeight = 0;

    bool valid = false;

    void calculate()
    {
        if (pixelRate > 0 && activeWidth > 0)
        {
            lineTime_us = (double)(activeWidth + hblank) / (double)pixelRate * 1e6;
            frameTime_us = lineTime_us * (activeHeight + vblank);
            rollingShutter_us = lineTime_us * activeHeight;
        }
    }
};

class SensorInfo
{
  public:
    // Find subdevice for a libcamera camera ID and open it
    bool open(const std::string &libcameraCameraId);

    // Close the subdevice
    void close();

    
    std::optional<int32_t> setDigitalGain(int32_t value);

    std::optional<int32_t> setAnalogGain(int32_t value);

    // Query current timing values
    std::optional<SensorTiming> getTiming(int activeWidth, int activeHeight);

    // Returns sensor die temperature in degrees C, or nullopt if the I2C
    // transaction failed entirely. Check TemperatureReading::reliable before
    // trusting the value.
    // Currently implemented for IMX708 only.
    std::optional<TemperatureReading> getTemperature();

    // Set VBlank (returns actual value set, or nullopt on failure)
    std::optional<int32_t> setVBlank(int32_t vblank);

    // Set HBlank (usually more restricted than VBlank)
    std::optional<int32_t> setHBlank(int32_t hblank);

    bool isOpen() const
    {
        return fd >= 0;
    }

    std::string getLastError() const
    {
        return lastError;
    }
    std::string getSubdevPath() const
    {
        return subdevPath;
    }

  private:
    int fd = -1;
    std::string subdevPath;
    std::string lastError;

    // Find the V4L2 subdevice path for a sensor
    std::string findSubdevForCamera(const std::string &cameraId);

    // Query a V4L2 control with min/max
    bool queryControl(uint32_t id, int32_t &value, int32_t &min, int32_t &max);

    // Get a V4L2 control value
    std::optional<int32_t> getControl(uint32_t id);

    // Set a V4L2 control value
    std::optional<int32_t> setControl(uint32_t id, int32_t value);

    // Get pixel rate (V4L2_CID_PIXEL_RATE)
    std::optional<int64_t> getPixelRate();


    bool parseI2cInfo(const std::string& devName);

    // Perform one atomic I2C_RDWR read of a 16-bit addressed register.
    // Returns nullopt if the ioctl itself fails.
    std::optional<uint8_t> readSensorRegister(int i2cFd, uint16_t reg);
  
    std::string sensorName;  // "imx708" or "imx219", set during open()
    int i2cBus = -1;         // parsed from sysfs device name
    uint8_t i2cAddress = 0;  //
};

} // namespace sanuwave

#endif // SENSOR_TIMING_H_