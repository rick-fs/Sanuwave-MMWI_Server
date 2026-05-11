// command_handler.h
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <map>
#include <atomic>
#include "command_router.h"
#include "camera_base.h"
#include "thermal_camera.h"
#include "vl53l4cd_wrapper.h"
#include "multi_camera_parameter_handler.h"
#include "jpeg_encoder_factory.h"
#include "protocol_constants.h"
#include "led_gpio_controller.h"
#include "param_extractor.h"
#include "capture_session.h"
#include "vblank_strobe_controller.h"
#include "LedMgr.h"
#include "vd6283tx_wrapper.h"
#include "lsm6ds3trc_wrapper.h"
#include "stream_context.h"   // exports StreamFrameCallback + StreamContext

namespace sanuwave
{


class AS7331Wrapper;

class TemperatureMonitor;

class CommandHandler
{
public:
    CommandHandler(
        CameraBase*                          rgbCamera,
        ThermalCamera*                       thermalCamera,
        CameraBase*                          arducamCamera,
        VL53L4CDWrapper*                     distanceSensor,
        AS7331Wrapper*                       uvSensor,
        VD6283TXWrapper*                     alsSensor,
        Lsm6ds3trcWrapper*                   imuSensor,
        LedMgr*                              ledManager,
        LedGpioController*                   ledGpio,
        TemperatureMonitor*                  tempMonitor,
        std::unique_ptr<MultiCameraParameterHandler> paramHandler);
    ~CommandHandler();

    std::string handleCommand(const std::string& jsonCommand);

    void setStreamFrameCallback(StreamFrameCallback callback);
    void setStreamStopCallback(std::function<void()> callback)
    {
        streamStopCallback = callback;
    }
    void setDiagFrameCallback(
        std::function<void(const std::string&, const uint8_t*, size_t)> callback);

    const std::vector<uint8_t>& getLastImageData()    const { return lastImageData; }
    const std::string&          getLastImageModality() const { return lastImageModality; }
    void clearImageData();
    void onClientDisconnect();

    std::string handleGetSensorTemperature(const std::map<std::string, std::string>& params);

    bool isDistanceRanging() const { return distanceRanging; }
    bool isStreaming()        const { return streaming; }
    const std::string& getStreamingModality() const { return streamModality; }
    void setNotifyCallback(std::function<void(const std::string&)> cb)
    {
        notifyCallback = cb;
    }
    void setSendDngCallback(
        std::function<void(const std::string& headerJson,
                        const uint8_t* data, size_t size)> cb)
    {
        sendDngCallback = cb;
    }
    std::string handleUVBFVBlankCapture(
       const std::map<std::string, std::string>& params);

private:
    void registerCommands();

    // -----------------------------------------------------------------------
    // Capture handlers
    // -----------------------------------------------------------------------
    std::string handleCaptureIMX708      (const std::map<std::string, std::string>& p);
    std::string handleCaptureRaw         (const std::map<std::string, std::string>& p);
    std::string handleUVBFCapture(const std::map<std::string, std::string> &params);
    bool uvbfArmLeds(const std::vector<int> &ledIds, uint8_t brightness);
    void uvbfDisarmLeds(const std::vector<int> &ledIds);
    std::string handleCaptureThermal(const std::map<std::string, std::string> &p);
    std::string handleCaptureIMX219Custom(const std::map<std::string, std::string>& p);
#ifdef DELETEME
    std::string handleCapture3D          (const std::map<std::string, std::string>& p);
#endif

    std::vector<uint8_t> uvbfCaptureDng(CameraBase *targetCamera, const CaptureSettings &settings,
                                        std::string &errorOut);

    void uvbfNotify(const std::string &json);

    // -----------------------------------------------------------------------
    // Streaming handlers
    // -----------------------------------------------------------------------
    std::string handleStreamStart(const std::map<std::string, std::string>& p);
    std::string handleStreamStop (const std::map<std::string, std::string>& p);
    bool startIMX708Stream (int width, int height, int quality);
    bool startIMX219Stream (int width, int height, int quality);
    bool startThermalStream(int width, int height, int quality);
    bool startDualStream   (int rgbWidth, int rgbHeight, int quality);

    // -----------------------------------------------------------------------
    // Distance sensor handlers
    // -----------------------------------------------------------------------
    std::string handleDistanceInit      (const std::map<std::string, std::string>& p);
    std::string handleDistanceStart     (const std::map<std::string, std::string>& p);
    std::string handleDistanceStop      (const std::map<std::string, std::string>& p);
    std::string handleDistanceRead      (const std::map<std::string, std::string>& p);
    std::string handleDistanceSetMode   (const std::map<std::string, std::string>& p);
    std::string handleDistanceCalibrate (const std::map<std::string, std::string>& p);

    // -----------------------------------------------------------------------
    // UV sensor handlers
    // -----------------------------------------------------------------------
    std::string handleUVInit    (const std::map<std::string, std::string>& p);
    std::string handleUVShutdown(const std::map<std::string, std::string>& p);
    std::string handleUVRead    (const std::map<std::string, std::string>& p);
    std::string handleUVSetMode (const std::map<std::string, std::string>& p);
    std::string handleUVSetGain (const std::map<std::string, std::string>& p);
    std::string handleUVSetTime (const std::map<std::string, std::string>& p);

    // -----------------------------------------------------------------------
    // ALS sensor handlers
    // -----------------------------------------------------------------------
    std::string handleALSInit       (const std::map<std::string, std::string>& p);
    std::string handleALSShutdown   (const std::map<std::string, std::string>& p);
    std::string handleALSRead       (const std::map<std::string, std::string>& p);
    std::string handleALSSetGain    (const std::map<std::string, std::string>& p);
    std::string handleALSSetExposure(const std::map<std::string, std::string>& p);

    // -----------------------------------------------------------------------
    // LED handlers
    // -----------------------------------------------------------------------
    std::string handleLedInit            (const std::map<std::string, std::string>& p);
    std::string handleLedShutdown        (const std::map<std::string, std::string>& p);
    std::string handleLedTorch           (const std::map<std::string, std::string>& p);
    std::string handleLedFlash           (const std::map<std::string, std::string>& p);
    std::string handleLedOff             (const std::map<std::string, std::string>& p);
    std::string handleLedAllOff          (const std::map<std::string, std::string> &p);
    std::string handleLedStrobeSyncEnable(const std::map<std::string, std::string>& p);
    std::string handleLedSetFlashDuration(const std::map<std::string, std::string>& p);
    std::string handleLedSetFlashTimeout (const std::map<std::string, std::string>& p);
    std::string handleLedSetGpioMode     (const std::map<std::string, std::string>& p);
    std::string handleLedSelect          (const std::map<std::string, std::string>& p);
    std::string handleLedDeselect        (const std::map<std::string, std::string>& p);
    std::string handleLedGetStatus       (const std::map<std::string, std::string>& p);
    // -----------------------------------------------------------------------
    // IMU handlers
    // -------
    std::string handleImuInit       (const std::map<std::string, std::string>& params);
    std::string handleImuShutdown   (const std::map<std::string, std::string>& params);
    std::string handleImuConfigure  (const std::map<std::string, std::string>& params);
    std::string handleImuStart      (const std::map<std::string, std::string>& params);
    std::string handleImuStop       (const std::map<std::string, std::string>& params);
    std::string handleImuSoftReset  (const std::map<std::string, std::string>& params);
    std::string handleImuReadReg    (const std::map<std::string, std::string>& params);
    std::string handleImuWriteReg   (const std::map<std::string, std::string>& params);

    // -----------------------------------------------------------------------
    // Parameter handlers
    // -----------------------------------------------------------------------
    std::string handleSetParam      (const std::map<std::string, std::string>& p);
    std::string handleGetParams     (const std::map<std::string, std::string>& p);
    std::string handleGetAllParams  (const std::map<std::string, std::string>& p);
    std::string handleResetSettings (const std::map<std::string, std::string>& p);

    // -----------------------------------------------------------------------
    // Diagnostic raw capture handlers
    // -----------------------------------------------------------------------
    std::string handleDiagRawCapture   (const std::map<std::string, std::string>& p);
    std::string handleDiagRawRoiCapture(const std::map<std::string, std::string>& p);
    // -----------------------------------------------------------------------
    // Focus handlers   
    // -----------------------------------------------------------------------
    std::string handleSetLensPosition(const std::map<std::string, std::string>& params);

    // Interval shooting handlers
    // -----------------------------------------------------------------------
    std::string handleIntervalStillStart(const std::map<std::string, std::string>& params);
    std::string handleIntervalStillStop();
    bool startIntervalStill(const std::string& camera, int width, int height,
                            int quality, int intervalMs);

    // -----------------------------------------------------------------------
    // Diagnostic helpers
    // -----------------------------------------------------------------------
    CameraBase* getDiagCamera         (const std::string& cameraName) const;
    bool        isDiagCameraStreaming (const std::string& cameraName) const;
    std::string buildDiagError        (const std::string& camera, protocol::DiagError code);
    std::string buildDiagRawHeader(
        const std::string& camera,
        uint8_t frameIndex, uint8_t frameCount,
        uint32_t width, uint32_t height,
        const cv::Mat& rawFrame,
        const FrameMetadata& metadata,
        const CaptureSettings& settings);
    std::string buildLeptonDiagRawHeader(
        uint8_t frameIndex, uint8_t frameCount,
        uint32_t width, uint32_t height,
        size_t dataSize);

    CaptureSettings extractCaptureSettings(const ParamExtractor& p,
                                           int defaultWidth, int defaultHeight);
    std::string handleGetSensorTiming(const std::map<std::string, std::string>& params);

    // -----------------------------------------------------------------------
    // LED capture helpers
    // -----------------------------------------------------------------------

    /// Represents a single LED configured for capture illumination.
    struct LedSelection {
        int     ledId;
        uint8_t brightness;
    };

    /// Turn on selected LEDs (I2C) then assert GPIO torch/strobe lines.
    void prepareLedForCapture();

    /// De-assert GPIO lines then turn off selected LEDs (I2C).
    void finishLedAfterCapture();

    // -----------------------------------------------------------------------
    // Misc helpers
    // -----------------------------------------------------------------------
    void        logCaptureSettings(const CaptureSettings& settings, const std::string& camera);
    std::string handleGetStatus   (const std::map<std::string, std::string>& p);
    std::vector<sanuwave::LedState> getCurrentLedState() const;
    std::string handleLedGpioFlash(const std::map<std::string, std::string> &params);
    std::string ledStateToJson(const std::vector<LedState> &states);
    std::vector<uint8_t> matToJpeg(const cv::Mat &image, int quality);
    static std::string buildJsonError  (const std::string& message);
    static std::string buildJsonSuccess(const std::string& message);

    // -----------------------------------------------------------------------
    // Member data
    // -----------------------------------------------------------------------
    CommandRouter      router_;
    CameraBase*        imx708Camera;
    ThermalCamera*     thermalCamera;
    CameraBase*        imx219Camera;
    VL53L4CDWrapper*   distanceSensor;
    AS7331Wrapper*     uvSensor;
    VD6283TXWrapper*     alsSensor;
    Lsm6ds3trcWrapper* imuSensor = nullptr;
    LedMgr*            ledManager;
    LedGpioController* ledGpio;
    TemperatureMonitor* tempMonitor = nullptr;

    // GPIO illumination mode (set via led_set_gpio_mode)
    enum class LedGpioMode { OFF, TORCH, STROBE };
    LedGpioMode ledGpioMode        = LedGpioMode::OFF;
    int         ledPreFrameDelay_ms = 0;
    bool        ledPostCaptureOff   = false;

    // I2C LED selection for capture (set via led_select / led_deselect)
    std::vector<LedSelection> selectedLeds;
    std::unique_ptr<VBlankStrobeController> strobeController = nullptr;
    std::unique_ptr<MultiCameraParameterHandler> multiParams;
    std::unique_ptr<IJpegEncoder> jpegEncoder;
    std::unique_ptr<IJpegEncoder> jpegEncoderDual;

    std::vector<uint8_t> lastImageData;
    std::string          lastImageModality;

    bool              distanceRanging;
    std::atomic<bool> streaming;
    std::atomic<bool> dualStreaming;
    std::atomic<bool> intervalShooting{false};
    std::string       streamModality;
    int               streamQuality;
    int               streamWidth;
    int               streamHeight;

    std::thread streamWorker;
    std::thread dualStreamWorker;
    std::thread intervalStillWorker;
    std::thread uvbfWorker;
    std::mutex streamStateMutex;
    mutable std::mutex ledCaptureMutex;
    std::mutex latestFrameMutex;
    std::condition_variable frameCondition;
    uint64_t latestRgbTimestamp     = 0;
    uint64_t latestThermalTimestamp = 0;
    bool     frameReady             = false;
    std::atomic<bool> uvbfActive{false};
    StreamFrameCallback                                         streamCallback;
    std::function<void()>                                       streamStopCallback{nullptr};
    std::function<void(const std::string&, const uint8_t*, size_t)> diagFrameCallback;

    std::function<void(const std::string&)>                         notifyCallback;
    std::function<void(const std::string&, const uint8_t*, size_t)> sendDngCallback;

    // Motion-measurement config staged by handleStreamStart and consumed
    // by the next start*Stream invocation. Per-call transient; not used
    // across streams. Defaults to disabled.
    StreamContext::MotionConfig pendingMotion_;
};

} // namespace sanuwave
