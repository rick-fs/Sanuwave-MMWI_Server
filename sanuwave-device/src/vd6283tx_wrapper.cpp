// VD6283TXWrapper.cpp
#include "vd6283tx_wrapper.h"
#include "I2cMgr.h"
#include "logger.h"
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>

namespace sanuwave {

// -----------------------------------------------------------------------------
// Diagnostic flag — set to 0 once the all-zero-reads root cause is found.
// -----------------------------------------------------------------------------
#define VD6283_DIAG 0

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static uint64_t nowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

#if VD6283_DIAG
// Raw register read that bypasses the driver — used for diagnostics only.
// Mux must already be selected by the caller.
static bool diagReadReg(uint8_t i2cAddr, uint8_t reg, uint8_t& val)
{
    return I2cMgr::getInstance().writeRead(i2cAddr, &reg, 1, &val, 1);
}

static std::string hex2(uint8_t v)
{
    std::ostringstream os;
    os << "0x" << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(v);
    return os.str();
}

static void diagDumpConfig(uint8_t i2cAddr)
{
    struct Entry { uint8_t reg; const char* name; };
    static const Entry table[] = {
        { 0x00, "DEVICE_ID         " },
        { 0x01, "REVISION_ID       " },
        { 0x02, "INTERRUPT_CTRL    " },
        { 0x03, "ALS_CTRL          " },
        { 0x04, "ALS_PERIOD        " },
        { 0x1D, "ALS_EXPOSURE_M    " },
        { 0x1E, "ALS_EXPOSURE_L    " },
        { 0x25, "GAIN_CH1_RED      " },
        { 0x26, "GAIN_CH2_VIS      " },
        { 0x27, "GAIN_CH3_BLU      " },
        { 0x28, "GAIN_CH4_GRN      " },
        { 0x29, "GAIN_CH5_IR       " },
        { 0x2A, "GAIN_CH6_CLR      " },
        { 0x2D, "CHANNEL6_ENABLE   " },
        { 0x2E, "ALS_CHANNEL_ENABLE" },
        { 0x32, "PEDESTAL_VALUE    " },
        { 0x2F, "AC_CLAMP_EN       " },   // expect 0x01
        { 0x30, "DC_CLAMP_EN       " },   // expect 0x1F
        { 0x3D, "OSC10M            " },   // expect 0x01
        { 0x3E, "OSC10M_TRIM_M     " },   // expect 0x00 (DEFAULT_HF_TRIM >> 8 = 0)
        { 0x3F, "OSC10M_TRIM_L     " },   // expect 0xE3
        { 0x40, "OSC50K_TRIM       " },   // expect 0x07
        { 0x5A, "OTP_STATUS        " },   // expect 0x0A (bits 1 & 3 set)
        { 0x6B, "SEL_PD_CH1        " },   // expect 0x07
        { 0x6C, "SEL_PD_CH2        " },   // expect 0x07
        { 0x6D, "SEL_PD_CH3        " },   // expect 0x07
        { 0x6E, "SEL_PD_CH4        " },   // expect 0x1F
        { 0x6F, "SEL_PD_CH5        " },   // expect 0x0F
        { 0x70, "SEL_PD_CH6        " },   // expect 0x1F
        { 0x71, "SPARE_0           " },   // expect 0x01
        { 0x72, "SPARE_1           " },   // expect 0x3F
    };

    LOG_INFO << "VD6283TX DIAG config dump (addr=" << hex2(i2cAddr) << "):" << std::endl;
    for (const auto& e : table) {
        uint8_t v = 0xAA;
        if (diagReadReg(i2cAddr, e.reg, v)) {
            LOG_INFO << "  reg " << hex2(e.reg) << " " << e.name
                     << " = " << hex2(v) << std::endl;
        } else {
            LOG_ERROR << "  reg " << hex2(e.reg) << " " << e.name
                      << " = READ FAILED" << std::endl;
        }
    }
}
#endif // VD6283_DIAG

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

#if VD6283_DIAG
    // Dump everything immediately after init+enableChannels so we can confirm
    // the writes actually stuck (expect 0x2E=0x1F, 0x2D=0x01, gain regs != 0).
    diagDumpConfig(i2cAddress);
#endif

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

#if VD6283_DIAG
    // Verify gain register writes stuck.
    LOG_INFO << "VD6283TX DIAG: gain readback after setGainAll" << std::endl;
    for (uint8_t reg = 0x25; reg <= 0x2A; ++reg) {
        uint8_t v = 0xAA;
        if (diagReadReg(VD6283TX::DEFAULT_I2C_ADDR, reg, v)) {
            LOG_INFO << "  gain reg " << hex2(reg) << " = " << hex2(v) << std::endl;
        } else {
            LOG_ERROR << "  gain reg " << hex2(reg) << " readback FAILED" << std::endl;
        }
    }
#endif

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

#if VD6283_DIAG
    // Snapshot config right before we kick off the measurement.
    LOG_INFO << "VD6283TX DIAG: pre-start config" << std::endl;
    diagDumpConfig(VD6283TX::DEFAULT_I2C_ADDR);
#endif

    if (!sensor.startALS(VD6283TX::AlsMode::SINGLE_SHOT)) {
        setError("startALS failed");
        return false;
    }

#if VD6283_DIAG
    // Confirm ALS_CTRL latched ALS_EN.
    {
        uint8_t alsCtrl = 0xAA;
        if (diagReadReg(VD6283TX::DEFAULT_I2C_ADDR, 0x03, alsCtrl)) {
            LOG_INFO << "VD6283TX DIAG: ALS_CTRL (0x03) after startALS = "
                     << hex2(alsCtrl)
                     << " (ALS_EN bit0 = " << ((alsCtrl & 0x01) ? "SET" : "clear")
                     << ")" << std::endl;
        }
    }
#endif

    // Wait at least one full exposure period before polling.
    // The default exposure is ~80ms; without this wait, isDataReady()
    // returns true immediately on the first poll (stale INTR_ST bit).
    const uint32_t exposureMs = static_cast<uint32_t>(sensor.getExposureMs()) + 10;
    std::this_thread::sleep_for(std::chrono::milliseconds(exposureMs));

    LOG_DEBUG << "VD6283TX: exposure wait " << exposureMs
              << "ms done, polling..." << std::endl;

#if VD6283_DIAG
    // Has the measurement actually completed?
    {
        uint8_t intCtrl = 0xAA;
        if (diagReadReg(VD6283TX::DEFAULT_I2C_ADDR, 0x02, intCtrl)) {
            LOG_INFO << "VD6283TX DIAG: INTERRUPT_CTRL (0x02) after exposure wait = "
                     << hex2(intCtrl)
                     << " (INTR_ST bit1 = " << ((intCtrl & 0x02) ? "SET" : "clear")
                     << ")" << std::endl;
        } else {
            LOG_ERROR << "VD6283TX DIAG: failed to read INTERRUPT_CTRL" << std::endl;
        }
    }
#endif

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

#if VD6283_DIAG
    // Compare burst result with per-channel reads. If burst yields zeros but
    // single reads yield real counts, the 23-byte auto-increment is being
    // interrupted on the bus (another device ACKing/driving SDA mid-burst).
    LOG_INFO << "VD6283TX DIAG burst: R=" << raw.red
             << " V=" << raw.visible
             << " B=" << raw.blue
             << " G=" << raw.green
             << " IR=" << raw.ir
             << " C=" << raw.clear << std::endl;

    {
        uint32_t r=0, v=0, b=0, g=0, i=0, c=0;
        bool s1 = sensor.readChannel(VD6283TX::Channel::RED,     r);
        bool s2 = sensor.readChannel(VD6283TX::Channel::VISIBLE, v);
        bool s3 = sensor.readChannel(VD6283TX::Channel::BLUE,    b);
        bool s4 = sensor.readChannel(VD6283TX::Channel::GREEN,   g);
        bool s5 = sensor.readChannel(VD6283TX::Channel::IR,      i);
        bool s6 = sensor.readChannel(VD6283TX::Channel::CLEAR,   c);
        LOG_INFO << "VD6283TX DIAG singles ["
                 << s1 << s2 << s3 << s4 << s5 << s6 << "]: "
                 << "R=" << r
                 << " V=" << v
                 << " B=" << b
                 << " G=" << g
                 << " IR=" << i
                 << " C=" << c << std::endl;
    }
#endif

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
