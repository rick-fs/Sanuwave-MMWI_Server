// camera_base.cpp
// Common implementation for libcamera-based camera drivers

#include "camera_base.h"
#include "logger.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <linux/media.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

using namespace libcamera;

namespace sanuwave
{


// ============================================================================
// COMMON VALIDATION HELPERS
// ============================================================================

float CameraBase::validateWbGain(float gain) const
{
    return std::clamp(gain, 0.1f, 8.0f);
}

float CameraBase::validateLensPosition(float position) const
{
    auto range = getLensPositionRange();
    if (range.valid)
        return std::clamp(position, range.min, range.max);
    return std::clamp(position, 0.0f, 32.0f); // fallback
}

int32_t CameraBase::validateBrightness(int32_t brightness) const
{
    return std::clamp(brightness, -100, 100);
}

float CameraBase::validateImageQuality(float value) const
{
    return std::clamp(value, 0.0f, 2.0f);
}

float CameraBase::validateDigitalGain(float gain) const
{
    // Default range - overridden by sensor-specific implementations
    return std::clamp(gain, 1.0f, 16.0f);
}


// ============================================================================
// CONTROL APPLICATION HELPERS
// ============================================================================

void CameraBase::applyExposureControls(libcamera::ControlList &controls, 
                                       const CaptureSettings &settings) const
{
    if (!settings.autoExposure)
    {
        controls.set(controls::AeEnable, false);
        
        if (settings.exposureTime_us > 0)
        {
            int32_t validated = validateExposureTime(settings.exposureTime_us);
            LOG_INFO << "Set ExposureTime control to: " << validated << " us" << std::endl;  
            controls.set(controls::ExposureTime, validated);
        }
        else
        {
            int32_t defaultExposure = getDefaultExposureTime();
            controls.set(controls::ExposureTime, defaultExposure);
        }
    }
    else
    {
        // Auto exposure mode
        controls.set(controls::AeEnable, true);
        
        if (settings.exposureTime_us > 0)
        {
            int32_t validated = validateExposureTime(settings.exposureTime_us);
            controls.set(controls::ExposureTime, validated);
        }
        
        // Apply EV compensation whenever AE is enabled
        if (std::abs(settings.evCompensation) > 0.01f)
        {
            float evClamped = std::clamp(settings.evCompensation, -2.0f, 2.0f);
            controls.set(controls::ExposureValue, evClamped);
            LOG_INFO << "  -> EV compensation = " << evClamped << std::endl;
        }
    }
}

void CameraBase::applyGainControls(libcamera::ControlList &controls, 
                                   const CaptureSettings &settings) const
{
    // Analog gain
    if (settings.autoAnalogGain)
    {
        controls.set(controls::AnalogueGainMode, controls::AnalogueGainModeAuto);
    }
    else
    {
        controls.set(controls::AnalogueGainMode, controls::AnalogueGainModeManual);
        float validated = validateAnalogGain(settings.analogGain);
        controls.set(controls::AnalogueGain, validated);
        LOG_INFO << "Set AnalogueGain control to: " << validated << std::endl;  // ADD THIS
    }
    
    if (settings.digitalGain > 1.0f)
    {
        applyDirectDigitalGain(settings.digitalGain);
    }
}

void CameraBase::applyWhiteBalanceControls(libcamera::ControlList &controls, 
                                           const CaptureSettings &settings) const
{
    if (settings.autoWhiteBalance)
    {
        controls.set(controls::AwbEnable, true);
    }
    else
    {
        controls.set(controls::AwbEnable, false);
        
        float redValidated = validateWbGain(settings.redGain);
        float blueValidated = validateWbGain(settings.blueGain);
        
        controls.set(controls::ColourGains,
                    libcamera::Span<const float, 2>({redValidated, blueValidated}));
    }
}


void CameraBase::applyFocusControls(libcamera::ControlList &controls, 
                                    const CaptureSettings &settings) const
{
    if (settings.autoFocus)
    {
        if (streaming)
        {
            // Continuous AF for streaming — algorithm runs autonomously
            // without needing repeated triggers
            controls.set(controls::AfMode, controls::AfModeContinuous);
        }
        else
        {
            // Single-shot AF for capture — trigger once and wait
            controls.set(controls::AfMode, controls::AfModeAuto);
            controls.set(controls::AfTrigger, controls::AfTriggerStart);
        }
    }
    else
    {
        // Manual focus mode
        controls.set(controls::AfMode, controls::AfModeManual);
        
        float validated = validateLensPosition(settings.lensPosition);
        controls.set(controls::LensPosition, validated);
    }
}



void CameraBase::applyAdvancedControls(libcamera::ControlList &controls, 
                                       const CaptureSettings &settings) const
{
    // Noise reduction / denoise mode
    if (settings.denoiseMode == "off")
    {
        controls.set(controls::draft::NoiseReductionMode, 
                    controls::draft::NoiseReductionModeOff);
    }
    else if (settings.denoiseMode == "fast")
    {
        controls.set(controls::draft::NoiseReductionMode, 
                    controls::draft::NoiseReductionModeFast);
    }
    else if (settings.denoiseMode == "high_quality")
    {
        controls.set(controls::draft::NoiseReductionMode, 
                    controls::draft::NoiseReductionModeHighQuality);
    }
    
    if (settings.hdrMode)
    {
        // HDR mode - implementation depends on sensor support
    }
    
}

void CameraBase::extractConvergedValues(libcamera::Request *request, 
                                        ConvergedSettings *out)
{
    if (!request || !out)
     {
        LOG_ERROR << "extractConvergedValues: null request or out" << std::endl;
        return;
     }

    const ControlList &metadata = request->metadata();
    
    auto expOpt = metadata.get(controls::ExposureTime);
    if (expOpt)
    {
        out->exposureTime_us = *expOpt;
        LOG_TRACE << "  Extracted exposure: " << out->exposureTime_us << " us" << std::endl;
    }
    else
    {
        LOG_WARNING << "  No ExposureTime in metadata" << std::endl;
    }
    
    auto gainOpt = metadata.get(controls::AnalogueGain);
    if (gainOpt)
    {
        out->analogGain = *gainOpt;
        LOG_INFO << "  Extracted analog gain: " << out->analogGain << std::endl;
    }
    else
    {
        LOG_WARNING << "  No AnalogueGain    in metadata" << std::endl;
    }
    
    auto colourGainsOpt = metadata.get(controls::ColourGains);
    if (colourGainsOpt)
    {
        out->redGain = (*colourGainsOpt)[0];
        out->blueGain = (*colourGainsOpt)[1];
        LOG_INFO << "  Converged WB gains: R=" << out->redGain 
                 << " B=" << out->blueGain << std::endl;
    } 
    else
    {
        LOG_WARNING << "  No ColourGains in metadata" << std::endl;
    }
    
    out->valid = true;
}

void CameraBase::applyConvergedSettings(libcamera::ControlList &controls,
                                        const ConvergedSettings &converged,
                                        const CaptureSettings &settings)
{
    LOG_INFO << "Applying converged settings..." << std::endl;
    LOG_INFO << "  Original settings: " << settings << std::endl;
    LOG_INFO << "  Converged settings: " << converged << std::endl;

    if (!converged.valid)
    {
        LOG_WARNING << "No valid converged settings, using original settings" << std::endl;
        applyExposureControls(controls, settings);
        applyGainControls(controls, settings);
        applyWhiteBalanceControls(controls, settings);
        return;
    }
    
    // Disable auto modes and use converged values
    controls.set(controls::AeEnable, false);
    controls.set(controls::AwbEnable, false);
    
    // Apply converged exposure
    if (settings.autoExposure && converged.exposureTime_us > 0)
    {
        controls.set(controls::ExposureTime, converged.exposureTime_us);
        LOG_INFO << "Using converged exposure: " << converged.exposureTime_us << " us" << std::endl;
    }
    else if (settings.exposureTime_us > 0)
    {
        int32_t validated = validateExposureTime(settings.exposureTime_us);
        controls.set(controls::ExposureTime, validated);
        LOG_INFO << "Using manual exposure: " << validated << " us" << std::endl;
    }
    else if (converged.exposureTime_us > 0)
    {
        controls.set(controls::ExposureTime, converged.exposureTime_us);
        LOG_INFO << "Using converged exposure (fallback): " << converged.exposureTime_us << " us" << std::endl;
    }
    
    // Apply converged gain
    if (settings.autoAnalogGain && converged.analogGain > 0)
    {
        controls.set(controls::AnalogueGain, converged.analogGain);
        LOG_INFO << "Using converged gain: " << converged.analogGain << std::endl;
    }
    else
    {
        float validated = validateAnalogGain(settings.analogGain);
        controls.set(controls::AnalogueGain, validated);
        LOG_INFO << "Using manual gain: " << validated << std::endl;
    }
    
    // Apply converged white balance
    if (settings.autoWhiteBalance && converged.redGain > 0 && converged.blueGain > 0)
    {
        controls.set(controls::ColourGains,
                    libcamera::Span<const float, 2>({converged.redGain, converged.blueGain}));
        LOG_INFO << "Using converged WB: R=" << converged.redGain 
                 << " B=" << converged.blueGain << std::endl;
    }
    else
    {
        float redValidated = validateWbGain(settings.redGain);
        float blueValidated = validateWbGain(settings.blueGain);
        controls.set(controls::ColourGains,
                    libcamera::Span<const float, 2>({redValidated, blueValidated}));
        LOG_INFO << "Using manual WB: R=" << redValidated 
                 << " B=" << blueValidated << std::endl;
    }
}

// ============================================================================
// CORE CAMERA METHODS
// ============================================================================

CameraBase::CameraBase() : jpegEncoder(JpegEncoderFactory::createDefaultEncoder())
{
}

CameraBase::~CameraBase()
{
    cleanup();
}

// ----------------------------------------------------------------------------
// resolveKernelStrobeSysfsPath
//
// Each RP1 CSI port instantiates its own rp1-cfe device (1f00110000.csi and
// 1f00128000.csi on the Pi 5). The patched kernel module exposes a
// sanuwave_strobe sysfs directory under each CFE platform device. Writing
// to the wrong CFE's sysfs is a silent no-op — the camera's ISR lives on
// the other instance.  We resolve per-camera at init time and cache.
//
// The sensor-to-CFE binding is expressed via the V4L2 media controller:
// each rp1-cfe instance has its own /dev/mediaN node whose bus_info names
// the CFE's platform address (e.g. "platform:1f00110000.csi"), and whose
// entity list includes the sensor that's physically wired to that CSI port.
//
// Algorithm:
//   1. Derive the sensor model token ("imx708" / "imx219") from
//      getSensorType(). Entity names on this kernel are "imx708_noir" or
//      "imx219 11-0010" — substring-match on the model is sufficient.
//   2. For each /dev/mediaN, MEDIA_IOC_DEVICE_INFO to check driver=="rp1-cfe"
//      and read bus_info.
//   3. MEDIA_IOC_ENUM_ENTITIES over that media device; if any entity name
//      contains the sensor model, this is our CFE.
//   4. Extract the platform address from bus_info ("platform:<addr>") and
//      build the sysfs path /sys/devices/platform/axi/*/<addr>/sanuwave_strobe.
//      Verify it exists (patched module loaded), cache, done.
// ----------------------------------------------------------------------------
bool CameraBase::resolveKernelStrobeSysfsPath()
{
    namespace fs = std::filesystem;

    kernelStrobeSysfsPath.clear();

    // Derive the sensor model token. getSensorType() returns e.g. "imx708"
    // or "imx219"; we lowercase for robust substring matching against
    // entity names like "imx708_noir" or "imx219 11-0010".
    std::string sensorModel = getSensorType();
    std::transform(sensorModel.begin(), sensorModel.end(),
                   sensorModel.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (sensorModel.empty() || sensorModel == "unknown")
    {
        setError("resolveKernelStrobeSysfsPath: getSensorType() returned '"
                 + sensorModel + "' — cannot match media entities");
        return false;
    }

    LOG_INFO << "resolveKernelStrobeSysfsPath: matching sensor model '"
             << sensorModel << "' (cameraId=" << cameraId << ")" << std::endl;

    // Walk /dev/media* and look for the rp1-cfe media device whose
    // entities include our sensor.
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/dev", ec))
    {
        if (ec) break;

        std::string fname = entry.path().filename().string();
        if (fname.rfind("media", 0) != 0)
            continue;
        // Must be "media" + digits, otherwise skip.
        if (fname.size() == 5) continue;
        bool allDigits = true;
        for (size_t i = 5; i < fname.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(fname[i])))
                { allDigits = false; break; }
        if (!allDigits) continue;

        int fd = ::open(entry.path().c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0)
        {
            LOG_WARNING << "resolveKernelStrobeSysfsPath: could not open "
                        << entry.path().string() << ": " << std::strerror(errno)
                        << " — skipping" << std::endl;
            continue;
        }

        struct media_device_info info{};
        if (::ioctl(fd, MEDIA_IOC_DEVICE_INFO, &info) < 0)
        {
            ::close(fd);
            continue;
        }

        if (std::strcmp(info.driver, "rp1-cfe") != 0)
        {
            ::close(fd);
            continue;
        }

        // info.bus_info is e.g. "platform:1f00110000.csi". The piece we
        // need for the sysfs path is everything after "platform:".
        std::string busInfo(info.bus_info,
                            strnlen(info.bus_info, sizeof(info.bus_info)));
        const std::string kPrefix = "platform:";
        if (busInfo.rfind(kPrefix, 0) != 0)
        {
            LOG_WARNING << "resolveKernelStrobeSysfsPath: "
                        << entry.path().string()
                        << " unexpected bus_info='" << busInfo
                        << "' — skipping" << std::endl;
            ::close(fd);
            continue;
        }
        std::string platformAddr = busInfo.substr(kPrefix.size());

        // Enumerate entities; look for one whose name contains our
        // sensor model token (case-insensitive).
        bool matchedSensor = false;
        struct media_entity_desc ent{};
        ent.id = MEDIA_ENT_ID_FLAG_NEXT;  // get first entity
        while (::ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &ent) >= 0)
        {
            std::string entName(ent.name,
                                strnlen(ent.name, sizeof(ent.name)));
            std::string lower = entName;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower.find(sensorModel) != std::string::npos)
            {
                matchedSensor = true;
                LOG_INFO << "resolveKernelStrobeSysfsPath: "
                         << entry.path().string() << " (" << platformAddr
                         << ") contains entity '" << entName << "'"
                         << std::endl;
                break;
            }
            // Advance to next entity.
            ent.id |= MEDIA_ENT_ID_FLAG_NEXT;
        }
        ::close(fd);

        if (!matchedSensor)
            continue;

        // Found the CFE for this sensor. Locate its platform device in
        // sysfs and verify the patched module's sysfs interface exists.
        // Platform devices live under /sys/devices/platform/**; we glob
        // by suffix so kernel enumeration changes (e.g. nested pcie
        // bridges) don't break us.
        fs::path cfeDevPath;
        for (const auto& sub : fs::recursive_directory_iterator(
                 "/sys/devices/platform",
                 fs::directory_options::skip_permission_denied, ec))
        {
            if (ec) { ec.clear(); continue; }
            if (sub.path().filename().string() == platformAddr &&
                fs::is_directory(sub.path(), ec))
            {
                cfeDevPath = sub.path();
                break;
            }
            ec.clear();
        }
        ec.clear();

        if (cfeDevPath.empty())
        {
            setError("resolveKernelStrobeSysfsPath: media device matched "
                     "sensor but sysfs path '" + platformAddr +
                     "' not found under /sys/devices/platform");
            return false;
        }

        fs::path strobeDir = cfeDevPath / "sanuwave_strobe";
        if (!fs::exists(strobeDir, ec))
        {
            setError("resolveKernelStrobeSysfsPath: CFE " +
                     cfeDevPath.string() +
                     " has no sanuwave_strobe directory — is the patched "
                     "rp1-cfe module loaded on this CFE?");
            return false;
        }

        kernelStrobeSysfsPath = strobeDir.string();
        LOG_INFO << "resolveKernelStrobeSysfsPath: " << cameraId
                 << " -> " << kernelStrobeSysfsPath << std::endl;
        return true;
    }

    setError("resolveKernelStrobeSysfsPath: no rp1-cfe media device lists "
             "sensor '" + sensorModel + "' as an entity (cameraId=" +
             cameraId + ")");
    return false;
}

bool CameraBase::init(libcamera::CameraManager* cameraManager, int index)
{
    try
    {
        LOG_INFO << "Initializing camera (sensor: " << getSensorType() << ")..." << std::endl;
        
        // Check for cameras
        if (cameraManager->cameras().empty())
        {
            setError("No cameras detected");
            return false;
        }

        if (index >= static_cast<int>(cameraManager->cameras().size()))
        {
            setError("Camera index out of range: " + std::to_string(index));
            return false;
        }

        // Get camera by index
        camera = cameraManager->cameras()[index];
        cameraId = camera->id();

        LOG_INFO << "Found camera: " << cameraId << std::endl;

        // Acquire camera
        int ret = camera->acquire();
        if (ret)
        {
            setError("Failed to acquire camera: " + cameraId);
            return false;
        }

        // Log available controls
        LOG_INFO << "Available controls:" << std::endl;
        for (const auto& [id, info] : camera->controls())
        {
            LOG_INFO << "  " << id->name() 
                    << " min=" << info.min().toString()
                    << " max=" << info.max().toString() << std::endl;
        }

        // Get camera capabilities
        std::unique_ptr<libcamera::CameraConfiguration> testConfig =
            camera->generateConfiguration({StreamRole::StillCapture});

        if (testConfig && !testConfig->empty())
        {
            StreamConfiguration &cfg = testConfig->at(0);
            maxResolution = cv::Size(cfg.size.width, cfg.size.height);
            
            LOG_INFO << "  Detected resolution: " << maxResolution.width 
                     << "x" << maxResolution.height << std::endl;
            LOG_INFO << "Default orientation: " << static_cast<int>(testConfig->orientation) << std::endl;
        }
        else
        {
            maxResolution = getDefaultResolution();
            LOG_INFO << "  Using default resolution: " << maxResolution.width 
                     << "x" << maxResolution.height << std::endl;
        }
        if (timingHelper.open(cameraId))
        {
            updateSensorTiming();
        }
        else
        {
            LOG_WARNING << "Could not open sensor timing interface: " 
                        << timingHelper.getLastError() << std::endl;
            LOG_WARNING << "Blanking controls will not be available" << std::endl;
        }

        initialized = true;
        LOG_INFO << "Camera initialized successfully" << std::endl;
        LOG_INFO << "  Camera ID: " << cameraId << std::endl;
        LOG_INFO << "  Sensor type: " << getSensorType() << std::endl;
        LOG_INFO << "  Max resolution: " << maxResolution.width << "x" 
                 << maxResolution.height << std::endl;

        // Resolve the sanuwave_strobe sysfs path for this camera's CFE.
        // Fail init if it can't be found — the kernel-strobe UVBF path
        // depends on this and silent fallback would be worse than a
        // clear startup failure (see Sanuwave kernel frame-sync design).
        if (!resolveKernelStrobeSysfsPath())
        {
            LOG_ERROR << "Camera init failed: " << getLastError() << std::endl;
            initialized = false;
            return false;
        }

        return true;
    }
    catch (const std::exception &e)
    {
        setError(std::string("Exception during init: ") + e.what());
        return false;
    }
}

void CameraBase::cleanup()
{
    if (streaming)
    {
        stopStreaming();
    }

    timingHelper.close();
    if (camera)
    {
        camera->release();
        camera.reset();
    }

    // Clean up mmap cache
    for (auto &[fd, ptr] : mmapCache)
    {
        munmap(ptr, mmapLengths[fd]);
    }
    mmapCache.clear();
    mmapLengths.clear();

    initialized = false;
    LOG_INFO << "Camera cleanup complete" << std::endl;
}

// camera_base.cpp
bool CameraBase::resetCameraAcquisition()
{
    if (!camera)
    {
        LOG_ERROR << "resetCameraAcquisition: no camera object" << std::endl;
        return false;
    }

    camera->release();

    int ret = camera->acquire();
    if (ret)
    {
        LOG_ERROR << "resetCameraAcquisition: failed to re-acquire camera: " << ret << std::endl;
        return false;
    }

    LOG_INFO << "Camera re-acquired (IPA state reset)" << std::endl;
    return true;
}

bool CameraBase::startStreaming(const CaptureSettings &settings)
{
    LOG_INFO << "Starting streaming (max throughput mode)..." << std::endl;
    
    if (!initialized)
    {
        setError("Camera not initialized");
        return false;
    }

    if (streaming)
    {
        stopStreaming();
    }

    try
    {
        LOG_INFO << "Stream config: " << settings.width << "x" << settings.height << std::endl;
        LOG_INFO << "  frameDurationEnabled=" << settings.frameDurationEnabled
                 << " frameDuration_us=" << settings.frameDuration_us << std::endl;
        // Store settings for later restoration
        currentStreamSettings = settings;

        // Generate video configuration
        config = camera->generateConfiguration({StreamRole::VideoRecording});
        if (!config)
        {
            setError("Failed to generate configuration");
            return false;
        }

        // Configure stream
        StreamConfiguration &streamConfig = config->at(0);
        streamConfig.size.width = settings.width;
        streamConfig.size.height = settings.height;
        streamConfig.pixelFormat = formats::BGR888;

        // Validate and apply configuration
        CameraConfiguration::Status validation = config->validate();
        if (validation == CameraConfiguration::Invalid)
        {
            setError("Invalid stream configuration");
            return false;
        }

        int ret = camera->configure(config.get());
        if (ret)
        {
            setError("Failed to configure camera for streaming");
            return false;
        }

        if (timingHelper.isOpen())
        {
            auto timing = timingHelper.getTiming(settings.width, settings.height);
            if (timing)
            {
                currentTiming = *timing;
                LOG_INFO << "Stream timing - line: " << currentTiming.lineTime_us 
                        << " us, rolling shutter: " << currentTiming.rollingShutter_us << " us" << std::endl;
            }
        }

        // Allocate buffers
        allocator = std::make_unique<FrameBufferAllocator>(camera);
        Stream *stream = streamConfig.stream();
        ret = allocator->allocate(stream);
        if (ret < 0)
        {
            setError("Failed to allocate buffers");
            return false;
        }

        // Create requests - NO FrameDurationLimits (max throughput)
        const std::vector<std::unique_ptr<FrameBuffer>> &buffers = allocator->buffers(stream);
        requests.clear();
        requests.reserve(buffers.size());

        for (unsigned int i = 0; i < buffers.size(); ++i)
        {
            std::unique_ptr<Request> request = camera->createRequest();
            if (!request)
            {
                setError("Failed to create request");
                return false;
            }

            ret = request->addBuffer(stream, buffers[i].get());
            if (ret < 0)
            {
                setError("Failed to add buffer to request");
                return false;
            }

            // Apply controls to all requests (NO FPS limiting)
            ControlList &controls = request->controls();
            
            applyExposureControls(controls, settings);
            applyGainControls(controls, settings);
            applyWhiteBalanceControls(controls, settings);
            applyFocusControls(controls, settings);
            applyAdvancedControls(controls, settings);
            requests.push_back(std::move(request));
        }

        // Connect to request completed signal
        camera->requestCompleted.connect(this, &CameraBase::requestComplete);

        // Start camera
        ret = camera->start();
        if (ret)
        {
            setError("Failed to start camera");
            return false;
        }

        // Queue all requests
        for (auto &request : requests)
        {
            camera->queueRequest(request.get());
        }

        streaming = true;
        LOG_INFO << "Streaming started (max throughput)" << std::endl;

        return true;
    }
    catch (const std::exception &e)
    {
        setError(std::string("Exception during startStreaming: ") + e.what());
        return false;
    }
}

void CameraBase::requestComplete(libcamera::Request* request)
{
    LOG_TRACE << "requestComplete called, streaming=" << streaming << std::endl;
    if (!streaming)  // Early exit - stop was requested
        return;

    if (request->status() == Request::RequestCancelled)
    {
        LOG_TRACE << "requestComplete called, streaming=" << streaming << " - request cancelled"   << std::endl;
        return;
    }
    
    const Request::BufferMap &buffers = request->buffers();
    for (auto &[stream, buffer] : buffers)
    {
        const FrameBuffer::Plane plane = buffer->planes()[0];
        
        void *data = nullptr;
        {
            std::lock_guard<std::mutex> lock(mmapCacheMutex);
            auto it = mmapCache.find(plane.fd.get());
            if (it != mmapCache.end())
            {
                data = it->second;
            }
            else
            {
                data = mmap(nullptr, plane.length, PROT_READ, MAP_SHARED, 
                           plane.fd.get(), 0);
                if (data == MAP_FAILED)
                {
                    continue;
                }
                mmapCache[plane.fd.get()] = data;
                mmapLengths[plane.fd.get()] = plane.length;
            }
        }
        
        StreamConfiguration &streamConfig = config->at(0);
        int width = streamConfig.size.width;
        int height = streamConfig.size.height;
        FrameMetadata metadata;
        metadata.width = width;
        metadata.height = height;
        metadata.valid = true;
        extractRequestMetadata(request, &metadata);
        
        cv::Mat frame(height, width, CV_8UC3, data, streamConfig.stride);
        cv::Mat frameCopy = frame.clone();
        
        frameQueue.push({frameCopy, metadata});
    }
    
    request->reuse(Request::ReuseBuffers);

    // Apply pending settings AFTER reuse (reuse clears controls)
    {
        std::lock_guard<std::mutex> lock(settingsMutex);
        if (hasPendingSettings)
        {

            LOG_INFO << "Applying pending settings: " << pendingSettings << std::endl;
            ControlList &controls = request->controls();
            
            applyExposureControls(controls, pendingSettings);
            applyGainControls(controls, pendingSettings);
            applyWhiteBalanceControls(controls, pendingSettings);
            applyFocusControls(controls, pendingSettings);
            applyAdvancedControls(controls, pendingSettings);
            applyFrameDurationControls(controls, pendingSettings); 


            hasPendingSettings = false;
            LOG_INFO << "Applied pending settings to request, buffers in flight: " 
                    << requests.size() << std::endl;
        }
    }

    camera->queueRequest(request);
}


void CameraBase::applyFrameDurationControls(libcamera::ControlList &controls,
                                            const CaptureSettings &settings)
{
    if (settings.frameDurationEnabled && settings.frameDuration_us > 0)
    {
        int64_t fd = settings.frameDuration_us;
        controls.set(controls::FrameDurationLimits,
                     libcamera::Span<const int64_t, 2>({fd, fd}));
        LOG_INFO << "FrameDurationLimits locked to " << fd << " us ("
                 << (1e6 / fd) << " fps)" << std::endl;
    }
    else
    {
        auto fdLimits = getFrameDurationLimits();
        if (fdLimits.valid)
        {
            int64_t defaultMinUs = 33333; // 30fps floor — matches IPA default behaviour
            controls.set(controls::FrameDurationLimits,
                        libcamera::Span<const int64_t, 2>({defaultMinUs, fdLimits.maxUs}));
            LOG_INFO << "FrameDurationLimits restored to default range ["
                    << defaultMinUs << ", " << fdLimits.maxUs << "] us" << std::endl;        }
        }
}

void CameraBase::stopStreaming()
{
    if (!streaming)
        return;

    try
    {
        LOG_INFO << "Stopping streaming..." << std::endl;

        camera->stop();
        camera->requestCompleted.disconnect(this, &CameraBase::requestComplete);

        requests.clear();
        allocator.reset();
        config.reset();
        resetCameraAcquisition();  // resets IPA state including persisted FrameDurationLimits

        // Clear frame queue
        frameQueue.clear();

        // Clear mmap cache
        {
            std::lock_guard<std::mutex> lock(mmapCacheMutex);
            
            for (auto &[fd, ptr] : mmapCache)
            {
                if (ptr && mmapLengths.count(fd))
                {
                    munmap(ptr, mmapLengths[fd]);
                }
            }
            
            mmapCache.clear();
            mmapLengths.clear();
            LOG_INFO << "Cleared mmap cache" << std::endl;
        }

        streaming = false;
        LOG_INFO << "Streaming stopped" << std::endl;
    }
    catch (const std::exception &e)
    {
        setError(std::string("Exception during stopStreaming: ") + e.what());
    }
}

void CameraBase::setPendingStreamSettings(const CaptureSettings &settings)
{
    if (!streaming)
    {
        LOG_WARNING << "Cannot update settings: not streaming" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(settingsMutex);
    pendingSettings = settings;
    hasPendingSettings = true;
    
    LOG_DEBUG << "Stream settings update queued" << std::endl;
}

cv::Mat CameraBase::getFrame()
{
    FrameMetadata dummy;
    return getFrame(dummy);
}

cv::Mat CameraBase::getFrame(FrameMetadata &metadata)
{
    auto frame = frameQueue.popWait(100); // 100ms timeout
    metadata = frame.metadata;
    return frame.image;
}

cv::Mat CameraBase::getLatestFrame()
{
    FrameMetadata dummy;
    return getLatestFrame(dummy);
}

cv::Mat CameraBase::getLatestFrame(FrameMetadata &metadata)
{
    auto frame = frameQueue.popLatest();
    metadata = frame.metadata;
    return frame.image;
}

void CameraBase::updateSensorTiming()
{
    if (!timingHelper.isOpen())
    {
        LOG_WARNING << "Timing helper not open, cannot update timing" << std::endl;
        return;
    }
    
    auto timing = timingHelper.getTiming(maxResolution.width, maxResolution.height);
    if (timing)
    {
        currentTiming = *timing;
    }
    else
    {
        LOG_WARNING << "Failed to get sensor timing" << std::endl;
        currentTiming = SensorTiming();
    }
}

std::optional<SensorTiming> CameraBase::getSensorTiming()
{
    if (!timingHelper.isOpen())
    {
        return std::nullopt;
    }
    
    // Query fresh values
    return timingHelper.getTiming(
        currentTiming.activeWidth > 0 ? currentTiming.activeWidth : maxResolution.width,
        currentTiming.activeHeight > 0 ? currentTiming.activeHeight : maxResolution.height
    );
}

const LensPositionRange CameraBase::getLensPositionRange() const
{
    LensPositionRange result;

    if (!camera)
        return result;

    auto it = camera->controls().find(&controls::LensPosition);
    if (it == camera->controls().end())
        return result;

    result.min = it->second.min().get<float>();
    result.max = it->second.max().get<float>();
    result.valid = true;

    return result;
}

FrameDurationLimits CameraBase::getFrameDurationLimits() const
{
    FrameDurationLimits result;

    if (!camera)
        return result;

    auto it = camera->controls().find(&controls::FrameDurationLimits);
    if (it == camera->controls().end())
    {
        LOG_WARNING << "FrameDurationLimits not available in camera controls" << std::endl;
        return result;
    }

    result.minUs = it->second.min().get<int64_t>();
    result.maxUs = it->second.max().get<int64_t>();
    result.valid = true;

    LOG_INFO << "Frame duration limits:"
             << " min=" << result.minUs << " us (" << (1e6 / result.minUs) << " fps max)"
             << " max=" << result.maxUs << " us (" << (1e6 / result.maxUs) << " fps min)"
             << std::endl;

    return result;
}

std::optional<int32_t> CameraBase::setVBlank(int32_t vblank)
{
    if (!timingHelper.isOpen())
    {
        LOG_ERROR << "Cannot set VBlank: timing helper not open" << std::endl;
        return std::nullopt;
    }
    
    if (streaming)
    {
        LOG_WARNING << "Setting VBlank while streaming - libcamera may override" << std::endl;
    }
    
    auto result = timingHelper.setVBlank(vblank);
    if (result)
    {
        // Refresh timing to get updated derived values
        updateSensorTiming();
    }
    return result;
}

std::optional<int32_t> CameraBase::setHBlank(int32_t hblank)
{
    if (!timingHelper.isOpen())
    {
        LOG_ERROR << "Cannot set HBlank: timing helper not open" << std::endl;
        return std::nullopt;
    }
    
    if (streaming)
    {
        LOG_WARNING << "Setting HBlank while streaming - libcamera may override" << std::endl;
    }
    
    auto result = timingHelper.setHBlank(hblank);
    if (result)
    {
        updateSensorTiming();
    }
    return result;
}

cv::Mat CameraBase::captureStill(const CaptureSettings &settings, FrameMetadata *metadata)
{
    if (!initialized)
    {
        setError("Camera not initialized");
        return cv::Mat();
    }

    // Stop streaming if active
    bool wasStreaming = streaming;
    if (wasStreaming)
    {
        stopStreaming();
    }

    cv::Mat result;

    try
    {
        LOG_INFO << "Capturing still image: " << settings.width << "x" << settings.height << std::endl;

        // Generate still capture configuration
        std::unique_ptr<CameraConfiguration> config =
            camera->generateConfiguration({StreamRole::StillCapture});

        if (!config)
        {
            setError("Failed to generate still configuration");
            return cv::Mat();
        }

        StreamConfiguration &output = config->at(0);

        LOG_TRACE << "Supported formats for StillCapture:" << std::endl;
        std::stringstream ss;

        for (const auto &format : output.formats().pixelformats())
        {
            ss << "  " << format.toString() << std::endl;            
            // Also show supported sizes for each format
            const auto &sizes = output.formats().sizes(format);
            if (!sizes.empty())
            {
                ss << " sizes: " << sizes.front().toString() 
                        << " - " << sizes.back().toString() << std::endl;
            }
            ss << std::endl;
        }
        LOG_TRACE << ss.str() << std::endl;

        output.size.width = settings.width;
        output.size.height = settings.height;

        if (settings.rawMode)
        {
            // Select raw format based on sensor and bit depth
            libcamera::PixelFormat rawFormat = getRawPixelFormat(settings.rawBitDepth);
            output.pixelFormat = rawFormat;
            LOG_INFO << "Raw capture: " << rawFormat.toString() 
                     << " (" << settings.rawBitDepth << "-bit)" << " width " << settings.width << " height " << settings.height << std::endl;
        }
        else
        {
            output.pixelFormat = formats::BGR888;
        }

        libcamera::PixelFormat requestedFormat = output.pixelFormat;
        libcamera::Size requestedSize = output.size;
        
        // Validate and apply
        auto status = config->validate();
        if (status == CameraConfiguration::Valid)
        {
            LOG_INFO << "Configuration valid" << std::endl;
        }
        else if (status == CameraConfiguration::Adjusted)
            LOG_WARNING << "Configuration was adjusted" << std::endl;
        else if (status == CameraConfiguration::Invalid)
            LOG_ERROR << "Configuration is invalid" << std::endl;

        // Check if libcamera changed anything
        if (output.pixelFormat != requestedFormat)
        {
            LOG_WARNING << "Format changed by validation: " << requestedFormat.toString()
                        << " -> " << output.pixelFormat.toString() << std::endl;
        }
        if (output.size != requestedSize)
        {
            LOG_WARNING << "Size changed by validation: " 
                        << requestedSize.toString() << " -> " << output.size.toString() << std::endl;
        }
        LOG_INFO << "Output stride: " << output.stride << std::endl;

        int ret = camera->configure(config.get());
        if (ret)
        {
            setError("Failed to configure for still capture");
            return cv::Mat();
        }

        if (timingHelper.isOpen())
        {
            auto timing = timingHelper.getTiming(settings.width, settings.height);
            if (timing)
            {
                currentTiming = *timing;
            }
        }

        // Allocate buffers
        std::unique_ptr<FrameBufferAllocator> stillAllocator =
            std::make_unique<FrameBufferAllocator>(camera);

        Stream *outputStream = output.stream();
        ret = stillAllocator->allocate(outputStream);
        if (ret < 0)
        {
            setError("Failed to allocate still buffers");
            return cv::Mat();
        }

        // Start camera
        camera->start();

        bool needsAutoConvergence = (settings.autoExposure || 
                            settings.autoWhiteBalance || 
                            settings.autoAnalogGain) && !settings.rawMode;

         
        ConvergedSettings converged;
        LOG_INFO << (needsAutoConvergence ? "auto convergence warmup needed for AE/AWB" : "1 warmup needed") << std::endl;
        
      if (needsAutoConvergence)
        {
            int warmupFrames = 15;
            LOG_INFO << "Auto convergence warmup: " << warmupFrames << " frames" << std::endl;

            if (!performWarmupCaptures(outputStream, stillAllocator.get(), &converged, settings, warmupFrames))
            {
                camera->stop();
                setError("Warmup capture failed");
                return cv::Mat();
            }
        }
        else
        {
            if (!settings.rawMode)
            {
                int settlingFrames = 5;
                LOG_INFO << "Manual mode, " << settlingFrames << " settling frames" << std::endl;

                ConvergedSettings discard;
                if (!performWarmupCaptures(outputStream, stillAllocator.get(), &discard, settings, settlingFrames))
                {
                    camera->stop();
                    setError("Settling frames failed");
                    return cv::Mat();
                }
            }
            else
            {
                 LOG_INFO << "Manual RAW mode, 1 pipeline flush frame" << std::endl;
                ConvergedSettings discard;
                if (!performWarmupCaptures(outputStream, stillAllocator.get(),
                                        &discard, settings, 1))
                {
                    camera->stop();
                    setError("Pipeline flush frame failed");
                    return cv::Mat();
                }
            }
        }

        // Create final capture request
        std::unique_ptr<Request> request = camera->createRequest();
        if (!request)
        {
            camera->stop();
            setError("Failed to create still request");
            return cv::Mat();
        }

        const std::vector<std::unique_ptr<FrameBuffer>> &buffers = 
            stillAllocator->buffers(outputStream);

        ret = request->addBuffer(outputStream, buffers[0].get());
        if (ret < 0)
        {
            camera->stop();
            setError("Failed to add buffer to still request");
            return cv::Mat();
        }

        // Apply controls - use converged values if warmup was performed
        ControlList &controls = request->controls();
        
        if (converged.valid)
        {
            // Use the converged AE/AWB values from warmup
            applyConvergedSettings(controls, converged, settings);
        }
        else
        {
            // Manual mode - apply settings directly
            applyExposureControls(controls, settings);
            applyGainControls(controls, settings);
            applyWhiteBalanceControls(controls, settings);
            LOG_INFO << "Applying manual controls - exposure_us: " << settings.exposureTime_us 
            << " autoExposure: " << settings.autoExposure
            << " analogGain: " << settings.analogGain 
            << " autoAnalogGain: " << settings.autoAnalogGain << std::endl;
        }
        
        // Always apply these regardless of warmup
        applyFocusControls(controls, settings);
        applyAdvancedControls(controls, settings);

        // Setup synchronization
        {
            std::lock_guard<std::mutex> lock(stillCaptureMutex);
            stillCaptureComplete = false;
            stillCompletedRequest = nullptr;
        }

        // Connect to requestCompleted signal
        camera->requestCompleted.connect(this, &CameraBase::onStillCaptureComplete);

        // Capture
        camera->queueRequest(request.get());

        // Wait for completion with timeout
        {
            std::unique_lock<std::mutex> lock(stillCaptureMutex);
            bool success = stillCaptureCV.wait_for(lock, std::chrono::seconds(2),
                                                    [this] { return stillCaptureComplete; });
            
            if (!success)
            {
                camera->stop();
                camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);
                setError("Timeout waiting for still capture completion");
                return cv::Mat();
            }
        }

        camera->stop();
        camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);

        // Extract frame
        FrameBuffer *buffer = buffers[0].get();
        const FrameBuffer::Plane plane = buffer->planes()[0];

        void *data = mmap(nullptr, plane.length, PROT_READ, MAP_SHARED, 
                         plane.fd.get(), 0);

        if (data != MAP_FAILED)
        {
            if (settings.rawMode)
            {
                // Determine CV type based on bit depth
                int cvType;
                if (settings.rawBitDepth <= 8)
                {
                    cvType = CV_8UC1;
                }
                else
                {
                    // 10-bit and 12-bit unpacked formats use 16-bit storage
                    cvType = CV_16UC1;
                }
                
                cv::Mat frame(settings.height, settings.width, cvType, data, output.stride);
                result = frame.clone();
                
                LOG_INFO << "Raw frame: " << result.cols << "x" << result.rows 
                         << " depth=" << result.depth() << " stride=" << output.stride << std::endl;

                if (cvType == CV_16UC1)
                {
                    double minVal, maxVal;
                    cv::minMaxLoc(result, &minVal, &maxVal);
                    LOG_INFO << "Raw pixel range: " << minVal << " - " << maxVal << std::endl;
                }        
            }
            else
            {
                cv::Mat frame(settings.height, settings.width, CV_8UC3, data, output.stride);
                result = frame.clone();
            }
            
            // Extract metadata if requested
            if (metadata)
            {
                metadata->width = settings.width;
                metadata->height = settings.height;
                metadata->valid = true;
                metadata->width = settings.width;
                metadata->height = settings.height;
                metadata->valid = true;
                extractRequestMetadata(stillCompletedRequest, metadata);
            }

            munmap(data, plane.length);
        }

        LOG_INFO << "Capture complete: " << result.cols << "x" << result.rows << std::endl;
    }
    catch (const std::exception &e)
    {
        setError(std::string("Exception during capture: ") + e.what());
    }

    // Restart streaming if it was active
    if (wasStreaming)
    {
        startStreaming(currentStreamSettings);
    }

    return result;
}


StrobeCaptureResult CameraBase::captureWithStrobe(const CaptureSettings &settings)
{
    StrobeCaptureResult out;

    if (!initialized)
    {
        out.error = "Camera not initialized";
        LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
        return out;
    }

    if (!currentTiming.valid || currentTiming.rollingShutter_us <= 0.0)
    {
        out.error = "No valid sensor timing — cannot determine exposure duration";
        LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
        return out;
    }

    // -----------------------------------------------------------------------
    // Build capture settings — RAW, manual, exposure = rolling shutter
    // -----------------------------------------------------------------------
    CaptureSettings strobeSettings;
    strobeSettings.width            = settings.width  > 0 ? settings.width  : maxResolution.width;
    strobeSettings.height           = settings.height > 0 ? settings.height : maxResolution.height;
    strobeSettings.rawMode          = true;
    strobeSettings.rawBitDepth      = getNativeBitDepth();
    strobeSettings.autoExposure     = false;
    strobeSettings.autoAnalogGain   = false;
    strobeSettings.autoWhiteBalance = false;
    strobeSettings.autoFocus        = false;
    strobeSettings.exposureTime_us  = static_cast<int32_t>(currentTiming.rollingShutter_us);
    strobeSettings.analogGain       = 1.0f;
    strobeSettings.denoiseMode      = "off";

    out.exposureTime_us = strobeSettings.exposureTime_us;

    LOG_INFO << "captureWithStrobe:"
             << " size="     << strobeSettings.width << "x" << strobeSettings.height
             << " exposure=" << strobeSettings.exposureTime_us << " us"
             << " bitDepth=" << strobeSettings.rawBitDepth
             << " strobe="   << (strobeCallback ? "enabled" : "disabled")
             << std::endl;

    // -----------------------------------------------------------------------
    // Stop streaming if active
    // -----------------------------------------------------------------------
    bool wasStreaming = streaming;
    if (wasStreaming)
        stopStreaming();

    // -----------------------------------------------------------------------
    // Configure camera for RAW still capture
    // -----------------------------------------------------------------------
    auto captureConfig = camera->generateConfiguration({libcamera::StreamRole::StillCapture});
    if (!captureConfig)
    {
        out.error = "Failed to generate still configuration";
        LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
        if (wasStreaming) startStreaming(currentStreamSettings);
        return out;
    }

    libcamera::StreamConfiguration &streamCfg = captureConfig->at(0);
    streamCfg.size.width  = strobeSettings.width;
    streamCfg.size.height = strobeSettings.height;
    streamCfg.pixelFormat = getRawPixelFormat(strobeSettings.rawBitDepth);

    auto status = captureConfig->validate();
    if (status == libcamera::CameraConfiguration::Invalid)
    {
        out.error = "Invalid RAW still configuration";
        LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
        if (wasStreaming) startStreaming(currentStreamSettings);
        return out;
    }
    if (status == libcamera::CameraConfiguration::Adjusted)
        LOG_WARNING << "captureWithStrobe: configuration adjusted by libcamera" << std::endl;

    int ret = camera->configure(captureConfig.get());
    if (ret)
    {
        out.error = "Failed to configure camera for RAW still capture";
        LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
        if (wasStreaming) startStreaming(currentStreamSettings);
        return out;
    }

    // Refresh timing for this resolution
    if (timingHelper.isOpen())
    {
        auto timing = timingHelper.getTiming(strobeSettings.width, strobeSettings.height);
        if (timing)
            currentTiming = *timing;
    }

    // -----------------------------------------------------------------------
    // Allocate buffers
    // -----------------------------------------------------------------------
    auto stillAllocator = std::make_unique<libcamera::FrameBufferAllocator>(camera);
    libcamera::Stream *stream = streamCfg.stream();

    ret = stillAllocator->allocate(stream);
    if (ret < 0)
    {
        out.error = "Failed to allocate RAW still buffers";
        LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
        if (wasStreaming) startStreaming(currentStreamSettings);
        return out;
    }

    camera->start();

    // -----------------------------------------------------------------------
    // Pipeline flush frame — no strobe, same settings
    // -----------------------------------------------------------------------
    LOG_INFO << "captureWithStrobe: pipeline flush frame" << std::endl;
    {
        std::unique_ptr<libcamera::Request> flushRequest = camera->createRequest();
        if (!flushRequest)
        {
            out.error = "Failed to create flush request";
            LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
            camera->stop();
            if (wasStreaming) startStreaming(currentStreamSettings);
            return out;
        }

        const auto &buffers = stillAllocator->buffers(stream);
        ret = flushRequest->addBuffer(stream, buffers[0].get());
        if (ret < 0)
        {
            out.error = "Failed to add buffer to flush request";
            LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
            camera->stop();
            if (wasStreaming) startStreaming(currentStreamSettings);
            return out;
        }

        libcamera::ControlList &controls = flushRequest->controls();
        applyExposureControls(controls, strobeSettings);
        applyGainControls(controls, strobeSettings);

        {
            std::lock_guard<std::mutex> lock(stillCaptureMutex);
            stillCaptureComplete = false;
            stillCompletedRequest = nullptr;
        }

        camera->requestCompleted.connect(this, &CameraBase::onStillCaptureComplete);
        camera->queueRequest(flushRequest.get());

        {
            std::unique_lock<std::mutex> lock(stillCaptureMutex);
            bool ok = stillCaptureCV.wait_for(lock, std::chrono::seconds(2),
                                              [this] { return stillCaptureComplete; });
            if (!ok)
                LOG_WARNING << "captureWithStrobe: flush frame timed out" << std::endl;
        }

        camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);
    }

    // -----------------------------------------------------------------------
    // Final capture — strobe fires for this frame only (if callback set)
    // -----------------------------------------------------------------------
    LOG_INFO << "captureWithStrobe: final capture" << std::endl;
    {
        std::unique_ptr<libcamera::Request> captureRequest = camera->createRequest();
        if (!captureRequest)
        {
            out.error = "Failed to create capture request";
            LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
            camera->stop();
            if (strobeCallback) strobeCallback(false);  // safety
            if (wasStreaming) startStreaming(currentStreamSettings);
            return out;
        }

        const auto &buffers = stillAllocator->buffers(stream);
        ret = captureRequest->addBuffer(stream, buffers[0].get());
        if (ret < 0)
        {
            out.error = "Failed to add buffer to capture request";
            LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
            camera->stop();
            if (strobeCallback) strobeCallback(false);  // safety
            if (wasStreaming) startStreaming(currentStreamSettings);
            return out;
        }

        libcamera::ControlList &controls = captureRequest->controls();
        applyExposureControls(controls, strobeSettings);
        applyGainControls(controls, strobeSettings);

        {
            std::lock_guard<std::mutex> lock(stillCaptureMutex);
            stillCaptureComplete = false;
            stillCompletedRequest = nullptr;
        }

        camera->requestCompleted.connect(this, &CameraBase::onStillCaptureComplete);

        // Strobe on — fires only if callback is set
        if (strobeCallback)
        {
            LOG_INFO << "captureWithStrobe: strobe on" << std::endl;
            strobeCallback(true);
        }

        camera->queueRequest(captureRequest.get());

        {
            std::unique_lock<std::mutex> lock(stillCaptureMutex);
            bool ok = stillCaptureCV.wait_for(lock, std::chrono::seconds(2),
                                              [this] { return stillCaptureComplete; });
            if (!ok)
            {
                out.error = "Timeout waiting for strobe capture completion";
                LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
                if (strobeCallback) strobeCallback(false);
                camera->stop();
                camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);
                if (wasStreaming) startStreaming(currentStreamSettings);
                return out;
            }
        }

        // Stamp wall-clock time at readout completion — before strobeOff so
        // the timestamp reflects when the sensor finished, not after I2C teardown.
        out.captureTimestamp_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
            .count());

        // Strobe off — readout complete
        if (strobeCallback)
        {
            LOG_INFO << "captureWithStrobe: strobe off" << std::endl;
            strobeCallback(false);
        }

        camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);

        // -----------------------------------------------------------------------
        // Extract RAW frame
        // -----------------------------------------------------------------------
        libcamera::FrameBuffer *buffer = buffers[0].get();
        const libcamera::FrameBuffer::Plane plane = buffer->planes()[0];

        void *data = mmap(nullptr, plane.length, PROT_READ, MAP_SHARED,
                          plane.fd.get(), 0);
        if (data == MAP_FAILED)
        {
            out.error = "mmap failed for RAW capture buffer";
            LOG_ERROR << "captureWithStrobe: " << out.error << std::endl;
            camera->stop();
            if (wasStreaming) startStreaming(currentStreamSettings);
            return out;
        }

        int cvType = (strobeSettings.rawBitDepth <= 8) ? CV_8UC1 : CV_16UC1;
        cv::Mat frame(strobeSettings.height, strobeSettings.width,
                      cvType, data, streamCfg.stride);
        out.image = frame.clone();

        munmap(data, plane.length);

        // Extract metadata
        out.metadata.width  = strobeSettings.width;
        out.metadata.height = strobeSettings.height;
        out.metadata.valid  = true;
        if (stillCompletedRequest)
            extractRequestMetadata(stillCompletedRequest, &out.metadata);

        out.success = true;

        LOG_INFO << "captureWithStrobe: complete"
                 << " size="              << out.image.cols << "x" << out.image.rows
                 << " exposure="          << out.exposureTime_us << " us"
                 << " capture_timestamp=" << out.captureTimestamp_ms << " ms"
                 << std::endl;
    }

    camera->stop();

    if (wasStreaming)
        startStreaming(currentStreamSettings);

    return out;
}

// ============================================================================
// STROBE BURST API
//
// Usage:
//   beginStrobeBurst(settings);
//   auto bg    = captureStrobeBurstFrame(false);   // background
//   auto illum = captureStrobeBurstFrame(true);    // illuminated
//   auto dark  = captureStrobeBurstFrame(false);   // dark
//   endStrobeBurst();
//
// Configure/allocate/start paid once.  Each frame reuses the same buffer
// and stream config.  endStrobeBurst restores streaming if it was active.
// ============================================================================

bool CameraBase::beginStrobeBurst(const CaptureSettings& settings)
{
    if (!initialized)
    {
        setError("beginStrobeBurst: camera not initialized");
        return false;
    }
    if (burstActive)
    {
        setError("beginStrobeBurst: burst already active — call endStrobeBurst first");
        return false;
    }
    if (!currentTiming.valid || currentTiming.rollingShutter_us <= 0.0)
    {
        setError("beginStrobeBurst: no valid sensor timing");
        return false;
    }

    // Stop streaming if active — remember so endStrobeBurst can restore it.
    burstWasStreaming = streaming;
    if (burstWasStreaming)
        stopStreaming();

    // ── Build capture settings ────────────────────────────────────────────────
    burstSettings              = CaptureSettings{};
    burstSettings.width        = settings.width  > 0 ? settings.width  : maxResolution.width;
    burstSettings.height       = settings.height > 0 ? settings.height : maxResolution.height;
    burstSettings.rawMode      = true;
    burstSettings.rawBitDepth  = getNativeBitDepth();
    burstSettings.autoExposure     = false;
    burstSettings.autoAnalogGain   = false;
    burstSettings.autoWhiteBalance = false;
    burstSettings.autoFocus        = false;
    burstSettings.exposureTime_us  = settings.exposureTime_us > 0
                                     ? settings.exposureTime_us
                                     : static_cast<int32_t>(currentTiming.rollingShutter_us);
    burstSettings.analogGain       = settings.analogGain > 0.0f ? settings.analogGain : 1.0f;
    burstSettings.denoiseMode      = "off";

    // Lock frame duration so VBlank > exposure_us.  With a rolling shutter,
    // row N of frame K exposes during
    //   [T_sof_K + N*line_time - exposure_us, T_sof_K + N*line_time].
    // For an LED-gated dark frame to be clean across all rows, the LED must
    // turn off at least exposure_us before T_sof of that dark frame.  The
    // LED turns off at EOF of the preceding illuminated frame, which leads
    // the next SOF by vblank_gap_us.  Therefore vblank_gap_us must be
    // >= exposure_us, i.e. frame_duration_us >= rolling_shutter_us +
    // exposure_us.  A 2 ms safety margin covers ISR latency, LM3643
    // turn-off, and doubles as warmup time at the leading edge of
    // illuminated frames (LM3643 datasheet recommends >=1 ms pulse width).
    burstSettings.frameDurationEnabled = true;
    burstSettings.frameDuration_us     =
        static_cast<int64_t>(currentTiming.rollingShutter_us)
        + static_cast<int64_t>(burstSettings.exposureTime_us)
        + 2000;

    LOG_INFO << "beginStrobeBurst: requesting"
         << " size=" << burstSettings.width << "x" << burstSettings.height
         << " rawBitDepth=" << burstSettings.rawBitDepth
         << " frame_duration=" << burstSettings.frameDuration_us << " us"
         << std::endl;
    // ── Configure ─────────────────────────────────────────────────────────────
    burstConfig = camera->generateConfiguration({libcamera::StreamRole::StillCapture});
    if (!burstConfig)
    {
        setError("beginStrobeBurst: failed to generate configuration");
        if (burstWasStreaming) startStreaming(currentStreamSettings);
        return false;
    }

    libcamera::StreamConfiguration& streamCfg = burstConfig->at(0);
    streamCfg.size.width  = burstSettings.width;
    streamCfg.size.height = burstSettings.height;
    //streamCfg.pixelFormat = getRawPixelFormat(burstSettings.rawBitDepth);
    streamCfg.pixelFormat = libcamera::formats::SBGGR10;
    streamCfg.bufferCount = 8;  
    auto status = burstConfig->validate();
    if (status == libcamera::CameraConfiguration::Invalid)
    {
        setError("beginStrobeBurst: invalid RAW configuration");
        burstConfig.reset();
        if (burstWasStreaming) startStreaming(currentStreamSettings);
        return false;
    }
    if (status == libcamera::CameraConfiguration::Adjusted)
        LOG_INFO << "beginStrobeBurst: configuration adjusted by libcamera" << std::endl;
    LOG_INFO << "beginStrobeBurst after validate: format=" 
         << streamCfg.pixelFormat.toString()
         << " size=" << streamCfg.size.width << "x" << streamCfg.size.height
         << " stride=" << streamCfg.stride << std::endl;
    int ret = camera->configure(burstConfig.get());
    if (ret)
    {
        setError("beginStrobeBurst: camera configure failed");
        burstConfig.reset();
        if (burstWasStreaming) startStreaming(currentStreamSettings);
        return false;
    }

    
    // ── Allocate buffers ──────────────────────────────────────────────────────
    burstAllocator = std::make_unique<libcamera::FrameBufferAllocator>(camera);
    burstStream    = streamCfg.stream();

    ret = burstAllocator->allocate(burstStream);
    if (ret < 0)
    {
        setError("beginStrobeBurst: buffer allocation failed");
        burstAllocator.reset();
        burstConfig.reset();
        if (burstWasStreaming) startStreaming(currentStreamSettings);
        return false;
    }

    // ── Start camera ──────────────────────────────────────────────────────────
    ret = camera->start();
    if (ret)
    {
        setError("beginStrobeBurst: camera start failed");
        burstAllocator.reset();
        burstConfig.reset();
        if (burstWasStreaming) startStreaming(currentStreamSettings);
        return false;
    }

    // ── Pipeline flush frame ──────────────────────────────────────────────────
    // One no-strobe frame to flush the IPA pipeline before the real burst.
    LOG_INFO << "beginStrobeBurst: pipeline flush frame" << std::endl;
    {
        std::unique_ptr<libcamera::Request> flushReq = camera->createRequest();
        if (!flushReq)
        {
            setError("beginStrobeBurst: failed to create flush request");
            camera->stop();
            burstAllocator.reset();
            burstConfig.reset();
            if (burstWasStreaming) startStreaming(currentStreamSettings);
            return false;
        }

        const auto& buffers = burstAllocator->buffers(burstStream);
        ret = flushReq->addBuffer(burstStream, buffers[0].get());
        if (ret < 0)
        {
            setError("beginStrobeBurst: failed to add buffer to flush request");
            camera->stop();
            burstAllocator.reset();
            burstConfig.reset();
            if (burstWasStreaming) startStreaming(currentStreamSettings);
            return false;
        }

        libcamera::ControlList& controls = flushReq->controls();
        applyExposureControls(controls, burstSettings);
        applyGainControls(controls, burstSettings);
        applyFrameDurationControls(controls, burstSettings);

        {
            std::lock_guard<std::mutex> lock(stillCaptureMutex);
            stillCaptureComplete = false;
            stillCompletedRequest = nullptr;
        }

        camera->requestCompleted.connect(this, &CameraBase::onStillCaptureComplete);
        camera->queueRequest(flushReq.get());

        {
            std::unique_lock<std::mutex> lock(stillCaptureMutex);
            bool ok = stillCaptureCV.wait_for(lock, std::chrono::seconds(2),
                                              [this] { return stillCaptureComplete; });
            if (!ok)
                LOG_WARNING << "beginStrobeBurst: flush frame timed out" << std::endl;
        }

        camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);
    }
     // ── Re-query timing after flush frame ─────────────────────────────────────
    // The IPA only pushes final HBlank/VBlank to the sensor subdev at streaming
    // start.  At this point the flush frame has completed, so the subdev now
    // reflects the IPA's actual choice.  If rolling_shutter turned out larger
    // than we assumed at configure time, our frame_duration would squeeze
    // VBlank below exposure_us and leak illuminated pixels into subsequent
    // dark frames.  Recompute and re-apply before warmup/burst.
    if (timingHelper.isOpen())
    {
        auto postFlushTiming = timingHelper.getTiming(burstSettings.width,
                                                     burstSettings.height);
        if (postFlushTiming && postFlushTiming->rollingShutter_us > 0.0)
        {
            double rsBefore = currentTiming.rollingShutter_us;
            currentTiming = *postFlushTiming;

            int64_t newFrameDuration_us =
                static_cast<int64_t>(currentTiming.rollingShutter_us)
                + static_cast<int64_t>(burstSettings.exposureTime_us)
                + 2000;

            if (newFrameDuration_us > burstSettings.frameDuration_us)
            {
                LOG_INFO << "beginStrobeBurst: post-flush rolling_shutter "
                         << rsBefore << " us -> " << currentTiming.rollingShutter_us
                         << " us; frame_duration " << burstSettings.frameDuration_us
                         << " us -> " << newFrameDuration_us << " us" << std::endl;
                burstSettings.frameDuration_us = newFrameDuration_us;
            }
        }
    }

    burstActive = true;

    LOG_INFO << "beginStrobeBurst: ready"
             << " size="     << burstSettings.width << "x" << burstSettings.height
             << " exposure=" << burstSettings.exposureTime_us << " us"
             << " bitDepth=" << burstSettings.rawBitDepth
             << std::endl;

    return true;
}

// -----------------------------------------------------------------------------
// captureVBlankWarmup
//
// Captures and discards warmup frames after beginStrobeBurst() to let the IPA
// settle frame timing, exposure, and VBlank before the real burst begins.
// Must be called after beginStrobeBurst() and before captureVBlankBurst().
// Returns true if all warmup frames completed successfully.
// -----------------------------------------------------------------------------
bool CameraBase::captureVBlankWarmup(int warmupCount)
{
    if (!burstActive)
    {
        setError("captureVBlankWarmup: no active burst — call beginStrobeBurst first");
        return false;
    }

    if (warmupCount <= 0)
        return true;

    LOG_INFO << "captureVBlankWarmup: capturing " << warmupCount
             << " warmup frames" << std::endl;

    const auto& buffers = burstAllocator->buffers(burstStream);

    for (int i = 0; i < warmupCount; ++i)
    {
        // Reuse buffer 0 for all warmup frames (sequential, not concurrent)
        auto req = camera->createRequest();
        if (!req)
        {
            setError("captureVBlankWarmup: failed to create request " + std::to_string(i));
            return false;
        }

        int ret = req->addBuffer(burstStream, buffers[0].get());
        if (ret < 0)
        {
            setError("captureVBlankWarmup: addBuffer failed for request " + std::to_string(i));
            return false;
        }

        libcamera::ControlList& controls = req->controls();
        applyExposureControls(controls, burstSettings);
        applyGainControls(controls, burstSettings);
        applyFrameDurationControls(controls, burstSettings);

        {
            std::lock_guard<std::mutex> lock(stillCaptureMutex);
            stillCaptureComplete = false;
            stillCompletedRequest = nullptr;
        }

        camera->requestCompleted.connect(this, &CameraBase::onStillCaptureComplete);
        camera->queueRequest(req.get());

        {
            std::unique_lock<std::mutex> lock(stillCaptureMutex);
            bool ok = stillCaptureCV.wait_for(lock, std::chrono::seconds(2),
                                              [this] { return stillCaptureComplete; });
            if (!ok)
            {
                LOG_WARNING << "captureVBlankWarmup: frame " << i
                            << " timed out" << std::endl;
                camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);
                setError("captureVBlankWarmup: timeout on frame " + std::to_string(i));
                return false;
            }
        }

        camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);
    }

    LOG_INFO << "captureVBlankWarmup: complete — " << warmupCount
             << " frames discarded" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// captureStrobeBurstFramePreQueue
//
// Variant of captureStrobeBurstFrame that accepts an optional pre-queue
// callback fired immediately before camera->queueRequest().  At that point
// the request is fully built and the pipeline is about to accept it, so the
// sensor will start integrating on the next frame boundary — giving the
// caller a deterministic window in which to assert GPIO before any photons
// are needed.
//
// Usage (illuminated frame):
//
//   auto cb = [&]() {
//       ledGpio->strobeOn(LedGpioController::Group::ALL);
//       ledOnTimestamp_ms = nowMs();
//   };
//   auto result = targetCamera->captureStrobeBurstFramePreQueue(cb);
//
// Usage (dark frame — no pre-queue action needed):
//
//   auto result = targetCamera->captureStrobeBurstFramePreQueue(nullptr);
//
// The post-capture strobe-off is handled by the caller after this function
// returns, exactly as in the current handleUVBFCapture worker.
// -----------------------------------------------------------------------------
StrobeCaptureResult CameraBase::captureStrobeBurstFramePreQueue(
    std::function<void()> preQueueCallback)
{
    StrobeCaptureResult out;

    if (!burstActive)
    {
        out.error = "captureStrobeBurstFramePreQueue: no active burst — call beginStrobeBurst first";
        LOG_ERROR << out.error << std::endl;
        return out;
    }

    out.exposureTime_us = burstSettings.exposureTime_us;

    std::unique_ptr<libcamera::Request> req = camera->createRequest();
    if (!req)
    {
        out.error = "captureStrobeBurstFramePreQueue: failed to create request";
        LOG_ERROR << out.error << std::endl;
        return out;
    }

    const auto& buffers = burstAllocator->buffers(burstStream);
    int ret = req->addBuffer(burstStream, buffers[0].get());
    if (ret < 0)
    {
        out.error = "captureStrobeBurstFramePreQueue: failed to add buffer";
        LOG_ERROR << out.error << std::endl;
        return out;
    }

    libcamera::ControlList& controls = req->controls();
    applyExposureControls(controls, burstSettings);
    applyGainControls(controls, burstSettings);

    {
        std::lock_guard<std::mutex> lock(stillCaptureMutex);
        stillCaptureComplete = false;
        stillCompletedRequest = nullptr;
    }

    camera->requestCompleted.connect(this, &CameraBase::onStillCaptureComplete);

    // ── Pre-queue callback ────────────────────────────────────────────────────
    // Fired after the request is fully built but before it enters the kernel
    // pipeline.  GPIO strobeOn belongs here so the LEDs are already at full
    // current when the sensor starts integrating the next frame.
    if (preQueueCallback)
        preQueueCallback();

    camera->queueRequest(req.get());

    {
        std::unique_lock<std::mutex> lock(stillCaptureMutex);
        bool ok = stillCaptureCV.wait_for(lock, std::chrono::seconds(2),
                                          [this] { return stillCaptureComplete; });
        if (!ok)
        {
            out.error = "captureStrobeBurstFramePreQueue: timeout waiting for frame";
            LOG_ERROR << out.error << std::endl;
            camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);
            return out;
        }
    }

    out.captureTimestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);

    // ── Extract RAW frame ─────────────────────────────────────────────────────
    libcamera::FrameBuffer* buffer = buffers[0].get();
    const libcamera::FrameBuffer::Plane plane = buffer->planes()[0];

    void* data = mmap(nullptr, plane.length, PROT_READ, MAP_SHARED,
                      plane.fd.get(), 0);
    if (data == MAP_FAILED)
    {
        out.error = "captureStrobeBurstFramePreQueue: mmap failed";
        LOG_ERROR << out.error << std::endl;
        return out;
    }

    const libcamera::StreamConfiguration& streamCfg = burstConfig->at(0);
    int cvType = (burstSettings.rawBitDepth <= 8) ? CV_8UC1 : CV_16UC1;
    cv::Mat frame(burstSettings.height, burstSettings.width,
                  cvType, data, streamCfg.stride);
    out.image = frame.clone();

    munmap(data, plane.length);

    out.metadata.width  = burstSettings.width;
    out.metadata.height = burstSettings.height;
    out.metadata.valid  = true;
    if (stillCompletedRequest)
        extractRequestMetadata(stillCompletedRequest, &out.metadata);

    out.success = true;

    LOG_INFO << "captureStrobeBurstFramePreQueue:"
             << " preQueue=" << (preQueueCallback ? "yes" : "no")
             << " size="     << out.image.cols << "x" << out.image.rows
             << " ts="       << out.captureTimestamp_ms << " ms"
             << std::endl;

    return out;
}

StrobeCaptureResult CameraBase::captureStrobeBurstFrame(bool useStrobe)
{
    StrobeCaptureResult out;

    if (!burstActive)
    {
        out.error = "captureStrobeBurstFrame: no active burst — call beginStrobeBurst first";
        LOG_ERROR << out.error << std::endl;
        return out;
    }

    out.exposureTime_us = burstSettings.exposureTime_us;

    std::unique_ptr<libcamera::Request> req = camera->createRequest();
    if (!req)
    {
        out.error = "captureStrobeBurstFrame: failed to create request";
        LOG_ERROR << out.error << std::endl;
        return out;
    }

    const auto& buffers = burstAllocator->buffers(burstStream);
    int ret = req->addBuffer(burstStream, buffers[0].get());
    if (ret < 0)
    {
        out.error = "captureStrobeBurstFrame: failed to add buffer";
        LOG_ERROR << out.error << std::endl;
        return out;
    }

    libcamera::ControlList& controls = req->controls();
    applyExposureControls(controls, burstSettings);
    applyGainControls(controls, burstSettings);

    {
        std::lock_guard<std::mutex> lock(stillCaptureMutex);
        stillCaptureComplete = false;
        stillCompletedRequest = nullptr;
    }

    camera->requestCompleted.connect(this, &CameraBase::onStillCaptureComplete);

    if (useStrobe && strobeCallback)
    {
        LOG_INFO << "captureStrobeBurstFrame: strobe on" << std::endl;
        strobeCallback(true);
    }

    camera->queueRequest(req.get());

    {
        std::unique_lock<std::mutex> lock(stillCaptureMutex);
        bool ok = stillCaptureCV.wait_for(lock, std::chrono::seconds(2),
                                          [this] { return stillCaptureComplete; });
        if (!ok)
        {
            out.error = "captureStrobeBurstFrame: timeout waiting for frame";
            LOG_ERROR << out.error << std::endl;
            if (useStrobe && strobeCallback) strobeCallback(false);  // safety
            camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);
            return out;
        }
    }

    out.captureTimestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    if (useStrobe && strobeCallback)
    {
        LOG_INFO << "captureStrobeBurstFrame: strobe off" << std::endl;
        strobeCallback(false);
    }

    camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);

    // ── Extract RAW frame ─────────────────────────────────────────────────────
    libcamera::FrameBuffer* buffer = buffers[0].get();
    const libcamera::FrameBuffer::Plane plane = buffer->planes()[0];

    void* data = mmap(nullptr, plane.length, PROT_READ, MAP_SHARED,
                      plane.fd.get(), 0);
    if (data == MAP_FAILED)
    {
        out.error = "captureStrobeBurstFrame: mmap failed";
        LOG_ERROR << out.error << std::endl;
        return out;
    }

    const libcamera::StreamConfiguration& streamCfg = burstConfig->at(0);
    int cvType = (burstSettings.rawBitDepth <= 8) ? CV_8UC1 : CV_16UC1;
    cv::Mat frame(burstSettings.height, burstSettings.width,
                  cvType, data, streamCfg.stride);
    out.image = frame.clone();

    munmap(data, plane.length);

    out.metadata.width  = burstSettings.width;
    out.metadata.height = burstSettings.height;
    out.metadata.valid  = true;
    if (stillCompletedRequest)
        extractRequestMetadata(stillCompletedRequest, &out.metadata);

    out.success = true;

    LOG_INFO << "captureStrobeBurstFrame:"
             << " strobe=" << (useStrobe ? "yes" : "no")
             << " size="   << out.image.cols << "x" << out.image.rows
             << " ts="     << out.captureTimestamp_ms << " ms"
             << std::endl;

    return out;
}

// -----------------------------------------------------------------------------

void CameraBase::endStrobeBurst()
{
    if (!burstActive)
    {
        LOG_WARNING << "endStrobeBurst: no active burst" << std::endl;
        return;
    }

    LOG_INFO << "endStrobeBurst: tearing down burst session" << std::endl;

    camera->stop();
    burstAllocator.reset();
    burstConfig.reset();
    burstStream  = nullptr;
    burstActive  = false;

    if (burstWasStreaming)
    {
        LOG_INFO << "endStrobeBurst: restoring streaming" << std::endl;
        startStreaming(currentStreamSettings);
    }
}


// -----------------------------------------------------------------------------
// queryVBlankBurstFrameCount
//
// Returns the number of frames captureVBlankBurst will use, based on how many
// buffers the driver actually provides after beginStrobeBurst().
// Must be called after beginStrobeBurst() and before captureVBlankBurst().
//
// The count is the largest odd number <= min(bufferCount, maxFrames).
// Minimum is 3 (dark_1, illum_1, dark_2). Returns -1 if < 3 buffers.
// -----------------------------------------------------------------------------
int CameraBase::queryVBlankBurstFrameCount(int maxFrames) const
{
    if (!burstActive || !burstAllocator || !burstStream)
        return -1;
 
    const int available = static_cast<int>(
        burstAllocator->buffers(burstStream).size());
 
    // Clamp to maxFrames
    int n = std::min(available, maxFrames);
 
    // Must be odd (sequence must end on a dark frame)
    if (n % 2 == 0) n--;
 
    if (n < 3) return -1;
    return n;
}
 
// -----------------------------------------------------------------------------
// onVBlankBurstFrameComplete
//
// Member function connected to camera->requestCompleted during
// captureVBlankBurst(). The request cookie carries the frame index.
// Must be fast — runs on the libcamera event thread.
// -----------------------------------------------------------------------------
void CameraBase::onVBlankBurstFrameComplete(libcamera::Request* req)
{
    if (!vblankState)
        return;
 
    VBlankBurstState& state = *vblankState;
 
    // ── 1. Record callback wall-clock time immediately ────────────────────────
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t callbackNow_ns =
        static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
 
    if (req->status() == libcamera::Request::RequestCancelled)
    {
        LOG_ERROR << "onVBlankBurstFrameComplete: request cancelled" << std::endl;
        std::lock_guard<std::mutex> lk(state.mutex);
        state.failed = true;
        state.completedCount++;
        state.cv.notify_all();
        return;
    }
 
    const int idx = static_cast<int>(req->cookie());
    if (idx < 0 || idx >= state.numFrames)
    {
        LOG_ERROR << "onVBlankBurstFrameComplete: invalid cookie " << idx << std::endl;
        std::lock_guard<std::mutex> lk(state.mutex);
        state.failed = true;
        state.completedCount++;
        state.cv.notify_all();
        return;
    }
 
    VBlankFrameResult& result = state.frames[idx];
 
    // ── 2. Extract sensor timestamp and frame duration ────────────────────────
    const libcamera::ControlList& meta = req->metadata();
 
    int64_t sensorTs_ns = 0;
    int64_t frameDur_us = 0;
 
    auto tsOpt = meta.get(libcamera::controls::SensorTimestamp);
    if (tsOpt) sensorTs_ns = *tsOpt;
 
    auto fdOpt = meta.get(libcamera::controls::FrameDuration);
    if (fdOpt) frameDur_us = *fdOpt;
 
    result.sensorTimestamp_ns   = sensorTs_ns;
    result.callbackTimestamp_ns = callbackNow_ns;
    result.frameDuration_us     = frameDur_us;
    result.exposureTime_us      = burstSettings.exposureTime_us;
 
    // ── 3. Toggle LED — the VBlank hypothesis ────────────────────────────────
    // even indices = dark frames, odd = illuminated frames
    const bool isDark = (idx % 2 == 0);
    const bool isLast = (idx == state.numFrames - 1);
 
    result.ledsOn = !isDark;
 
    if (!isLast && state.strobeToggle)
    {
        if (isDark)
            state.strobeToggle(true);   // dark done → strobeOn for next illum
        else
            state.strobeToggle(false);  // illum done → strobeOff for next dark
    }
 
    // ── 4. TIMINGDATA log ─────────────────────────────────────────────────────
    const int64_t callbackDelta_us =
        sensorTs_ns > 0 ? (callbackNow_ns - sensorTs_ns) / 1000 : -1;
 
    LOG_INFO << "TIMINGDATA"
             << " frame="             << idx
             << " role="              << state.roles[idx]
             << " sensor_ts_ns="      << sensorTs_ns
             << " callback_ts_ns="    << callbackNow_ns
             << " callback_delta_us=" << callbackDelta_us
             << " frame_dur_us="      << frameDur_us
             << " leds_during_frame=" << (result.ledsOn ? "on" : "off")
             << " toggle="            << (isLast  ? "none" :
                                          isDark   ? "strobeOn" : "strobeOff")
             << std::endl;
 
    // ── 5. Copy image data ────────────────────────────────────────────────────
    libcamera::FrameBuffer* buf = state.bufferPtrs[idx];
    const libcamera::FrameBuffer::Plane plane = buf->planes()[0];
 
    void* data = mmap(nullptr, plane.length, PROT_READ, MAP_SHARED,
                      plane.fd.get(), 0);
    if (data != MAP_FAILED)
    {
        const libcamera::StreamConfiguration& streamCfg = burstConfig->at(0);
        int cvType = (burstSettings.rawBitDepth <= 8) ? CV_8UC1 : CV_16UC1;
        cv::Mat frame(burstSettings.height, burstSettings.width,
                      cvType, data, streamCfg.stride);
        result.image   = frame.clone();
        result.success = true;
        munmap(data, plane.length);
    }
    else
    {
        LOG_ERROR << "onVBlankBurstFrameComplete: mmap failed frame=" << idx << std::endl;
    }
 
    // ── 6. Signal waiting thread ──────────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.completedCount++;
        state.cv.notify_all();
    }
}
 
 
// -----------------------------------------------------------------------------
// captureVBlankBurst
// -----------------------------------------------------------------------------
VBlankBurstResult CameraBase::captureVBlankBurst(
    const CaptureSettings&    settings,
    std::function<void(bool)> strobeToggle,
    int                       maxFrames)
{
    VBlankBurstResult out;
 
    if (!initialized)
    {
        out.error = "captureVBlankBurst: camera not initialized";
        LOG_ERROR << out.error << std::endl;
        return out;
    }
    if (burstActive)
    {
        out.error = "captureVBlankBurst: burst already active";
        LOG_ERROR << out.error << std::endl;
        return out;
    }
    if (!strobeToggle)
    {
        out.error = "captureVBlankBurst: strobeToggle callback required";
        LOG_ERROR << out.error << std::endl;
        return out;
    }
 
    // ── Configure and start ───────────────────────────────────────────────────
    if (!beginStrobeBurst(settings))
    {
        out.error = "captureVBlankBurst: beginStrobeBurst failed: " + getLastError();
        return out;
    }
 
    // ── IPA warmup — let frame timing settle before the real burst ────────────
    if (!captureVBlankWarmup(5))
    {
        out.error = "captureVBlankBurst: warmup failed: " + getLastError();
        endStrobeBurst();
        return out;
    }

    // ── Determine actual frame count from available buffers ───────────────────
    const int N = queryVBlankBurstFrameCount(maxFrames);
    if (N < 3)
    {
        out.error = "captureVBlankBurst: insufficient buffers for minimum 3-frame sequence";
        LOG_ERROR << out.error << std::endl;
        endStrobeBurst();
        return out;
    }
 
    out.frameCount = N;
    const std::vector<std::string> roles = vblankRolesForCount(N);
    const auto& buffers = burstAllocator->buffers(burstStream);
 
    LOG_INFO << "captureVBlankBurst: N=" << N << " frames, roles:";
    for (const auto& r : roles) LOG_INFO << " " << r;
    LOG_INFO << std::endl;
 
    // ── Initialise vblankState ────────────────────────────────────────────────
    vblankState = std::make_unique<VBlankBurstState>();
    vblankState->numFrames      = N;
    vblankState->frames.resize(N);
    vblankState->roles          = roles;
    vblankState->strobeToggle   = std::move(strobeToggle);
    vblankState->completedCount = 0;
    vblankState->failed         = false;
    vblankState->bufferPtrs.resize(N);
 
    for (int i = 0; i < N; i++)
    {
        vblankState->frames[i].role = roles[i];
        vblankState->bufferPtrs[i]  = buffers[i].get();
    }
 
    // ── Build all N requests ──────────────────────────────────────────────────
    std::vector<std::unique_ptr<libcamera::Request>> requests(N);
 
    for (int i = 0; i < N; i++)
    {
        requests[i] = camera->createRequest(static_cast<uint64_t>(i));
        if (!requests[i])
        {
            out.error = "captureVBlankBurst: failed to create request "
                        + std::to_string(i);
            LOG_ERROR << out.error << std::endl;
            vblankState.reset();
            endStrobeBurst();
            return out;
        }
 
        int ret = requests[i]->addBuffer(burstStream, buffers[i].get());
        if (ret < 0)
        {
            out.error = "captureVBlankBurst: addBuffer failed for request "
                        + std::to_string(i);
            LOG_ERROR << out.error << std::endl;
            vblankState.reset();
            endStrobeBurst();
            return out;
        }
 
        libcamera::ControlList& controls = requests[i]->controls();
        applyExposureControls(controls, burstSettings);
        applyGainControls(controls, burstSettings);
        applyFrameDurationControls(controls, burstSettings);
    }
 
    // ── Connect member function completion handler ────────────────────────────
    camera->requestCompleted.connect(
        this, &CameraBase::onVBlankBurstFrameComplete);
 
    // ── Queue all N requests before any complete ──────────────────────────────
    LOG_INFO << "captureVBlankBurst: queuing " << N << " requests" << std::endl;
 
    for (int i = 0; i < N; i++)
        camera->queueRequest(requests[i].get());
 
    // ── Wait for all N completions ────────────────────────────────────────────
    {
        std::unique_lock<std::mutex> lk(vblankState->mutex);
        const bool ok = vblankState->cv.wait_for(
            lk,
            std::chrono::seconds(N * 3),
            [this] { return vblankState->completedCount >= vblankState->numFrames; });
 
        if (!ok)
        {
            out.error = "captureVBlankBurst: timeout — "
                        + std::to_string(vblankState->completedCount)
                        + "/" + std::to_string(N) + " frames completed";
            LOG_ERROR << out.error << std::endl;
            camera->requestCompleted.disconnect(
                this, &CameraBase::onVBlankBurstFrameComplete);
            vblankState.reset();
            endStrobeBurst();
            return out;
        }
    }
 
    camera->requestCompleted.disconnect(
        this, &CameraBase::onVBlankBurstFrameComplete);
 
    // ── Move results out ──────────────────────────────────────────────────────
    out.frames  = std::move(vblankState->frames);
    out.success = !vblankState->failed;
    if (!out.success)
        out.error = "captureVBlankBurst: one or more requests cancelled";
 
    vblankState.reset();
    endStrobeBurst();
 
    // ── Final TIMINGDATA summary log ──────────────────────────────────────────
    for (int i = 0; i < static_cast<int>(out.frames.size()); i++)
    {
        const VBlankFrameResult& f = out.frames[i];
        int64_t period_us = 0;
        if (i > 0
            && out.frames[i-1].sensorTimestamp_ns > 0
            && f.sensorTimestamp_ns > 0)
        {
            period_us = (f.sensorTimestamp_ns
                         - out.frames[i-1].sensorTimestamp_ns) / 1000;
        }
        LOG_INFO << "TIMINGDATA summary"
                 << " frame="             << i
                 << " role="              << f.role
                 << " leds_on="           << (f.ledsOn ? "on" : "off")
                 << " sensor_ts_ns="      << f.sensorTimestamp_ns
                 << " callback_delta_us=" << (f.sensorTimestamp_ns > 0
                                              ? (f.callbackTimestamp_ns
                                                 - f.sensorTimestamp_ns) / 1000
                                              : -1LL)
                 << " frame_period_us="   << period_us
                 << " frame_dur_us="      << f.frameDuration_us
                 << std::endl;
    }
 
    LOG_INFO << "captureVBlankBurst: complete success=" << out.success << std::endl;
    return out;
}
 

void CameraBase::extractRequestMetadata(libcamera::Request *request, FrameMetadata *metadata)
{
    if (!request || !metadata)
        return;

    const ControlList &meta = request->metadata();

    // Exposure
    auto expOpt = meta.get(controls::ExposureTime);
    if (expOpt)
        metadata->exposureTime_us = *expOpt;

    // Analog gain
    auto gainOpt = meta.get(controls::AnalogueGain);
    if (gainOpt)
        metadata->analogGain = *gainOpt;

    // Digital gain
    auto digitalGainOpt = meta.get(controls::DigitalGain);
    if (digitalGainOpt)
    {
        metadata->digitalGain = *digitalGainOpt;
    }    

    // AWB colour gains (red, blue)
    auto colourGainsOpt = meta.get(controls::ColourGains);
    if (colourGainsOpt)
    {
        metadata->redGain = (*colourGainsOpt)[0];
        metadata->blueGain = (*colourGainsOpt)[1];
    }

    // Colour temperature
    auto colourTempOpt = meta.get(controls::ColourTemperature);
    if (colourTempOpt)
        metadata->colourTemperature = *colourTempOpt;

    // Sensor timestamp
    auto tsOpt = meta.get(controls::SensorTimestamp);
    if (tsOpt)
        metadata->timestamp_ns = *tsOpt;

    // Frame duration
    auto fdOpt = meta.get(controls::FrameDuration);
    if (fdOpt)
        metadata->frameDuration_us = *fdOpt;

    // Lens position
    auto lensOpt = meta.get(controls::LensPosition);
    if (lensOpt)
        metadata->lensPosition = *lensOpt;

    // AE / AWB enabled state
    auto aeEnableOpt = meta.get(controls::AeEnable);
    if (aeEnableOpt)
        metadata->aeEnabled = *aeEnableOpt;

    auto awbEnableOpt = meta.get(controls::AwbEnable);
    if (awbEnableOpt)
        metadata->awbEnabled = *awbEnableOpt;

    // Sensor black levels (uncomment when available on your libcamera version)
    // auto blackLevelOpt = meta.get(controls::SensorBlackLevels);
    // if (blackLevelOpt)
    // {
    //     auto bl = *blackLevelOpt;
    //     for (int i = 0; i < 4 && i < static_cast<int>(bl.size()); i++)
    //         metadata->sensorBlackLevels[i] = bl[i];
    //     metadata->blackLevelsValid = true;
    // }

    // Timing from V4L2
    if (currentTiming.valid)
    {
        metadata->hblank = currentTiming.hblank;
        metadata->vblank = currentTiming.vblank;
        metadata->lineTime_us = currentTiming.lineTime_us;
        metadata->rollingShutter_us = currentTiming.rollingShutter_us;
    }
}

bool CameraBase::performWarmupCaptures(libcamera::Stream *stream, 
                                       libcamera::FrameBufferAllocator *allocator,
                                       ConvergedSettings *convergedOut,
                                       const CaptureSettings &settings,
                                       int frameCount)
{
    LOG_INFO << "Performing " << frameCount << " warmup/settling frames" << std::endl;
    ConvergedSettings lastFrameValues;

    for (int i = 0; i < frameCount; i++)
    {
        std::unique_ptr<Request> warmupRequest = camera->createRequest();
        if (!warmupRequest)
        {
            LOG_ERROR << "Failed to create warmup request" << std::endl;
            return false;
        }

        const std::vector<std::unique_ptr<FrameBuffer>> &buffers = 
            allocator->buffers(stream);

        int ret = warmupRequest->addBuffer(stream, buffers[0].get());
        if (ret < 0)
        {
            LOG_ERROR << "Failed to add buffer to warmup request" << std::endl;
            return false;
        }

        // Apply the SAME settings as the final capture
        ControlList &controls = warmupRequest->controls();
        applyExposureControls(controls, settings);
        applyGainControls(controls, settings);
        applyWhiteBalanceControls(controls, settings);
        applyFocusControls(controls, settings);

        {
            std::lock_guard<std::mutex> lock(stillCaptureMutex);
            stillCaptureComplete = false;
            stillCompletedRequest = nullptr;
        }

        camera->requestCompleted.connect(this, &CameraBase::onStillCaptureComplete);
        camera->queueRequest(warmupRequest.get());

        {
            std::unique_lock<std::mutex> lock(stillCaptureMutex);
            bool success = stillCaptureCV.wait_for(lock, std::chrono::seconds(1),
                                                   [this] { return stillCaptureComplete; });
            if (!success)
            {
                LOG_WARNING << "Warmup frame " << i << " timed out" << std::endl;
            }
        }

        camera->requestCompleted.disconnect(this, &CameraBase::onStillCaptureComplete);


        if (stillCompletedRequest)
        {
            extractConvergedValues(stillCompletedRequest, &lastFrameValues);
             LOG_INFO << "  lastFrameValues " << (i + 1) << "/" << frameCount
                     << " lastFrameValues " << lastFrameValues
                     << std::endl;
        }
    }

    // Extract final converged/settled values from last frame
    if (convergedOut && lastFrameValues.valid)
    {
        *convergedOut = lastFrameValues;
        LOG_INFO << "Final converged values from warmup: " << lastFrameValues << std::endl;
    }

    return true;
}

bool CameraBase::isAeAwbConverged(libcamera::Request *request)
{
    const ControlList &metadata = request->metadata();
    
    auto aeState = metadata.get(controls::AeState);
    if (aeState && *aeState != controls::AeStateConverged)
    {
        return false;
    }
    
    auto awbState = metadata.get(controls::draft::AwbState);
    if (awbState && *awbState != controls::draft::AwbConverged)
    {
        return false;
    }
    
    return true;
}

void CameraBase::onStillCaptureComplete(libcamera::Request *request)
{
    std::lock_guard<std::mutex> lock(stillCaptureMutex);
    stillCompletedRequest = request;
    stillCaptureComplete = true;
    stillCaptureCV.notify_one();
}

cv::Mat CameraBase::capture(const CaptureSettings &settings, FrameMetadata *metadata)
{
    return captureStill(settings, metadata);
}

ImageFormatInfo CameraBase::detectImageFormat(const cv::Mat& image, const std::string& sensorType) const
{
    ImageFormatInfo info;
    info.isRaw = false;
    info.bitDepth = 8;
    info.formatType = "RGB";
    info.encoding = "rgb";
    info.bayerPattern = "";
    
    if (image.channels() == 3)
    {
        return info;
    }
    
    if (image.channels() == 1)
    {
        info.isRaw = true;
        info.formatType = "BAYER";
        
        int width = image.cols;
        int height = image.rows;
        size_t dataSize = image.total() * image.elemSize();
        
        size_t size_8bit = width * height;
        size_t size_10bit_packed = (width * height * 5) / 4;
        size_t size_12bit_packed = (width * height * 3) / 2;
        size_t size_14bit_packed = (width * height * 7) / 4;
        size_t size_16bit = width * height * 2;
        
        if (dataSize == size_8bit || image.depth() == CV_8U)
        {
            info.encoding = "raw8";
            info.bitDepth = 8;
        }
        else if (dataSize == size_10bit_packed)
        {
            info.encoding = "raw10p";
            info.bitDepth = 10;
        }
        else if (dataSize == size_12bit_packed)
        {
            info.encoding = "raw12p";
            info.bitDepth = 12;
        }
        else if (dataSize == size_14bit_packed)
        {
            info.encoding = "raw14p";
            info.bitDepth = 14;
        }
        else if (dataSize == size_16bit || image.depth() == CV_16U)
        {
            info.encoding = "raw16";
            info.bitDepth = 16;
        }
        
        if (sensorType == "imx708")
        {
            info.bayerPattern = "BGGR";
        }
        else if (sensorType == "imx219")
        {
            info.bayerPattern = "BGGR";
        }
        else
        {
            info.bayerPattern = "BGGR";
            LOG_WARNING << "Unknown sensor type '" << sensorType 
                       << "', defaulting to BGGR Bayer pattern" << std::endl;
        }
        
        LOG_INFO << "Detected Bayer " << info.encoding << " format: " 
                 << width << "x" << height << " " << info.bayerPattern << std::endl;
    }
    
    return info;
}

cv::Mat CameraBase::debayerImage(const cv::Mat& bayer, const ImageFormatInfo& info)
{
    if (info.formatType != "BAYER")
    {
        return bayer;
    }
    
    cv::Mat rgb;
    int bayerCode;
    
    if (info.bayerPattern == "RGGB")
        bayerCode = cv::COLOR_BayerRG2RGB;
    else if (info.bayerPattern == "BGGR")
        bayerCode = cv::COLOR_BayerBG2RGB;
    else if (info.bayerPattern == "GRBG")
        bayerCode = cv::COLOR_BayerGR2RGB;
    else if (info.bayerPattern == "GBRG")
        bayerCode = cv::COLOR_BayerGB2RGB;
    else
        bayerCode = cv::COLOR_BayerRG2RGB;
    
    cv::cvtColor(bayer, rgb, bayerCode);
    
    if (rgb.depth() == CV_16U)
    {
        cv::Mat rgb8;
        double scale = (info.bitDepth == 10) ? 1.0 / 4.0 : 
                       (info.bitDepth == 12) ? 1.0 / 16.0 : 
                       (info.bitDepth == 14) ? 1.0 / 64.0 : 
                       1.0 / 256.0;
        rgb.convertTo(rgb8, CV_8UC3, scale);
        rgb = rgb8;
    }
    
    LOG_INFO << "Debayered " << info.bayerPattern << " to RGB: " 
             << rgb.cols << "x" << rgb.rows << std::endl;
    
    return rgb;
}

std::vector<uint8_t> CameraBase::encodeRgbUncompressed(const cv::Mat& rgb)
{
    std::vector<uint8_t> buffer;
    
    if (rgb.channels() != 3 || rgb.depth() != CV_8U)
    {
        LOG_ERROR << "Image must be 8-bit RGB for uncompressed encoding" << std::endl;
        return buffer;
    }
    
    std::string header = "RGB|" + 
                        std::to_string(rgb.cols) + "|" +
                        std::to_string(rgb.rows) + "|";
    
    buffer.reserve(header.size() + rgb.total() * 3);
    buffer.insert(buffer.end(), header.begin(), header.end());
    
    size_t dataSize = rgb.total() * 3;
    buffer.insert(buffer.end(), rgb.data, rgb.data + dataSize);
    
    LOG_INFO << "Encoded uncompressed RGB: " << rgb.cols << "x" << rgb.rows 
             << " (" << buffer.size() << " bytes)" << std::endl;
    
    return buffer;
}

std::vector<uint8_t> CameraBase::encodeJpeg(cv::Mat& image, int quality)
{
    return jpegEncoder->encode(image, quality);
}

std::vector<uint8_t> CameraBase::encodeImageForTransmission(
    cv::Mat& image, 
    bool useCompression,
    int jpegQuality)
{
    if (image.empty())
    {
        LOG_ERROR << "Cannot encode empty image" << std::endl;
        return std::vector<uint8_t>();
    }
    
    if (image.channels() == 3 && image.depth() == CV_8U)
    {
        if (useCompression)
        {
            return encodeJpeg(image, jpegQuality);
        }
        else
        {
            return encodeRgbUncompressed(image);
        }
    }
    
    if (image.channels() == 1)
    {
        ImageFormatInfo info = detectImageFormat(image, getSensorType());
        cv::Mat rgbImage = debayerImage(image, info);
        
        if (useCompression)
        {
            return encodeJpeg(rgbImage, jpegQuality);
        }
        else
        {
            return encodeRgbUncompressed(rgbImage);
        }
    }
    
    LOG_ERROR << "Unsupported image format for transmission" << std::endl;
    return std::vector<uint8_t>();
}

void CameraBase::setError(const std::string &error)
{
    lastError = error;
    LOG_ERROR << "Camera error: " << error << std::endl;

    if (errorCallback)
    {
        errorCallback(error);
    }
}

std::vector<std::string> CameraBase::getDenoiseModes() const
{
    return {
        "off",
        "fast",
        "high_quality"
    };
}

} // namespace sanuwave
