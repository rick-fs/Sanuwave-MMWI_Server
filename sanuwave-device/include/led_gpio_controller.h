#ifndef LED_GPIO_CONTROLLER_H
#define LED_GPIO_CONTROLLER_H

#include "Gpio.h"
#include <memory>
#include <string>

namespace sanuwave {

/**
 * Fast GPIO-based LED control for synchronized capture.
 * 
 * The LM3643 LED drivers support a hardware torch/strobe mode where
 * I2C configures brightness and mode, but the GPIO pin gates on/off.
 * This gives microsecond-level LED switching for camera sync.
 *
 * GPIO pin mapping (from hardware design):
 *   TORCH_A_NS  (GPIO 4)  - 0x63 drivers, North+South
 *   TORCH_B_NS  (GPIO 5)  - 0x67 drivers, North+South
 *   TORCH_A_WE  (GPIO 23) - 0x63 drivers, West+East
 *   TORCH_B_WE  (GPIO 25) - 0x67 drivers, West+East
 *   STROBE_A_NS (GPIO 17) - 0x63 flash, North+South
 *   STROBE_B_NS (GPIO 6)  - 0x67 flash, North+South
 *   STROBE_A_WE (GPIO 24) - 0x63 flash, West+East
 *   STROBE_B_WE (GPIO 7)  - 0x67 flash, West+East
 *   I2C_RESET   (GPIO 18) - I2C bus reset
 */
class LedGpioController {
public:
    enum class Group {
        NS_A,   // North+South, 0x63 drivers
        NS_B,   // North+South, 0x67 drivers
        WE_A,   // West+East, 0x63 drivers
        WE_B,   // West+East, 0x67 drivers
        ALL
    };

    LedGpioController();
    ~LedGpioController();

    LedGpioController(const LedGpioController&) = delete;
    LedGpioController& operator=(const LedGpioController&) = delete;

    /// Initialize GPIO pins. Returns true if at least one pin opened.
    bool initialize();
    bool isInitialized() const { return initialized; }

    /// Returns how many torch + strobe pins are available
    int getAvailablePinCount() const { return availablePins; }

    void torchOn(Group group);
    void torchOff(Group group);
    void strobeOn(Group group);
    void strobeOff(Group group);
    void i2cReset();

    /// Release strobe GPIO pins so the kernel module can acquire them.
    /// Pins are set low and the file descriptors are closed.
    /// Call before writing to the kernel sysfs strobe_gpio attribute.
    void releaseStrobe(Group group);

    /// Reclaim strobe GPIO pins after the kernel module has released them.
    /// Call after writing "-1" to the kernel sysfs strobe_gpio attribute.
    void reclaimStrobe(Group group);

    /// Get the strobe GPIO pin numbers for a group (for passing to kernel sysfs).
    /// Returns comma-separated string, e.g., "17,6,24,7" for Group::ALL.
    std::string getStrobeGpioPins(Group group) const;

private:
    /// Try to open a single GPIO pin. Returns nullptr on failure (non-fatal).
    std::unique_ptr<Gpio::GPIO> tryOpenPin(unsigned int pin, const std::string& name);

    /// Safe set_high / set_low that checks for null
    void safeHigh(const std::unique_ptr<Gpio::GPIO>& pin);
    void safeLow(const std::unique_ptr<Gpio::GPIO>& pin);

    bool initialized = false;
    int availablePins = 0;
    bool strobeReleased = false;

    std::unique_ptr<Gpio::GPIO> torchA_NS;
    std::unique_ptr<Gpio::GPIO> torchB_NS;
    std::unique_ptr<Gpio::GPIO> torchA_WE;
    std::unique_ptr<Gpio::GPIO> torchB_WE;

    std::unique_ptr<Gpio::GPIO> strobeA_NS;
    std::unique_ptr<Gpio::GPIO> strobeB_NS;
    std::unique_ptr<Gpio::GPIO> strobeA_WE;
    std::unique_ptr<Gpio::GPIO> strobeB_WE;

    std::unique_ptr<Gpio::GPIO> i2cResetPin;
};

} // namespace sanuwave

#endif // LED_GPIO_CONTROLLER_H