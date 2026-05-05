// vblank_strobe_controller.cpp
#include "vblank_strobe_controller.h"
#include "logger.h"
#include <time.h>

namespace sanuwave
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint64_t clockMonotonicNow_ns()
{
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
         + static_cast<uint64_t>(ts.tv_nsec);
}

static void absNanosleep(uint64_t target_ns)
{
    struct timespec ts{};
    ts.tv_sec  = static_cast<time_t>(target_ns / 1'000'000'000ULL);
    ts.tv_nsec = static_cast<long>  (target_ns % 1'000'000'000ULL);
    // Loop handles spurious wakeups from signals
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR)
        ;
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

VBlankStrobeController::VBlankStrobeController(LedGpioController* gpio)
    : gpio(gpio)
{
    strobeThread = std::thread(&VBlankStrobeController::run, this);
}

VBlankStrobeController::~VBlankStrobeController()
{
    // Disable first so the thread stops accepting new frames
    enabled.store(false);

    // Wake and join the strobe thread
    {
        std::lock_guard<std::mutex> lk(slotMutex);
        stopping.store(true);
        slot.fresh = true;
    }
    slotCv.notify_one();

    if (strobeThread.joinable())
        strobeThread.join();

    if (gpio)
        gpio->strobeOff(LedGpioController::Group::ALL);
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void VBlankStrobeController::setEnabled(bool en)
{
    enabled.store(en);
    if (!en && gpio)
        gpio->strobeOff(LedGpioController::Group::ALL);

    LOG_INFO << "VBlankStrobeController: " << (en ? "enabled" : "disabled") << std::endl;
}

void VBlankStrobeController::setStrobeLeadTime_us(int64_t us)
{
    strobeLeadTime_us.store(us);
    LOG_INFO << "VBlankStrobeController: strobe lead time = " << us << " µs" << std::endl;
}

void VBlankStrobeController::onFrame(const FrameMetadata& meta)
{
    if (!enabled.load())
        return;

    if (meta.timestamp_ns == 0 || meta.rollingShutter_us <= 0.0
        || meta.frameDuration_us <= 0 || meta.lineTime_us <= 0.0)
    {
        LOG_WARNING << "VBlankStrobeController: dropping frame — insufficient timing data"
                    << " timestamp_ns=" << meta.timestamp_ns
                    << " rollingShutter_us=" << meta.rollingShutter_us
                    << " frameDuration_us=" << meta.frameDuration_us
                    << " lineTime_us=" << meta.lineTime_us
                    << std::endl;
        return;
    }

    {
        std::lock_guard<std::mutex> lk(slotMutex);
        slot.timestamp_ns      = meta.timestamp_ns;
        slot.rollingShutter_us = meta.rollingShutter_us;
        slot.frameDuration_us  = meta.frameDuration_us;
        slot.vblank            = meta.vblank;
        slot.lineTime_us       = meta.lineTime_us;
        slot.fresh             = true;
    }
    LOG_DEBUG << "VBlankStrobeController: onFrame ts=" << meta.timestamp_ns
              << " frameDuration=" << meta.frameDuration_us << "µs"
              << " vblank=" << meta.vblank << " lines"
              << " lineTime=" << meta.lineTime_us << "µs"
              << std::endl;
    slotCv.notify_one();
}

void VBlankStrobeController::setRearmCallback(std::function<void()> cb)
{
    rearmCallback = std::move(cb);
}

// ---------------------------------------------------------------------------
// Strobe thread
// ---------------------------------------------------------------------------
void VBlankStrobeController::run()
{
    LOG_INFO << "VBlankStrobeController: strobe thread started" << std::endl;

    while (true)
    {
        {
            std::unique_lock<std::mutex> lk(slotMutex);
            slotCv.wait(lk, [this]{ return slot.fresh || stopping.load(); });
            if (stopping.load())
                break;
            slot.fresh = false;
        }

        if (!enabled.load())
            continue;

        // ---------------------------------------------------------------
        // Timing model (readout-illumination mode):
        //
        //   now_ns ≈ end-of-readout + delivery jitter
        //          ≈ start of VBlank for frame N
        //
        //   VBlank duration = vblank_lines × lineTime_us
        //
        //   We want the strobe HIGH for the entire readout of frame N+1,
        //   turning on (preroll_us) before readout starts:
        //
        //   vblank_start  = now_ns + margin
        //   strobe_on     = vblank_start + vblank_duration_ns - preroll_ns
        //   strobe_off    = strobe_on + rollingShutter_ns + preroll_ns
        //                 = vblank_start + vblank_duration_ns + rollingShutter_ns
        //                 ≈ vblank_start + frameDuration_ns  (next VBlank start)
        //
        //   The strobe is OFF during VBlank and ON during the full readout.
        // ---------------------------------------------------------------

        int64_t  vblank_lines     = 0;
        double   lineTime_us      = 0.0;
        double   rollingShutter_us = 0.0;

        {
            std::lock_guard<std::mutex> lk(slotMutex);
            vblank_lines      = slot.vblank;
            lineTime_us       = slot.lineTime_us;
            rollingShutter_us = slot.rollingShutter_us;
        }

        const int64_t margin_ns        = DEFAULT_MARGIN_US * 1000LL;
        const int64_t leadTime_ns      = strobeLeadTime_us.load() * 1000LL;

        // VBlank duration in nanoseconds
        const int64_t vblank_dur_ns =
            static_cast<int64_t>(vblank_lines * lineTime_us * 1000.0);

        // Sanity: preroll must be less than VBlank duration
        if (leadTime_ns >= vblank_dur_ns)
        {
            LOG_WARNING << "VBlankStrobeController: strobe lead time (" << leadTime_ns / 1000
                        << " µs) >= vblank duration (" << vblank_dur_ns / 1000
                        << " µs) — clamping to half vblank" << std::endl;
        }
        const int64_t effective_leadTime_ns = std::min(leadTime_ns, vblank_dur_ns / 2);

        uint64_t now_ns         = clockMonotonicNow_ns();
        uint64_t vblank_start   = now_ns + static_cast<uint64_t>(margin_ns);
        uint64_t strobe_on_ns   = vblank_start
                                  + static_cast<uint64_t>(vblank_dur_ns - effective_leadTime_ns);
        uint64_t strobe_off_ns  = vblank_start
                                  + static_cast<uint64_t>(vblank_dur_ns)
                                  + static_cast<uint64_t>(rollingShutter_us * 1000.0);

        LOG_DEBUG << "VBlankStrobeController:"
                  << " vblank_dur=" << vblank_dur_ns / 1000 << "µs"
                  << " leadTime=" << effective_leadTime_ns / 1000 << "µs"
                  << " rolling=" << rollingShutter_us << "µs"
                  << " strobe_on in " << (strobe_on_ns - now_ns) / 1000 << "µs"
                  << " strobe_on_dur=" << (strobe_off_ns - strobe_on_ns) / 1000 << "µs"
                  << std::endl;

        // Sleep until strobe-on time (end of VBlank minus preroll)
        absNanosleep(strobe_on_ns);

        if (!enabled.load())
            continue;
        LOG_TRACE << "VBlankStrobeController: strobeOn"
                 << " strobe_on_ns=" << strobe_on_ns
                 << " now=" << clockMonotonicNow_ns()
                 << " late_by=" << (int64_t)(clockMonotonicNow_ns() - strobe_on_ns) / 1000 << "µs"
                 << std::endl;
        gpio->strobeOn(LedGpioController::Group::ALL);

        // Sleep until end of readout (next VBlank start)
        absNanosleep(strobe_off_ns);
        LOG_TRACE << "VBlankStrobeController: strobeOff"
                 << " strobe_off_ns=" << strobe_off_ns
                 << " now=" << clockMonotonicNow_ns()
                 << " late_by=" << (int64_t)(clockMonotonicNow_ns() - strobe_off_ns) / 1000 << "µs"
                 << std::endl;
        gpio->strobeOff(LedGpioController::Group::ALL);

        // Re-arm LM3643 during VBlank — I2C latency here doesn't affect
        // GPIO timing since the next strobe_on is a full VBlank away.
        if (rearmCallback)
            rearmCallback();
    }

    if (gpio)
        gpio->strobeOff(LedGpioController::Group::ALL);

    LOG_INFO << "VBlankStrobeController: strobe thread exiting" << std::endl;
}

} // namespace sanuwave
