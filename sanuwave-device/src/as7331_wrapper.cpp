// as7331_wrapper.cpp
#include "as7331_wrapper.h"
#include "AS7331.h"
#include "logger.h"

namespace sanuwave {

AS7331Wrapper::AS7331Wrapper()
    : initialized(false)
    , lastUVA(0.0f)
    , lastUVB(0.0f)
    , lastUVC(0.0f)
    , lastTemp(0.0f)
    , dataValid(false)
{
}

AS7331Wrapper::~AS7331Wrapper()
{
    shutdown();
}

bool AS7331Wrapper::init(uint8_t i2cAddress)
{
    std::lock_guard<std::mutex> lock(sensorMutex);

    if (initialized)
    {
        LOG_WARNING << "AS7331 already initialized" << std::endl;
        return true;
    }

    LOG_INFO << "Initializing AS7331 UV sensor (0x"
             << std::hex << static_cast<int>(i2cAddress) << std::dec << ")" << std::endl;

    if (!AS7331::getInstance().init(i2cAddress))
    {
        lastError = "Failed to initialize AS7331 sensor";
        LOG_ERROR << lastError << std::endl;
        return false;
    }

    initialized = true;
    LOG_INFO << "AS7331 UV sensor initialized successfully" << std::endl;
    return true;
}

void AS7331Wrapper::shutdown()
{
    std::lock_guard<std::mutex> lock(sensorMutex);
    if (initialized)
    {
        AS7331::getInstance().deinit();
        initialized = false;
        LOG_INFO << "AS7331 UV sensor shut down" << std::endl;
    }
}

bool AS7331Wrapper::configure(AS7331::Gain gain,
                               AS7331::IntTime intTime,
                               AS7331::MeasMode measMode)
{
    std::lock_guard<std::mutex> lock(sensorMutex);

    if (!initialized)
    {
        LOG_ERROR << "AS7331 not initialized" << std::endl;
        return false;
    }

    if (!AS7331::getInstance().setGain(gain))
    {
        lastError = "Failed to set gain";
        LOG_ERROR << lastError << std::endl;
        return false;
    }

    if (!AS7331::getInstance().setIntegrationTime(intTime))
    {
        lastError = "Failed to set integration time";
        LOG_ERROR << lastError << std::endl;
        return false;
    }

    if (!AS7331::getInstance().setMeasurementMode(measMode))
    {
        lastError = "Failed to set measurement mode";
        LOG_ERROR << lastError << std::endl;
        return false;
    }

    return true;
}

bool AS7331Wrapper::takeMeasurement(float& uva, float& uvb, float& uvc, float& temp)
{
    std::lock_guard<std::mutex> lock(sensorMutex);

    if (!initialized)
    {
        lastError = "Sensor not initialized";
        LOG_ERROR << "takeMeasurement: " << lastError << std::endl;
        return false;
    }

    AS7331::UvData result = AS7331::getInstance().measure();

    if (!result.valid)
    {
        lastError = "Measurement failed or timed out";
        LOG_ERROR << lastError << std::endl;
        return false;
    }

    uva  = AS7331::getInstance().rawToIrradiance(result.uva, 0);
    uvb  = AS7331::getInstance().rawToIrradiance(result.uvb, 1);
    uvc  = AS7331::getInstance().rawToIrradiance(result.uvc, 2);
    temp = result.tempC;

    {
        std::lock_guard<std::mutex> dataLock(dataMutex);
        lastUVA   = uva;
        lastUVB   = uvb;
        lastUVC   = uvc;
        lastTemp  = temp;
        dataValid = true;
    }

    LOG_INFO << "UV measurement: UVA=" << uva
             << " UVB=" << uvb
             << " UVC=" << uvc
             << " Temp=" << temp << "°C" << std::endl;

    return true;
}

bool AS7331Wrapper::getLastMeasurement(float& uva, float& uvb, float& uvc, float& temp)
{
    std::lock_guard<std::mutex> lock(dataMutex);

    if (!dataValid)
    {
        lastError = "No valid cached data available";
        return false;
    }

    uva  = lastUVA;
    uvb  = lastUVB;
    uvc  = lastUVC;
    temp = lastTemp;

    return true;
}

UVData AS7331Wrapper::readUVData()
{
    UVData data;
    data.valid = takeMeasurement(data.uva, data.uvb, data.uvc, data.temp_c);

    if (data.valid)
    {
        auto now = std::chrono::system_clock::now();
        data.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count());
    }

    return data;
}

bool AS7331Wrapper::setGain(const std::string& gain)
{
    AS7331::Gain gainValue;

    if      (gain == "1")    gainValue = AS7331::Gain::GAIN_1;
    else if (gain == "2")    gainValue = AS7331::Gain::GAIN_2;
    else if (gain == "4")    gainValue = AS7331::Gain::GAIN_4;
    else if (gain == "8")    gainValue = AS7331::Gain::GAIN_8;
    else if (gain == "16")   gainValue = AS7331::Gain::GAIN_16;
    else if (gain == "32")   gainValue = AS7331::Gain::GAIN_32;
    else if (gain == "64")   gainValue = AS7331::Gain::GAIN_64;
    else if (gain == "128")  gainValue = AS7331::Gain::GAIN_128;
    else if (gain == "256")  gainValue = AS7331::Gain::GAIN_256;
    else if (gain == "512")  gainValue = AS7331::Gain::GAIN_512;
    else if (gain == "1024") gainValue = AS7331::Gain::GAIN_1024;
    else if (gain == "2048") gainValue = AS7331::Gain::GAIN_2048;
    else
    {
        lastError = "Invalid gain: " + gain;
        return false;
    }

    std::lock_guard<std::mutex> lock(sensorMutex);
    if (!initialized)
    {
        lastError = "Sensor not initialized";
        return false;
    }

    return AS7331::getInstance().setGain(gainValue);
}

bool AS7331Wrapper::setIntegrationTime(const std::string& time)
{
    AS7331::IntTime intTime;

    if      (time == "1ms")    intTime = AS7331::IntTime::TIME_1MS;
    else if (time == "2ms")    intTime = AS7331::IntTime::TIME_2MS;
    else if (time == "4ms")    intTime = AS7331::IntTime::TIME_4MS;
    else if (time == "8ms")    intTime = AS7331::IntTime::TIME_8MS;
    else if (time == "16ms")   intTime = AS7331::IntTime::TIME_16MS;
    else if (time == "32ms")   intTime = AS7331::IntTime::TIME_32MS;
    else if (time == "64ms")   intTime = AS7331::IntTime::TIME_64MS;
    else if (time == "128ms")  intTime = AS7331::IntTime::TIME_128MS;
    else if (time == "256ms")  intTime = AS7331::IntTime::TIME_256MS;
    else if (time == "512ms")  intTime = AS7331::IntTime::TIME_512MS;
    else if (time == "1024ms") intTime = AS7331::IntTime::TIME_1024MS;
    else
    {
        lastError = "Invalid integration time: " + time;
        return false;
    }

    std::lock_guard<std::mutex> lock(sensorMutex);
    if (!initialized)
    {
        lastError = "Sensor not initialized";
        return false;
    }

    return AS7331::getInstance().setIntegrationTime(intTime);
}

bool AS7331Wrapper::setMode(const std::string& mode)
{
    AS7331::MeasMode measMode;

    if      (mode == "cmd"  || mode == "command")    measMode = AS7331::MeasMode::CMD;
    else if (mode == "cont" || mode == "continuous") measMode = AS7331::MeasMode::CONT;
    else if (mode == "syns")                         measMode = AS7331::MeasMode::SYNS;
    else if (mode == "synd" || mode == "sync")       measMode = AS7331::MeasMode::SYND;
    else
    {
        lastError = "Invalid mode: " + mode + ". Valid: cmd, cont, syns, synd";
        return false;
    }

    std::lock_guard<std::mutex> lock(sensorMutex);
    if (!initialized)
    {
        lastError = "Sensor not initialized";
        return false;
    }

    return AS7331::getInstance().setMeasurementMode(measMode);
}

} // namespace sanuwave
