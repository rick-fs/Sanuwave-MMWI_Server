#include "led_gpio_controller.h"
#include "logger.h"
#include <unistd.h>
#include <string>
#include <fstream>
#include <filesystem>

namespace sanuwave {

LedGpioController::LedGpioController() = default;

LedGpioController::~LedGpioController()
{
    if (initialized)
    {
        torchOff(Group::ALL);
        strobeOff(Group::ALL);
    }
}

std::unique_ptr<Gpio::GPIO> LedGpioController::tryOpenPin(unsigned int pin,
                                                           const std::string& name)
{
    try
    {
        auto gpio = std::make_unique<Gpio::GPIO>(pin, Gpio::Direction::OUTPUT);
        gpio->set_low();
        ++availablePins;
        LOG_INFO << "LedGpioController: " << name << " (GPIO " << pin << ") OK" << std::endl;
        return gpio;
    }
    catch (const std::runtime_error& e)
    {
        LOG_WARNING << "LedGpioController: " << name << " (GPIO " << pin
                    << ") failed: " << e.what() << std::endl;
        return nullptr;
    }
}

bool LedGpioController::initialize()
{
    if (initialized)
        return true;

    availablePins = 0;

    // Torch pins
    torchA_NS  = tryOpenPin(Gpio::GPIO::GPIO_TORCH_A_NS,  "TORCH_A_NS");
    torchB_NS  = tryOpenPin(Gpio::GPIO::GPIO_TORCH_B_NS,  "TORCH_B_NS");
    torchA_WE  = tryOpenPin(Gpio::GPIO::GPIO_TORCH_A_WE,  "TORCH_A_WE");
    torchB_WE  = tryOpenPin(Gpio::GPIO::GPIO_TORCH_B_WE,  "TORCH_B_WE");

    // Strobe pins
    strobeA_NS = tryOpenPin(Gpio::GPIO::GPIO_STROBE_A_NS, "STROBE_A_NS");
    strobeB_NS = tryOpenPin(Gpio::GPIO::GPIO_STROBE_B_NS, "STROBE_B_NS");
    strobeA_WE = tryOpenPin(Gpio::GPIO::GPIO_STROBE_A_WE, "STROBE_A_WE");
    strobeB_WE = tryOpenPin(Gpio::GPIO::GPIO_STROBE_B_WE, "STROBE_B_WE");

    // I2C reset pin
    i2cResetPin = tryOpenPin(Gpio::GPIO::GPIO_I2C_RESET, "I2C_RESET");

    if (availablePins == 0)
    {
        LOG_ERROR << "LedGpioController: No GPIO pins available" << std::endl;
        return false;
    }

    initialized = true;
    LOG_INFO << "LedGpioController: Initialized with " << availablePins
             << "/9 pins available" << std::endl;
    return true;
}

void LedGpioController::safeHigh(const std::unique_ptr<Gpio::GPIO>& pin)
{
    if (pin) pin->set_high();
}

void LedGpioController::safeLow(const std::unique_ptr<Gpio::GPIO>& pin)
{
    if (pin) pin->set_low();
}

void LedGpioController::torchOn(Group group)
{
    switch (group)
    {
        case Group::NS_A: safeHigh(torchA_NS); break;
        case Group::NS_B: safeHigh(torchB_NS); break;
        case Group::WE_A: safeHigh(torchA_WE); break;
        case Group::WE_B: safeHigh(torchB_WE); break;
        case Group::ALL:
            LOG_INFO << "LedGpioController: torchOn ALL" << std::endl;
            safeHigh(torchA_NS);
            safeHigh(torchB_NS);
            safeHigh(torchA_WE);
            safeHigh(torchB_WE);
            break;
    }
}

void LedGpioController::torchOff(Group group)
{
    switch (group)
    {
        case Group::NS_A: safeLow(torchA_NS); break;
        case Group::NS_B: safeLow(torchB_NS); break;
        case Group::WE_A: safeLow(torchA_WE); break;
        case Group::WE_B: safeLow(torchB_WE); break;
        case Group::ALL:
            LOG_INFO << "LedGpioController: torchOff ALL" << std::endl;
            safeLow(torchA_NS);
            safeLow(torchB_NS);
            safeLow(torchA_WE);
            safeLow(torchB_WE);
            break;
    }
}

void LedGpioController::strobeOn(Group group)
{
    switch (group)
    {
        case Group::NS_A: safeHigh(strobeA_NS); break;
        case Group::NS_B: safeHigh(strobeB_NS); break;
        case Group::WE_A: safeHigh(strobeA_WE); break;
        case Group::WE_B: safeHigh(strobeB_WE); break;
        case Group::ALL:
            LOG_INFO << "LedGpioController: strobeOn ALL" << std::endl;
            safeHigh(strobeA_NS);
            safeHigh(strobeB_NS);
            safeHigh(strobeA_WE);
            safeHigh(strobeB_WE);
            break;
    }
}

void LedGpioController::strobeOff(Group group)
{
    switch (group)
    {
        case Group::NS_A: safeLow(strobeA_NS); break;
        case Group::NS_B: safeLow(strobeB_NS); break;
        case Group::WE_A: safeLow(strobeA_WE); break;
        case Group::WE_B: safeLow(strobeB_WE); break;
        case Group::ALL:
            LOG_INFO << "LedGpioController: strobeOff ALL" << std::endl;
            safeLow(strobeA_NS);
            safeLow(strobeB_NS);
            safeLow(strobeA_WE);
            safeLow(strobeB_WE);
            break;
    }
}

void LedGpioController::i2cReset()
{
    if (!i2cResetPin)
    {
        LOG_WARNING << "LedGpioController: I2C reset pin not available" << std::endl;
        return;
    }
    i2cResetPin->set_high();
    usleep(1000);
    i2cResetPin->set_low();
    usleep(1000);
    LOG_INFO << "LedGpioController: I2C bus reset" << std::endl;
}

void LedGpioController::releaseStrobe(Group group)
{
    // Set all strobe pins low, then release the file descriptors
    // by resetting the unique_ptr (which closes the GPIO handle).
    if (group == Group::ALL || group == Group::NS_A) {
        safeLow(strobeA_NS);
        strobeA_NS.reset();
    }
    if (group == Group::ALL || group == Group::NS_B) {
        safeLow(strobeB_NS);
        strobeB_NS.reset();
    }
    if (group == Group::ALL || group == Group::WE_A) {
        safeLow(strobeA_WE);
        strobeA_WE.reset();
    }
    if (group == Group::ALL || group == Group::WE_B) {
        safeLow(strobeB_WE);
        strobeB_WE.reset();
    }
    strobeReleased = true;
    LOG_INFO << "LedGpioController: strobe GPIOs released for kernel use" << std::endl;
}

void LedGpioController::reclaimStrobe(Group group)
{
    // Re-open the strobe GPIO pins after the kernel module has released them.
    if (group == Group::ALL || group == Group::NS_A)
        strobeA_NS = tryOpenPin(Gpio::GPIO::GPIO_STROBE_A_NS, "STROBE_A_NS");
    if (group == Group::ALL || group == Group::NS_B)
        strobeB_NS = tryOpenPin(Gpio::GPIO::GPIO_STROBE_B_NS, "STROBE_B_NS");
    if (group == Group::ALL || group == Group::WE_A)
        strobeA_WE = tryOpenPin(Gpio::GPIO::GPIO_STROBE_A_WE, "STROBE_A_WE");
    if (group == Group::ALL || group == Group::WE_B)
        strobeB_WE = tryOpenPin(Gpio::GPIO::GPIO_STROBE_B_WE, "STROBE_B_WE");
    strobeReleased = false;
    LOG_INFO << "LedGpioController: strobe GPIOs reclaimed from kernel" << std::endl;
}

std::string LedGpioController::getStrobeGpioPins(Group group) const
{
    // The kernel's gpio_request() uses the global GPIO number space.
    // On Pi 5 the RP1 GPIO controller (pinctrl-rp1) has a base offset
    // (e.g., 569) so BCM pin 17 is global GPIO 586.  Look it up at runtime
    // by scanning /sys/class/gpio/gpiochip*/label for "pinctrl-rp1".
    int rp1Base = 0;
    namespace fs = std::filesystem;
    for (auto& entry : fs::directory_iterator("/sys/class/gpio")) {
        std::string name = entry.path().filename().string();
        if (name.find("gpiochip") != 0) continue;

        std::ifstream labelFile(entry.path() / "label");
        std::string label;
        if (labelFile.is_open())
            std::getline(labelFile, label);

        if (label == "pinctrl-rp1") {
            std::ifstream baseFile(entry.path() / "base");
            if (baseFile.is_open())
                baseFile >> rp1Base;
            LOG_INFO << "LedGpioController: RP1 GPIO base = " << rp1Base
                     << " (from " << name << ")" << std::endl;
            break;
        }
    }

    if (rp1Base == 0)
        LOG_WARNING << "LedGpioController: could not find pinctrl-rp1 base, "
                    << "using raw pin numbers" << std::endl;

    // Return the global GPIO numbers as a comma-separated string
    // suitable for writing to the kernel sysfs strobe_gpio attribute.
    std::string result;
    auto append = [&](unsigned int pin) {
        if (!result.empty()) result += ",";
        result += std::to_string(rp1Base + pin);
    };

    if (group == Group::ALL || group == Group::NS_A)
        append(Gpio::GPIO::GPIO_STROBE_A_NS);
    if (group == Group::ALL || group == Group::NS_B)
        append(Gpio::GPIO::GPIO_STROBE_B_NS);
    if (group == Group::ALL || group == Group::WE_A)
        append(Gpio::GPIO::GPIO_STROBE_A_WE);
    if (group == Group::ALL || group == Group::WE_B)
        append(Gpio::GPIO::GPIO_STROBE_B_WE);

    return result;
}

} // namespace sanuwave