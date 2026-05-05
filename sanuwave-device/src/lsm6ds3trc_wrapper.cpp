// lsm6ds3trc_wrapper.cpp
#include "lsm6ds3trc_wrapper.h"

#include "Lsm6I2cBus.h"
#include "logger.h"
#include "protocol_constants.h"

#include <chrono>
#include <sstream>

namespace sanuwave {

// Aliases so the JSON-build helpers below read the same way as the
// existing server code (e.g. command_handler.cpp uses bare
// ResponseType::DISTANCE_DATA — the file lives inside `namespace
// sanuwave` and the constants are inside `namespace sanuwave::protocol`,
// which is implicitly visible there).
namespace ResponseType = protocol::ResponseType;
namespace ImuField     = protocol::ImuField;
namespace ImuEventKind = protocol::ImuEventKind;

namespace {

uint64_t monotonicNowNs()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
               steady_clock::now().time_since_epoch()).count();
}

// Map an EventKind to its wire-string representation.
const char* eventKindString(Lsm6ds3trc::EventKind k)
{
    switch (k) {
        case Lsm6ds3trc::EventKind::SingleTap: return ImuEventKind::SINGLE_TAP;
        case Lsm6ds3trc::EventKind::DoubleTap: return ImuEventKind::DOUBLE_TAP;
        case Lsm6ds3trc::EventKind::FreeFall:  return ImuEventKind::FREE_FALL;
        case Lsm6ds3trc::EventKind::WakeUp:    return ImuEventKind::WAKE_UP;
        case Lsm6ds3trc::EventKind::None:      return "none";
    }
    return "none";
}

} // namespace

// ===========================================================================
// Construction / destruction
// ===========================================================================

Lsm6ds3trcWrapper::Lsm6ds3trcWrapper() = default;

Lsm6ds3trcWrapper::~Lsm6ds3trcWrapper()
{
    stop();
}

void Lsm6ds3trcWrapper::setNotifyCallback(NotifyCallback cb)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_notifyCallback = std::move(cb);
}

// ===========================================================================
// Lifecycle
// ===========================================================================

bool Lsm6ds3trcWrapper::init(uint8_t i2cAddress)
{
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError.clear();
    }

    if (m_streaming.load()) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = "init: cannot re-init while streaming";
        return false;
    }

    // Build a fresh chip driver each init() so re-init after error gets a
    // clean slate.
    auto bus = std::unique_ptr<ImuBus>(new Lsm6I2cBus(i2cAddress));
    m_chip   = std::unique_ptr<Lsm6ds3trc>(new Lsm6ds3trc(std::move(bus)));

    if (!m_chip->init()) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = m_chip->getLastError();
        m_chip.reset();
        m_initialized.store(false);
        return false;
    }

    // Apply default config so the part is in a usable state immediately;
    // the GUI can override later via imu_configure.
    Lsm6ds3trc::Config defaultCfg;
    if (!m_chip->configure(defaultCfg)) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = "init: default configure failed: " + m_chip->getLastError();
        m_chip.reset();
        m_initialized.store(false);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_config = defaultCfg;
        m_stats  = Stats{};
    }
    m_initialized.store(true);
    LOG_INFO << "IMU initialized on " << m_chip->busDescription() << std::endl;
    return true;
}

std::string Lsm6ds3trcWrapper::getLastError() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_lastError;
}

bool Lsm6ds3trcWrapper::configure(const Lsm6ds3trc::Config& cfg)
{
    if (!m_initialized.load()) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = "configure: not initialized";
        return false;
    }

    // If streaming, we must stop the worker before touching chip registers.
    // The worker holds no chip lock while sleeping, but during a service
    // tick it reads the FIFO; concurrent reconfig while a drain is mid-
    // transaction would corrupt state.
    const bool wasStreaming = m_streaming.load();
    if (wasStreaming) stop();

    bool ok = m_chip->configure(cfg);
    if (!ok) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = m_chip->getLastError();
    } else {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_config = cfg;
        m_lastError.clear();
    }

    if (wasStreaming && ok) start();
    return ok;
}

bool Lsm6ds3trcWrapper::start()
{
    if (!m_initialized.load()) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = "start: not initialized";
        return false;
    }
    if (m_streaming.load()) return true;  // idempotent

    m_streaming.store(true);
    m_worker = std::thread(&Lsm6ds3trcWrapper::workerLoop, this);
    LOG_INFO << "IMU streaming started" << std::endl;
    return true;
}

void Lsm6ds3trcWrapper::stop()
{
    if (!m_streaming.load() && !m_worker.joinable()) return;

    m_streaming.store(false);
    m_workerCv.notify_all();
    if (m_worker.joinable()) m_worker.join();
    LOG_INFO << "IMU streaming stopped" << std::endl;
}

// ===========================================================================
// Debug helpers
// ===========================================================================

bool Lsm6ds3trcWrapper::softReset()
{
    if (m_streaming.load()) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = "softReset: cannot reset while streaming";
        return false;
    }
    if (!m_chip) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = "softReset: not initialized";
        return false;
    }
    bool ok = m_chip->softReset();
    if (!ok) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = m_chip->getLastError();
    }
    // softReset clears the chip's m_initialized flag; mirror that here.
    m_initialized.store(false);
    return ok;
}

bool Lsm6ds3trcWrapper::readRegister(uint8_t reg, uint8_t& outValue)
{
    if (m_streaming.load()) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = "readRegister: not safe while streaming";
        return false;
    }
    if (!m_chip) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = "readRegister: not initialized";
        return false;
    }
    if (!m_chip->readRegisterRaw(reg, outValue)) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = m_chip->getLastError();
        return false;
    }
    return true;
}

bool Lsm6ds3trcWrapper::writeRegister(uint8_t reg, uint8_t value)
{
    if (m_streaming.load()) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = "writeRegister: not safe while streaming";
        return false;
    }
    if (!m_chip) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = "writeRegister: not initialized";
        return false;
    }
    if (!m_chip->writeRegisterRaw(reg, value)) {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_lastError = m_chip->getLastError();
        return false;
    }
    return true;
}

Lsm6ds3trcWrapper::Stats Lsm6ds3trcWrapper::stats() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_stats;
}

void Lsm6ds3trcWrapper::setServiceIntervalMs(int ms)
{
    if (ms < 1)   ms = 1;
    if (ms > 1000) ms = 1000;
    m_serviceIntervalMs = ms;
}

// ===========================================================================
// Worker thread
// ===========================================================================

void Lsm6ds3trcWrapper::workerLoop()
{
    LOG_INFO << "IMU worker thread started" << std::endl;

    std::vector<Lsm6ds3trc::Sample> batch;
    batch.reserve(256);

    // Worker-side FIFO kick. Mirrors the manual i2cset sequence verified
    // on bench to unstick the LSM6DS3TR-C FIFO from the
    // STATUS2=0xE0/DIFF_FIFO=0 lockup state that the in-configure()
    // bypass-then-continuous transition fails to clear:
    //   write FIFO_CTRL5 = 0x00     (full bypass: mode=0, ODR_FIFO=0)
    //   sleep 200ms                  (settle, accel/gyro already running)
    //   write FIFO_CTRL5 = 0x36     (Continuous, ODR_FIFO=416Hz)
    //
    // Doing it here, on the worker thread just before the polling loop,
    // replicates the timing of the manual test (no other I2C traffic on
    // the bus, accel/gyro have been producing samples for a while).
    if (m_chip) {
        m_chip->writeRegisterRaw(0x0A, 0x00);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        m_chip->writeRegisterRaw(0x0A, 0x36);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    while (m_streaming.load()) {
        const uint64_t nowNs = monotonicNowNs();

        // Check FIFO status once per tick — overrun bookkeeping.
        uint8_t fifoStatus = 0;
        if (m_chip->readFifoStatus(fifoStatus)) {
            constexpr uint8_t OVER_RUN_BIT = 0x40;
            if (fifoStatus & OVER_RUN_BIT) {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_stats.fifoOverruns++;
            }
        } else {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_stats.busErrors++;
            LOG_WARNING << "IMU FIFO status read failed: "
                        << m_chip->getLastError() << std::endl;
        }

        // Drain the FIFO. The chip's drainFifo bounds itself to ~1 KB; we
        // loop a few times so a one-tick burst doesn't get split across
        // many ticks at the cost of unbounded latency.
        for (int iter = 0; iter < 8 && m_streaming.load(); ++iter) {
            batch.clear();
            if (!m_chip->drainFifo(nowNs, batch)) {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_stats.busErrors++;
                LOG_WARNING << "IMU FIFO drain failed: "
                            << m_chip->getLastError() << std::endl;
                break;
            }
            if (batch.empty()) break;

            emitDataBatch(batch);

            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_stats.samplesEmitted += batch.size();
                m_stats.batchesEmitted++;
            }
        }

        // Read event sources. Tap / free-fall / wake-up registers are
        // latched (LIR set in TAP_CFG), so reading them clears them.
        if (m_streaming.load()) {
            Lsm6ds3trc::Event ev;
            if (m_chip->readEvent(nowNs, ev)) {
                if (ev.kind != Lsm6ds3trc::EventKind::None) {
                    emitEvent(ev);
                }
            } else {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_stats.busErrors++;
            }
        }

        // Sleep until next service tick, or until stop() pings the cv.
        std::unique_lock<std::mutex> lock(m_workerMutex);
        m_workerCv.wait_for(lock,
            std::chrono::milliseconds(m_serviceIntervalMs),
            [this] { return !m_streaming.load(); });
    }

    LOG_INFO << "IMU worker thread stopped" << std::endl;
}

// ===========================================================================
// JSON build + send
// ===========================================================================

void Lsm6ds3trcWrapper::emitDataBatch(const std::vector<Lsm6ds3trc::Sample>& batch)
{
    if (batch.empty()) return;

    // Snapshot config for scale factors and counters under the state lock.
    Lsm6ds3trc::AccelFs accelFs;
    Lsm6ds3trc::GyroFs  gyroFs;
    uint64_t fifoOverruns;
    uint64_t busErrors;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        accelFs      = m_config.accelFs;
        gyroFs       = m_config.gyroFs;
        fifoOverruns = m_stats.fifoOverruns;
        busErrors    = m_stats.busErrors;
    }
    const double accelLsbToG  = Lsm6ds3trc::accelSensitivityGperLsb(accelFs);
    const double gyroLsbToDps = Lsm6ds3trc::gyroSensitivityDpsPerLsb(gyroFs);

    // Build JSON manually with ostringstream — same approach used elsewhere
    // in the server (see TCPServer::sendFrameToClients header build).
    // Compact format, one line, no newlines inside.
    std::ostringstream ss;
    ss << R"({"type":")" << ResponseType::IMU_DATA << R"(",)"
       << R"(")" << ImuField::VALID << R"(":true,)"
       << R"(")" << ImuField::COUNT << R"(":)" << batch.size() << ",";

    // Helper to emit a parallel array. We avoid allocating intermediate
    // QJsonArrays — write directly into the stream.
    auto emitInt16Array = [&](const char* key, int axisIndex, bool isAccel) {
        ss << R"(")" << key << R"(":[)";
        for (size_t i = 0; i < batch.size(); ++i) {
            if (i) ss << ',';
            const int16_t v = isAccel ? batch[i].accel[axisIndex]
                                      : batch[i].gyro [axisIndex];
            ss << static_cast<int>(v);
        }
        ss << "],";
    };

    ss << R"(")" << ImuField::TIMESTAMPS_NS << R"(":[)";
    for (size_t i = 0; i < batch.size(); ++i) {
        if (i) ss << ',';
        ss << batch[i].hostTimestampNs;
    }
    ss << "],";

    emitInt16Array(ImuField::ACCEL_X, 0, true);
    emitInt16Array(ImuField::ACCEL_Y, 1, true);
    emitInt16Array(ImuField::ACCEL_Z, 2, true);
    emitInt16Array(ImuField::GYRO_X,  0, false);
    emitInt16Array(ImuField::GYRO_Y,  1, false);
    emitInt16Array(ImuField::GYRO_Z,  2, false);

    ss << R"(")" << ImuField::ACCEL_LSB_TO_G   << R"(":)" << accelLsbToG  << ","
       << R"(")" << ImuField::GYRO_LSB_TO_DPS  << R"(":)" << gyroLsbToDps << ","
       << R"(")" << ImuField::FIFO_OVERRUNS    << R"(":)" << fifoOverruns << ","
       << R"(")" << ImuField::BUS_ERRORS       << R"(":)" << busErrors
       << "}";

    // Copy the callback under the lock then invoke without it, so a slow
    // TCP send doesn't block setNotifyCallback() callers.
    NotifyCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        cb = m_notifyCallback;
    }
    if (cb) {
        cb(ss.str());
    } else {
        // Loud first time so misconfiguration in main.cpp is visible.
        static bool warned = false;
        if (!warned) {
            warned = true;
            LOG_WARNING << "IMU notify callback not set; data dropped" << std::endl;
        }
    }
}

void Lsm6ds3trcWrapper::emitEvent(const Lsm6ds3trc::Event& ev)
{
    std::ostringstream ss;
    ss << R"({"type":")" << ResponseType::IMU_EVENT << R"(",)"
       << R"(")" << ImuField::EVENT_KIND       << R"(":")" << eventKindString(ev.kind) << R"(",)"
       << R"(")" << ImuField::EVENT_TIMESTAMP  << R"(":)" << ev.hostTimestampNs << ","
       << R"(")" << ImuField::EVENT_AXIS_X     << R"(":)" << (ev.axisX ? "true" : "false") << ","
       << R"(")" << ImuField::EVENT_AXIS_Y     << R"(":)" << (ev.axisY ? "true" : "false") << ","
       << R"(")" << ImuField::EVENT_AXIS_Z     << R"(":)" << (ev.axisZ ? "true" : "false") << ","
       << R"(")" << ImuField::EVENT_SIGN       << R"(":)" << (ev.sign  ? "true" : "false") << ","
       << R"(")" << ImuField::EVENT_RAW_TAP_SRC  << R"(":)" << static_cast<int>(ev.rawTapSrc) << ","
       << R"(")" << ImuField::EVENT_RAW_WAKE_SRC << R"(":)" << static_cast<int>(ev.rawWakeSrc)
       << "}";

    NotifyCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        cb = m_notifyCallback;
    }
    if (cb) cb(ss.str());
}

} // namespace sanuwave
