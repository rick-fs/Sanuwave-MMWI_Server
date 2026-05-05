
#include "sensor_info.h"
#include "logger.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <dirent.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

static constexpr uint16_t IMX708_REG_TEMPERATURE = 0x013a;
static constexpr int NUM_TEMP_READS = 2;  // reads must agree to be reliable


namespace sanuwave
{


// In SensorInfo::open(), call parseI2cInfo on the camera ID immediately,
// before the sysfs scan, so the fields are populated even if the subdev
// name file doesn't contain them.
bool SensorInfo::open(const std::string& libcameraCameraId)
{
    close();

    // Extract sensor name and I2C coordinates from the libcamera ID up front.
    // This covers the common case where the sysfs subdev name file only
    // contains the model (e.g. "imx708_noir") without bus/address.
    if (libcameraCameraId.find("imx708") != std::string::npos)
        sensorName = "imx708";
    else if (libcameraCameraId.find("imx219") != std::string::npos)
        sensorName = "imx219";

    parseI2cInfo(libcameraCameraId);   // best-effort; logs a warning if it can't map the bus

    subdevPath = findSubdevForCamera(libcameraCameraId);
    if (subdevPath.empty())
    {
        lastError = "Could not find V4L2 subdevice for camera: " + libcameraCameraId;
        return false;
    }

    fd = ::open(subdevPath.c_str(), O_RDWR);
    if (fd < 0)
    {
        lastError = "Failed to open " + subdevPath + ": " + strerror(errno);
        subdevPath.clear();
        return false;
    }

    LOG_INFO << "Opened sensor subdevice: " << subdevPath << std::endl;
    return true;
}

void SensorInfo::close()
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }
    subdevPath.clear();
}


std::string SensorInfo::findSubdevForCamera(const std::string& cameraId)
{
    // Extract sensor name from libcamera ID
    std::string sensorName;
    if (cameraId.find("imx708") != std::string::npos)
        sensorName = "imx708";
    else if (cameraId.find("imx219") != std::string::npos)
        sensorName = "imx219";
    else
    {
        size_t atPos = cameraId.rfind('@');
        if (atPos != std::string::npos)
        {
            size_t slashPos = cameraId.rfind('/', atPos);
            if (slashPos != std::string::npos)
                sensorName = cameraId.substr(slashPos + 1, atPos - slashPos - 1);
        }
    }

    if (sensorName.empty())
    {
        LOG_WARNING << "Could not extract sensor name from: " << cameraId << std::endl;
        return "";
    }

    LOG_INFO << "Looking for V4L2 subdevice for sensor: " << sensorName << std::endl;

    const std::string sysPath = "/sys/class/video4linux";

    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(sysPath))
        {
            std::string name = entry.path().filename().string();

            if (name.find("v4l-subdev") != 0)
                continue;

            // Read the subdevice name
            std::string namePath = entry.path().string() + "/name";
            std::ifstream nameFile(namePath);
            if (!nameFile.is_open())
                continue;

            std::string devName;
            std::getline(nameFile, devName);
            devName.erase(devName.find_last_not_of(" \t\r\n") + 1);
            nameFile.close();

            LOG_DEBUG << "Found subdev " << name << ": '" << devName << "'" << std::endl;

            if (devName.find(sensorName) == std::string::npos)
                continue;

            // Matched — record the sensor name
            std::string devPath = "/dev/" + name;
            LOG_INFO << "Matched sensor " << sensorName << " to " << devPath
                     << " (name='" << devName << "')" << std::endl;

            this->sensorName = sensorName;

            // Try to parse I2C info from the subdev name first (e.g. "imx708 11-0010").
            // On some kernels the name file only contains the model (e.g. "imx708_noir"),
            // so fall back to the parent device name.
            if (!parseI2cInfo(devName))
            {
                std::string deviceNamePath = entry.path().string() + "/device/name";
                std::ifstream deviceNameFile(deviceNamePath);
                if (deviceNameFile.is_open())
                {
                    std::string deviceName;
                    std::getline(deviceNameFile, deviceName);
                    deviceName.erase(deviceName.find_last_not_of(" \t\r\n") + 1);
                    deviceNameFile.close();
                    LOG_INFO << "Trying device/name: '" << deviceName << "'" << std::endl;
                    parseI2cInfo(deviceName);
                }
                else
                {
                    LOG_WARNING << "Could not open " << deviceNamePath << std::endl;
                }
            }

            return devPath;
        }
    }
    catch (const std::exception& e)
    {
        LOG_ERROR << "Error scanning for subdevices: " << e.what() << std::endl;
    }

    LOG_WARNING << "No V4L2 subdevice found for sensor: " << sensorName << std::endl;
    return "";
}

bool SensorInfo::queryControl(uint32_t id, int32_t& value, int32_t& min, int32_t& max)
{
    if (fd < 0)
        return false;
    
    struct v4l2_queryctrl qctrl = {};
    qctrl.id = id;
    
    if (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) < 0)
    {
        return false;
    }
    
    min = qctrl.minimum;
    max = qctrl.maximum;
    
    struct v4l2_control ctrl = {};
    ctrl.id = id;
    
    if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0)
    {
        return false;
    }
    
    value = ctrl.value;
    return true;
}

std::optional<int32_t> SensorInfo::getControl(uint32_t id)
{
    if (fd < 0)
        return std::nullopt;
    
    struct v4l2_control ctrl = {};
    ctrl.id = id;
    
    if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0)
    {
        lastError = std::string("Failed to get control: ") + strerror(errno);
        return std::nullopt;
    }
    
    return ctrl.value;
}

std::optional<int32_t> SensorInfo::setControl(uint32_t id, int32_t value)
{
    if (fd < 0)
        return std::nullopt;
    
    struct v4l2_control ctrl = {};
    ctrl.id = id;
    ctrl.value = value;
    
    if (ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
        lastError = std::string("Failed to set control: ") + strerror(errno);
        return std::nullopt;
    }
    
    // Read back actual value (driver may have clamped it)
    if (ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0)
    {
        return value; // Assume it worked
    }
    
    return ctrl.value;
}

std::optional<int64_t> SensorInfo::getPixelRate()
{
    if (fd < 0)
        return std::nullopt;
    
    struct v4l2_ext_control ctrl = {};
    ctrl.id = V4L2_CID_PIXEL_RATE;
    ctrl.size = 0;
    
    struct v4l2_ext_controls ctrls = {};
    ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    
    if (ioctl(fd, VIDIOC_G_EXT_CTRLS, &ctrls) < 0)
    {
        lastError = std::string("Failed to get pixel rate: ") + strerror(errno);
        return std::nullopt;
    }
    int64_t rate = ctrl.value64;
    return rate;
}

std::optional<SensorTiming> SensorInfo::getTiming(int activeWidth, int activeHeight)
{
    if (fd < 0)
    {
        lastError = "Subdevice not open";
        return std::nullopt;
    }
    
    SensorTiming timing;
    timing.activeWidth = activeWidth;
    timing.activeHeight = activeHeight;
    
    // Query HBlank
    if (!queryControl(V4L2_CID_HBLANK, timing.hblank, timing.hblankMin, timing.hblankMax))
    {
        LOG_WARNING << "Could not query HBlank" << std::endl;
    }
    
    // Query VBlank
    if (!queryControl(V4L2_CID_VBLANK, timing.vblank, timing.vblankMin, timing.vblankMax))
    {
        LOG_WARNING << "Could not query VBlank" << std::endl;
    }
    
    // Get pixel rate
    auto pixelRate = getPixelRate();
    if (pixelRate)
    {
        timing.pixelRate = *pixelRate;
    }
    else
    {
        LOG_WARNING << "Could not get pixel rate" << std::endl;
    }
    
    // Calculate derived values
    timing.calculate();
    timing.valid = true;
    
    LOG_INFO << "Sensor timing:" << std::endl
             << "  HBlank: " << timing.hblank << " (" << timing.hblankMin << "-" << timing.hblankMax << ")" << std::endl
             << "  VBlank: " << timing.vblank << " (" << timing.vblankMin << "-" << timing.vblankMax << ")" << std::endl
             << "  Pixel rate: " << timing.pixelRate << " Hz" << std::endl
             << "  Line time: " << timing.lineTime_us << " us" << std::endl
             << "  Frame time: " << timing.frameTime_us << " us" << std::endl
             << "  Rolling shutter: " << timing.rollingShutter_us << " us" << std::endl;
    
    return timing;
}

std::optional<int32_t> SensorInfo::setVBlank(int32_t vblank)
{
    auto result = setControl(V4L2_CID_VBLANK, vblank);
    if (result)
    {
        LOG_INFO << "Set VBlank to " << *result << " (requested " << vblank << ")" << std::endl;
    }
    return result;
}

std::optional<int32_t> SensorInfo::setHBlank(int32_t hblank)
{
    auto result = setControl(V4L2_CID_HBLANK, hblank);
    if (result)
    {
        LOG_INFO << "Set HBlank to " << *result << " (requested " << hblank << ")" << std::endl;
    }
    return result;
}

std::optional<int32_t> SensorInfo::setDigitalGain(int32_t value)
{
    auto result = setControl(V4L2_CID_DIGITAL_GAIN, value);
    if (result)
    {
        LOG_INFO << "Set digital gain register to " << *result 
                 << " (requested " << value << ")" << std::endl;
    }
    return result;
}

std::optional<int32_t> SensorInfo::setAnalogGain(int32_t value)
{
    auto result = setControl(V4L2_CID_ANALOGUE_GAIN, value);
    if (result)
    {
        LOG_INFO << "Set analog gain register to " << *result 
                 << " (requested " << value << ")" << std::endl;
    }
    return result;
}



// Parse I2C bus and address from either format:
//   sysfs device name:   "imx708 11-0010"
//   libcamera camera ID: "/base/axi/.../i2c@88000/imx708@1a"
// Returns true if both i2cBus and i2cAddress were successfully populated.
bool SensorInfo::parseI2cInfo(const std::string& devName)
{
    std::string s = devName;
    s.erase(s.find_last_not_of(" \t\r\n") + 1);

    // ── Format 1: sysfs device name "imx708 11-0010" ─────────────────────
    size_t spacePos = s.find(' ');
    if (spacePos != std::string::npos)
    {
        std::string busAddr = s.substr(spacePos + 1); // "11-0010"
        size_t dashPos = busAddr.find('-');
        if (dashPos != std::string::npos)
        {
            try
            {
                i2cBus     = std::stoi(busAddr.substr(0, dashPos));
                i2cAddress = static_cast<uint8_t>(
                                 std::stoul(busAddr.substr(dashPos + 1), nullptr, 16));
                LOG_INFO << "Sensor I2C (sysfs): bus=" << i2cBus
                         << " addr=0x" << std::hex << (int)i2cAddress << std::dec << std::endl;
                return true;
            }
            catch (const std::exception& e)
            {
                LOG_WARNING << "Failed to parse sysfs I2C info from '"
                            << devName << "': " << e.what() << std::endl;
            }
        }
    }

    // ── Format 2: libcamera ID ".../i2c@XXXXXX/sensor@ADDR" ──────────────
    // Address: last '@' gives the hex I2C address, e.g. "imx708@1a" → 0x1a
    size_t atAddr = s.rfind('@');
    if (atAddr == std::string::npos)
        return false;

    try
    {
        i2cAddress = static_cast<uint8_t>(
                         std::stoul(s.substr(atAddr + 1), nullptr, 16));
    }
    catch (const std::exception& e)
    {
        LOG_WARNING << "Failed to parse I2C address from '"
                    << devName << "': " << e.what() << std::endl;
        return false;
    }

    // Bus: scan /sys/bus/i2c/devices/ for an entry matching "{bus}-{addr:04x}",
    // e.g. "10-001a" for bus=10, addr=0x1a.
    char addrSuffix[16];
    snprintf(addrSuffix, sizeof(addrSuffix), "-%04x", (unsigned)i2cAddress);

    try
    {
        for (const auto& dev :
             std::filesystem::directory_iterator("/sys/bus/i2c/devices"))
        {
            std::string devEntry = dev.path().filename().string();
            if (devEntry.find("i2c-") == 0)
                continue;  // skip adapter entries, only want device entries
            if (devEntry.find(addrSuffix) != std::string::npos)
            {
                size_t dash = devEntry.find('-');
                if (dash != std::string::npos)
                {
                    i2cBus = std::stoi(devEntry.substr(0, dash));
                    LOG_INFO << "Found I2C device: " << devEntry
                             << " → bus=" << i2cBus << std::endl;
                    break;
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        LOG_WARNING << "Error scanning /sys/bus/i2c/devices: "
                    << e.what() << std::endl;
    }

    if (i2cBus < 0)
    {
        LOG_WARNING << "Could not find I2C bus for address 0x"
                    << std::hex << (int)i2cAddress << std::dec
                    << "; temperature reads will fail" << std::endl;
        return false;
    }

    LOG_INFO << "Sensor I2C (libcamera ID): bus=" << i2cBus
             << " addr=0x" << std::hex << (int)i2cAddress << std::dec << std::endl;
    return true;
}


// readSensorRegister
// Performs a single atomic combined-write/read using I2C_RDWR, avoiding the
// collision window that exists between separate write() and read() calls.
// ---------------------------------------------------------------------------
std::optional<uint8_t> SensorInfo::readSensorRegister(int i2cFd, uint16_t reg)
{
    uint8_t regBuf[2] = {
        static_cast<uint8_t>(reg >> 8),
        static_cast<uint8_t>(reg & 0xff)
    };
    uint8_t dataBuf = 0;

    struct i2c_msg msgs[2] = {};
    msgs[0].addr  = i2cAddress;
    msgs[0].flags = 0;           // write
    msgs[0].len   = 2;
    msgs[0].buf   = regBuf;

    msgs[1].addr  = i2cAddress;
    msgs[1].flags = I2C_M_RD;   // read
    msgs[1].len   = 1;
    msgs[1].buf   = &dataBuf;

    struct i2c_rdwr_ioctl_data data = {};
    data.msgs  = msgs;
    data.nmsgs = 2;

    if (ioctl(i2cFd, I2C_RDWR, &data) < 0)
    {
        lastError = "I2C_RDWR failed: " + std::string(strerror(errno));
        return std::nullopt;
    }

    return dataBuf;
}


// ---------------------------------------------------------------------------
// getTemperature
// ---------------------------------------------------------------------------
std::optional<TemperatureReading> SensorInfo::getTemperature()
{
    LOG_TRACE << "getTemperature: this=" << this
             << " sensorName='" << sensorName
             << "' i2cBus=" << i2cBus
             << " i2cAddress=0x" << std::hex << (int)i2cAddress << std::dec << std::endl;

    if (sensorName != "imx708")
        return std::nullopt;

    if (i2cBus < 0 || i2cAddress == 0)
    {
        lastError = "I2C bus/address not resolved";
        return std::nullopt;
    }

    std::string i2cDev = "/dev/i2c-" + std::to_string(i2cBus);
    int i2cFd = ::open(i2cDev.c_str(), O_RDWR);
    if (i2cFd < 0)
    {
        lastError = "Failed to open " + i2cDev + ": " + strerror(errno);
        return std::nullopt;
    }

    TemperatureReading result;
    int8_t readings[NUM_TEMP_READS] = {};

    for (int i = 0; i < NUM_TEMP_READS; ++i)
    {
        auto raw = readSensorRegister(i2cFd, IMX708_REG_TEMPERATURE);
        if (!raw)
        {
            ::close(i2cFd);
            return std::nullopt;  // ioctl failed entirely
        }
        readings[i] = static_cast<int8_t>(*raw);
        result.readAttempts++;
    }

    ::close(i2cFd);

    result.celsius = readings[0];

    // Reads must agree
    bool readsAgree = true;
    for (int i = 1; i < NUM_TEMP_READS; ++i)
    {
        if (readings[i] != readings[0])
        {
            readsAgree = false;
            break;
        }
    }

    // Value must be within plausible operating range
    bool inRange = (result.celsius >= IMX708_TEMP_MIN &&
                    result.celsius <= IMX708_TEMP_MAX);

    result.reliable = readsAgree && inRange;

    if (!result.reliable)
    {
        LOG_WARNING << "IMX708 temperature read unreliable:"
                    << " reads agree=" << readsAgree
                    << " in range=" << inRange
                    << " value=" << (int)result.celsius << "C" << std::endl;
    }
    else
    {
        LOG_TRACE << "IMX708 temperature: " << (int)result.celsius << "C" << std::endl;
    }

    return result;
}

} // namespace sanuwave