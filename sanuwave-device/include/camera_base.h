#ifndef CAMERA_BASE_H
#define CAMERA_BASE_H

#include <libcamera/libcamera.h>
#include <opencv2/opencv.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "frame_data.h"
#include "thread_safe_queue.h"
#include "ijpeg_encoder.h"
#include "jpeg_encoder_factory.h"
#include "sensor_info.h"
#include "logger.h"
#include <atomic>
namespace sanuwave
{

// ============================================================================
// STRUCTURES
// ============================================================================

struct FrameDurationLimits
{
    int64_t minUs = 0;
    int64_t maxUs = 0;
    bool    valid = false;
};

struct LensPositionRange
{
    float min = 0.0f;
    float max = 0.0f;
    bool valid = false;
};


struct ImageFormatInfo
{
    std::string formatType;
    std::string bayerPattern;
    std::string encoding;
    int bitDepth;
    bool isRaw;
};

struct CaptureSettings
{
    // Resolution
    int width = 0;
    int height = 0;
    
    // Exposure
    bool autoExposure = true;
    int32_t exposureTime_us = 0;  // 0 = auto
    float evCompensation = 0.0f;
    
    // Gain
    bool autoAnalogGain = true;
    float analogGain = 1.0f;
    float digitalGain = 1.0f;
    
    // White Balance
    bool autoWhiteBalance = true;
    std::string awbMode = "auto";
    float redGain = 1.0f;
    float blueGain = 1.0f;
    
    // Focus
    bool autoFocus = true;
    float lensPosition = 0.0f;
    
    // Advanced
    std::string denoiseMode = "auto";
    bool hdrMode = false;
    int32_t colourBalanceRed = 256;
    int32_t colourBalanceBlue = 256;
    std::string binningMode = "none";
    int32_t frameLength = 0;
    std::string meteringMode = "center";
    
      // Frame duration lock (streaming only)
    bool    frameDurationEnabled = false;
    int64_t frameDuration_us     = 0;

    // Format
    bool rawMode = false;
    int rawBitDepth = 10;
    int currentStreamFps;

    friend std::ostream& operator<<(std::ostream& os, const CaptureSettings& s)
    {
        os << "CaptureSettings{"
           << "res=" << s.width << "x" << s.height
           << ", autoExp=" << s.autoExposure << ", exposure=" << s.exposureTime_us << "us"
           << ", ev=" << s.evCompensation
           << ", autoGain=" << s.autoAnalogGain << ", analogGain=" << s.analogGain
           << ", digitalGain=" << s.digitalGain
           << ", autoWB=" << s.autoWhiteBalance << ", awbMode=" << s.awbMode
           << ", redGain=" << s.redGain << ", blueGain=" << s.blueGain
           << ", autoFocus=" << s.autoFocus << ", lensPos=" << s.lensPosition
           << ", denoise=" << s.denoiseMode << ", hdr=" << s.hdrMode
           << ", metering=" << s.meteringMode
           << ", raw=" << s.rawMode << ", rawBits=" << s.rawBitDepth
           << "}";
        return os;
    }
};

// Holds converged AE/AWB values extracted from warmup frames
struct ConvergedSettings
{
    bool valid = false;
    int32_t exposureTime_us = 0;
    float analogGain = 1.0f;
    float redGain = 1.0f;
    float blueGain = 1.0f;

    friend std::ostream& operator<<(std::ostream& os, const ConvergedSettings& s)
    {
        os << "ConvergedSettings{"
           << "valid=" << s.valid
           << ", exposure=" << s.exposureTime_us << "us"
           << ", analogGain=" << s.analogGain
           << ", redGain=" << s.redGain
           << ", blueGain=" << s.blueGain
           << "}";
        return os;
    }

};

struct StrobeCaptureResult
{
    cv::Mat       image;
    FrameMetadata metadata;
    int32_t       exposureTime_us = 0;  // actual exposure used — for LM3643 timeout config
    uint64_t      captureTimestamp_ms = 0;  // wall-clock ms when requestCompleted fired
    bool          success         = false;
    std::string   error;
};

struct VBlankFrameResult
{
    std::string role;
    cv::Mat image;
    int64_t sensorTimestamp_ns = 0;   // controls::SensorTimestamp
    int64_t callbackTimestamp_ns = 0; // CLOCK_MONOTONIC at callback entry
    int64_t frameDuration_us = 0;     // controls::FrameDuration
    int32_t exposureTime_us = 0;
    bool ledsOn = false;
    bool success = false;
};

struct VBlankBurstResult
{
    bool success = false;
    std::string error;
    int frameCount = 0; // actual frames captured
    std::vector<VBlankFrameResult> frames;
};
// -----------------------------------------------------------------------------
// vblankRolesForCount
//
// Generates the alternating dark/illum role sequence for N frames.
// N must be odd and >= 3. Always starts with dark and ends with dark.
// -----------------------------------------------------------------------------
inline std::vector<std::string> vblankRolesForCount(int n)
{
    // n must be odd >= 3; caller is responsible for ensuring this
    std::vector<std::string> roles;
    roles.reserve(n);
 
    int darkIdx  = 1;
    int illumIdx = 1;
 
    for (int i = 0; i < n; i++)
    {
        if (i % 2 == 0)
            roles.push_back("dark_"  + std::to_string(darkIdx++));
        else
            roles.push_back("illum_" + std::to_string(illumIdx++));
    }
    return roles;
}
 
class CameraBase
{
public:
    using ErrorCallback = std::function<void(const std::string&)>;
    
    CameraBase();
    virtual ~CameraBase();
    
    // Initialization
    virtual bool init(libcamera::CameraManager* cameraManager, int index);
    virtual void cleanup();

  

    // Streaming
    virtual bool startStreaming(const CaptureSettings &settings);
    virtual void stopStreaming();
    virtual void setPendingStreamSettings(const CaptureSettings &settings);
        

    bool isStreaming() const { return streaming; }
    
    // Frame retrieval
    cv::Mat getFrame();
    cv::Mat getFrame(FrameMetadata &metadata);
    cv::Mat getLatestFrame();
    cv::Mat getLatestFrame(FrameMetadata &metadata);

    // Returns the number of frames that captureVBlankBurst will capture,
    // based on driver buffer availability. Call after beginStrobeBurst().
    // Returns -1 on error.
    int queryVBlankBurstFrameCount(int maxFrames) const;
    bool captureVBlankWarmup(int warmupCount);
    VBlankBurstResult captureVBlankBurst(const CaptureSettings &settings,
                                         std::function<void(bool)> strobeToggle, int maxFrames = 7);

    // Still capture
    virtual cv::Mat captureStill(const CaptureSettings &settings, FrameMetadata *metadata);
    cv::Mat capture(const CaptureSettings &settings, FrameMetadata *metadata);
    // Strobe-synchronised RAW still capture.
    // Caller must set a strobe callback before calling this.
    // Returns StrobeCaptureResult — check .success before using .image.
    StrobeCaptureResult captureWithStrobe(const CaptureSettings &settings);
    bool beginStrobeBurst(const CaptureSettings &settings);
    StrobeCaptureResult captureStrobeBurstFrame(bool useStrobe);
    StrobeCaptureResult captureStrobeBurstFramePreQueue(
    std::function<void()> preQueueCallback);
    // Encoding
    ImageFormatInfo detectImageFormat(const cv::Mat& image, const std::string& sensorType) const;
    cv::Mat debayerImage(const cv::Mat& bayer, const ImageFormatInfo& info);
    std::vector<uint8_t> encodeImageForTransmission(cv::Mat& image, bool useCompression, int jpegQuality);
    std::vector<uint8_t> encodeRgbUncompressed(const cv::Mat& rgb);
    std::vector<uint8_t> encodeJpeg(cv::Mat& image, int quality);
    
    // Status
    bool isInitialized() const { return initialized; }
    std::string getLastError() const { return lastError; }
    std::string getCameraId() const { return cameraId; }
    cv::Size getMaxResolution() const { return maxResolution; }
    virtual std::string getSensorType() const = 0;
    
    // Get current sensor timing (blanking, line time, etc.)
     [[nodiscard]] std::optional<SensorTiming> getSensorTiming();
     
    sanuwave::SensorInfo& getSensorInfo() { return timingHelper; }
    // Set VBlank (affects frame rate). Call before streaming or between captures.
    // Returns actual value set, or nullopt on failure.
    [[nodiscard]] std::optional<int32_t> setVBlank(int32_t vblank);
    
    // Set HBlank (usually limited range). Call before streaming or between captures.
     [[nodiscard]] std::optional<int32_t> setHBlank(int32_t hblank);
    
     FrameDurationLimits getFrameDurationLimits() const;

    // Refresh timing after resolution change
    void updateSensorTiming();

    // Path to the sanuwave_strobe sysfs directory for the CFE instance
    // bound to this camera. Resolved once at init time.
    const std::string& getKernelStrobeSysfsPath() const
    {
        return kernelStrobeSysfsPath;
    }

    // Callbacks
    void setErrorCallback(ErrorCallback callback) { errorCallback = callback; }
    void setStrobeCallback(std::function<void(bool)> cb)
    {
        strobeCallback = std::move(cb);
    }
    // Capabilities
    virtual std::vector<std::string> getDenoiseModes() const;

    void endStrobeBurst();

    void extractRequestMetadata(libcamera::Request *request, FrameMetadata *metadata);

    const LensPositionRange getLensPositionRange() const;

    #ifdef MEASURE_FRAME_LATENCY
    void setLogFrameMetadata(bool enable) 
    { 
        logFrameMetadata.store(enable); 
    }
#endif
protected:
    // Pure virtual methods - sensor specific
    virtual int32_t validateExposureTime(int32_t exposure_us) const = 0;
    virtual float validateAnalogGain(float gain) const = 0;
    virtual float validateDigitalGain(float gain) const = 0;
    virtual cv::Size getDefaultResolution() const = 0;
    virtual int getNativeBitDepth() const = 0;
    // Default exposure time for manual mode fallback (override in subclasses)
    virtual int32_t getDefaultExposureTime() const { return 10000; } // 10ms default
    virtual libcamera::PixelFormat getRawPixelFormat(int bitDepth) const = 0;

    // Common validation methods
    float validateWbGain(float gain) const;
    float validateLensPosition(float position) const;
    int32_t validateBrightness(int32_t brightness) const;
    float validateImageQuality(float value) const;
    bool resetCameraAcquisition();
    // Control application helpers
    virtual void applyExposureControls(libcamera::ControlList &controls, 
                              const CaptureSettings &settings) const;
    virtual void applyGainControls(libcamera::ControlList &controls, 
                          const CaptureSettings &settings) const;
    virtual void applyFrameDurationControls(libcamera::ControlList &controls,
                                    const CaptureSettings &settings);
    virtual void applyDirectDigitalGain(float gain) const 
    {

        // Some sensors support direct digital gain control, while others require it to be applied via the analog gain or a combined gain control.
        // This method can be overridden by subclasses to apply digital gain directly if supported.
    
        // Default implementation does nothing, as many sensors do not support direct digital gain control.}
    
        LOG_WARNING << getSensorType() << ": Digital gain of " << gain 
                    << " requested but sensor does not support direct digital gain control - ignoring" << std::endl;
    
    }
    virtual void applyWhiteBalanceControls(libcamera::ControlList &controls, 
                                   const CaptureSettings &settings) const;
    virtual void applyFocusControls(libcamera::ControlList &controls, 
                           const CaptureSettings &settings) const;

    void applyAdvancedControls(libcamera::ControlList &controls, 
                              const CaptureSettings &settings) const;
    
    void extractConvergedValues(libcamera::Request *request, ConvergedSettings *out);
    void applyConvergedSettings(libcamera::ControlList &controls,
                                const ConvergedSettings &converged,
                                const CaptureSettings &settings);

    // Request callback
    virtual void requestComplete(libcamera::Request* request);

    // Error handling
    void setError(const std::string &error);
   
    // Camera state
    std::shared_ptr<libcamera::Camera> camera;
    std::string cameraId;
    std::unique_ptr<libcamera::CameraConfiguration> config;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator;
    std::vector<std::unique_ptr<libcamera::Request>> requests;
    
    // Streaming state
    bool streaming = false;
    bool initialized = false;
    bool streamFocusNeedsApply = false;
    cv::Size maxResolution;
    ThreadSafeQueue<FrameData> frameQueue;
    
    // Dynamic settings
    std::mutex settingsMutex;
    CaptureSettings pendingSettings;
    CaptureSettings currentStreamSettings;
    bool hasPendingSettings = false;
    
    // Error handling
    std::string lastError;
    ErrorCallback errorCallback;

    mutable SensorInfo timingHelper;
    SensorTiming currentTiming;

    // Resolved once at init time; points into
    // /sys/devices/.../<CFE>/sanuwave_strobe. Non-empty iff the patched
    // rp1-cfe module is loaded and matches this camera's CFE instance.
    std::string kernelStrobeSysfsPath;

    // Walks /sys/class/video4linux to find the rp1-cfe image output node
    // whose CFE has this camera's sensor as an I2C child, then records
    // the sanuwave_strobe sysfs path for later writes. Returns false if
    // the patched module is not loaded for this camera's CFE.
    bool resolveKernelStrobeSysfsPath();

private:
    std::mutex stillCaptureMutex;
    std::condition_variable stillCaptureCV;
    bool stillCaptureComplete = false;
    libcamera::Request *stillCompletedRequest = nullptr;
    
    void onStillCaptureComplete(libcamera::Request *request);
    bool isAeAwbConverged(libcamera::Request *request);
    bool performWarmupCaptures(libcamera::Stream *stream, libcamera::FrameBufferAllocator *allocator,
                           ConvergedSettings *convergedOut, const CaptureSettings &settings,
                           int frameCount);

    // Mmap cache for optimized buffer access
    std::unordered_map<int, void*> mmapCache;
    std::unordered_map<int, size_t> mmapLengths;
    std::mutex mmapCacheMutex;

    std::unique_ptr<IJpegEncoder> jpegEncoder;
    std::function<void(bool)> strobeCallback;
#ifdef MEASURE_FRAME_LATENCY
    std::atomic<bool>    logFrameMetadata{false};  // enable per-frame logging
    std::atomic<uint64_t> frameCounter{0};         // rolling frame index
#endif

    bool    burstActive       = false;
    bool    burstWasStreaming  = false;
    CaptureSettings burstSettings;
    std::unique_ptr<libcamera::CameraConfiguration> burstConfig;
    std::unique_ptr<libcamera::FrameBufferAllocator>    burstAllocator;
    libcamera::Stream* burstStream       = nullptr;
    void onVBlankBurstFrameComplete(libcamera::Request* req);

    struct VBlankBurstState
    {
        int numFrames = 0;
        std::vector<VBlankFrameResult> frames;
        std::vector<std::string> roles;
        std::function<void(bool)> strobeToggle;
        std::mutex mutex;
        std::condition_variable cv;
        int completedCount = 0;
        bool failed = false;
        std::vector<libcamera::FrameBuffer *> bufferPtrs;
    };
    std::unique_ptr<VBlankBurstState> vblankState;
};

} // namespace sanuwave

#endif // CAMERA_BASE_H
