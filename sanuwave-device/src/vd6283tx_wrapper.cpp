// VD6283TXWrapper.cpp
#include "vd6283tx_wrapper.h"
#include "logger.h"
#include <chrono>
#include <thread>

namespace sanuwave {

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static uint64_t nowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------
VD6283TXWrapper::VD6283TXWrapper(PCA9545A* mux, uint8_t muxChannel)
    : mux(mux)
    , muxChannel(muxChannel)
    , initialized(false)
    , streaming(false)
{
}

VD6283TXWrapper::~VD6283TXWrapper()
{
    shutdown();
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------
bool VD6283TXWrapper::init(uint8_t i2cAddress)
{
    std::lock_guard<std::mutex> lock(sensorMutex);

    if (initialized) return true;

    if (!selectMux()) return false;

    if (!VD6283TX::getInstance().init(i2cAddress)) {
        setError("VD6283TX init failed");
        return false;
    }

    if (!VD6283TX::getInstance().enableChannels(VD6283TX::CH_ALL)) {
        setError("Failed to enable channels");
        return false;
    }

    initialized = true;
    LOG_INFO << "VD6283TX initialized (addr=0x"
             << std::hex << static_cast<int>(i2cAddress) << std::dec
             << ", mux=0x70 ch=" << static_cast<int>(muxChannel) << ")" << std::endl;
    return true;
}

void VD6283TXWrapper::shutdown()
{
    if (streaming) stopStreaming();

    std::lock_guard<std::mutex> lock(sensorMutex);
    if (initialized) {
        selectMux();
        VD6283TX::getInstance().deinit();
        initialized = false;
    }
}

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
bool VD6283TXWrapper::setExposureMs(float ms)
{
    if (!initialized || streaming) { setError("Cannot set exposure now"); return false; }
    std::lock_guard<std::mutex> lock(sensorMutex);
    if (!selectMux()) return false;
    if (!VD6283TX::getInstance().setExposureMs(ms)) {
        setError("Failed to set exposure");
        return false;
    }
    return true;
}

bool VD6283TXWrapper::setGainAll(VD6283TX::Gain gain)
{
    if (!initialized || streaming) { setError("Cannot set gain now"); return false; }
    std::lock_guard<std::mutex> lock(sensorMutex);
    if (!selectMux()) return false;
    if (!VD6283TX::getInstance().setGainAll(gain)) {
        setError("Failed to set gain");
        return false;
    }
    return true;
}

bool VD6283TXWrapper::setGain(VD6283TX::Channel ch, VD6283TX::Gain gain)
{
    if (!initialized || streaming) { setError("Cannot set gain now"); return false; }
    std::lock_guard<std::mutex> lock(sensorMutex);
    if (!selectMux()) return false;
    if (!VD6283TX::getInstance().setGain(ch, gain)) {
        setError("Failed to set channel gain");
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Single-shot
// -----------------------------------------------------------------------------
bool VD6283TXWrapper::readSingle(ALSData& data, uint32_t timeoutMs)
{
    if (!initialized) { setError("Not initialized"); return false; }
    std::lock_guard<std::mutex> lock(sensorMutex);

    if (!selectMux()) return false;

    auto& sensor = VD6283TX::getInstance();

    // Stop any in-progress measurement
    sensor.stopALS();
    sensor.clearInterrupt();

    if (!sensor.startALS(VD6283TX::AlsMode::SINGLE_SHOT)) {
        setError("startALS failed");
        return false;
    }

    // Wait at least one full exposure period before polling.
    // The default exposure is ~80ms; without this wait, isDataReady()
    // returns true immediately on the first poll (stale INTR_ST bit).
    const uint32_t exposureMs = static_cast<uint32_t>(sensor.getExposureMs()) + 10;
    std::this_thread::sleep_for(std::chrono::milliseconds(exposureMs));

    LOG_DEBUG << "VD6283TX: exposure wait " << exposureMs
              << "ms done, polling..." << std::endl;

    const auto deadline = nowMs() + timeoutMs;
    int pollCount = 0;
    while (!sensor.isDataReady()) {
        if (nowMs() >= deadline) {
            sensor.stopALS();
            setError("Single-shot timeout after exposure wait");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ++pollCount;
    }

    LOG_DEBUG << "VD6283TX: data ready after " << pollCount
              << " extra polls" << std::endl;

    // Read data BEFORE clearing interrupt
    VD6283TX::ChannelData raw;
    if (!sensor.readAllChannels(raw)) {
        sensor.stopALS();
        setError("readAllChannels failed");
        return false;
    }

    // Clear interrupt and stop after data is safely read
    sensor.clearInterrupt();
    sensor.stopALS();

    data.red     = raw.red;
    data.visible = raw.visible;
    data.blue    = raw.blue;
    data.green   = raw.green;
    data.ir      = raw.ir;
    data.clear   = raw.clear;
    data.valid   = true;
    data.timestamp = nowMs();

    LOG_DEBUG << "VD6283TX: red=" << data.red
              << " vis=" << data.visible
              << " blue=" << data.blue
              << " green=" << data.green
              << " ir=" << data.ir
              << " clear=" << data.clear << std::endl;

    {
        std::lock_guard<std::mutex> dlock(dataMutex);
        latestData = data;
    }
    return true;
}

bool VD6283TXWrapper::getLastMeasurement(ALSData& data)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    data = latestData;
    return latestData.valid;
}

// -----------------------------------------------------------------------------
// Streaming
// -----------------------------------------------------------------------------
bool VD6283TXWrapper::startStreaming(int intervalMs, ALSCallback callback)
{
    if (!initialized) { setError("Not initialized"); return false; }
    if (streaming)    { setError("Already streaming"); return false; }

    dataCallback = callback;
    streaming    = true;
    streamThread = std::thread(&VD6283TXWrapper::streamingLoop, this, intervalMs);
    return true;
}

bool VD6283TXWrapper::stopStreaming()
{
    if (!streaming) return true;
    streaming = false;
    if (streamThread.joinable()) streamThread.join();
    dataCallback = nullptr;
    return true;
}

void VD6283TXWrapper::streamingLoop(int intervalMs)
{
    {
        std::lock_guard<std::mutex> lock(sensorMutex);
        if (!selectMux()) { streaming = false; return; }
        if (!VD6283TX::getInstance().startALS(VD6283TX::AlsMode::CONTINUOUS)) {
            LOG_ERROR << "VD6283TX: failed to start continuous mode" << std::endl;
            streaming = false;
            return;
        }
    }

    while (streaming) {
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));

        std::lock_guard<std::mutex> lock(sensorMutex);

        if (!selectMux()) continue;

        auto& sensor = VD6283TX::getInstance();
        if (!sensor.isDataReady()) continue;

        VD6283TX::ChannelData raw;
        if (!sensor.readAllChannels(raw)) {
            LOG_ERROR << "VD6283TX: readAllChannels failed in streaming loop" << std::endl;
            continue;
        }
        sensor.clearInterrupt();

        ALSData data;
        data.red     = raw.red;
        data.visible = raw.visible;
        data.blue    = raw.blue;
        data.green   = raw.green;
        data.ir      = raw.ir;
        data.clear   = raw.clear;
        data.valid   = true;
        data.timestamp = nowMs();

        {
            std::lock_guard<std::mutex> dlock(dataMutex);
            latestData = data;
        }

        if (dataCallback) dataCallback(data);
    }

    std::lock_guard<std::mutex> lock(sensorMutex);
    selectMux();
    VD6283TX::getInstance().stopALS();
}

// -----------------------------------------------------------------------------
// Private helpers
// -----------------------------------------------------------------------------
bool VD6283TXWrapper::selectMux()
{
    if (!mux) return true;  // no mux, assume direct connection
    if (!mux->selectChannel(muxChannel)) {
        setError("Failed to select mux channel");
        return false;
    }
    return true;
}

void VD6283TXWrapper::setError(const std::string& error)
{
    lastError = error;
    LOG_ERROR << "VD6283TXWrapper: " << error << std::endl;
}

} // namespace sanuwave
