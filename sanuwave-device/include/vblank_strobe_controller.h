// vblank_strobe_controller.h
//
// Fires strobe GPIO lines synchronized to the camera's vertical blanking
// interval so the LED illuminates the full sensor readout of each frame.
//
// Timing model:
//   - Strobe goes HIGH (strobeLeadTime_us) before readout starts,
//     i.e. near the end of the VBlank interval.
//   - Strobe goes LOW when readout ends (next VBlank start).
//   - LED is therefore OFF during VBlank and ON during the full readout.
//
// Usage:
//   1. Construct with a valid LedGpioController*.
//   2. Call setStrobeLeadTime_us() with the desired lead time.
//   3. Call setEnabled(true) to arm.
//   4. Call onFrame() from the stream worker thread on every frame.
//   5. Call setEnabled(false) or destroy to stop.
//
#pragma once

#include "led_gpio_controller.h"
#include "frame_data.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace sanuwave
{

class VBlankStrobeController
{
public:
    // Margin added to now_ns when estimating VBlank start, to absorb
    // userspace delivery jitter (500 µs is well within a typical VBlank).
    static constexpr int64_t DEFAULT_MARGIN_US = 500;

    explicit VBlankStrobeController(LedGpioController* gpio);
    ~VBlankStrobeController();

    VBlankStrobeController(const VBlankStrobeController&)            = delete;
    VBlankStrobeController& operator=(const VBlankStrobeController&) = delete;

    // Called from the stream worker thread for every decoded frame.
    // Posts timing metadata to the strobe thread. No-op when disabled.
    void onFrame(const FrameMetadata& meta);

    // Enable / disable strobe firing. Disabling immediately lowers all
    // strobe GPIOs. A pulse already in progress completes normally.
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled.load(); }

    // How far before readout starts the strobe should go HIGH.
    // Must be less than the VBlank duration; clamped automatically if not.
    // Can be updated while running; takes effect on the next frame.
    void setStrobeLeadTime_us(int64_t us);
    int64_t getStrobeLeadTime_us() const { return strobeLeadTime_us.load(); }
    void setRearmCallback(std::function<void()> cb);
private:
    void run();

    LedGpioController*      gpio;
    std::atomic<bool>       enabled{false};
    std::atomic<bool>       stopping{false};
    std::atomic<int64_t>    strobeLeadTime_us{2000};

    // Single-slot mailbox: stream worker writes, strobe thread reads.
    struct FrameSlot
    {
        uint64_t timestamp_ns      = 0;
        double   rollingShutter_us = 0.0;
        int64_t  frameDuration_us  = 0;
        int32_t  vblank            = 0;
        double   lineTime_us       = 0.0;
        bool     fresh             = false;
    };

    std::mutex              slotMutex;
    std::condition_variable slotCv;
    FrameSlot               slot;
    std::function<void()> rearmCallback;

    std::thread             strobeThread;
};

} // namespace sanuwave
