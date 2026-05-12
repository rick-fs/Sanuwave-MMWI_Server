// shared/include/protocol_constants.h
// Shared protocol definitions for Sanuwave client-server communication
// Copyright 2026 Sanuwave Medical LLC.
#pragma once

#include <cstdint>
#include <cstring>
namespace sanuwave {
namespace protocol {

// ============================================================================
// CAMERA/SENSOR IDENTIFIERS
// ============================================================================
namespace Camera {
    constexpr const char* IMX708       = "imx708";
    constexpr const char* IMX219       = "imx219";
    constexpr const char* THERMAL      = "thermal";
    constexpr const char* DEPTH        = "depth";
    constexpr const char* DUAL         = "dual";
    constexpr const char* IMX708_STILL = "imx708_still";
    constexpr const char* IMX219_STILL = "imx219_still";
}

// ============================================================================
// MODALITIES
// ============================================================================
namespace Modality {
    constexpr const char* RGB            = "rgb";
    constexpr const char* THERMAL        = "thermal";
    constexpr const char* DUAL           = "dual";
    constexpr const char* RAW            = "raw";
    constexpr const char* ARDUCAM_CUSTOM = "arducam_custom";
    constexpr const char* THREE_D        = "3d";
}
#ifdef UNUSED_DELETE
// ============================================================================
// CAPTURE MODES
// ============================================================================
namespace CaptureMode {
    constexpr const char* STANDARD = "standard";
    constexpr const char* UVBF     = "uvbf";
}
#endif
// ============================================================================
// COMMANDS
// ============================================================================
namespace Command {
    // Streaming
    constexpr const char* STREAM_START = "stream_start";
    constexpr const char* STREAM_STOP  = "stream_stop";

    // RGB/IMX708 Capture
    constexpr const char* CAPTURE_RGB = "capture_rgb";
    constexpr const char* CAPTURE_RAW = "capture_raw";
    constexpr const char* CAPTURE_3D  = "capture_3d";

    // Arducam/IMX219 Capture
    #ifdef UNUSED_DELETE
    constexpr const char* CAPTURE_ARDUCAM        = "capture_arducam";
    #endif
    constexpr const char* CAPTURE_ARDUCAM_CUSTOM = "capture_arducam_custom";

    // Thermal Capture
    constexpr const char* CAPTURE_THERMAL = "capture_thermal";

    // UVBF Capture
    #ifdef UNUSED_DELETE
    constexpr const char* SET_CAPTURE_MODE = "set_capture_mode";
    constexpr const char* FRAME_TRANSFER   = "frame_transfer";
    #endif
    constexpr const char* UVBF_CAPTURE = "uvbf_capture";
    constexpr const char* UVBF_VBLANK_CAPTURE = "uvbf_vblank_capture";
    constexpr const char* UVBF_VBLANK_COMPLETE = "uvbf_vblank_complete";
    // Distance Sensor (VL53L4CD)
    constexpr const char* DISTANCE_INIT      = "distance_init";
    constexpr const char* DISTANCE_START     = "distance_start";
    constexpr const char* DISTANCE_STOP      = "distance_stop";
    constexpr const char* DISTANCE_READ      = "distance_read";
    constexpr const char* DISTANCE_SET_MODE  = "distance_set_mode";
    constexpr const char* DISTANCE_CALIBRATE = "distance_calibrate";

    // UV Sensor (AS7331)
    constexpr const char* UV_INIT     = "uv_init";
    constexpr const char* UV_SHUTDOWN = "uv_shutdown";
    constexpr const char* UV_READ     = "uv_read";
    constexpr const char* UV_SET_MODE = "uv_set_mode";
    constexpr const char* UV_SET_GAIN = "uv_set_gain";
    constexpr const char* UV_SET_TIME = "uv_set_time";

    // ALS Sensor (VD6283)
    constexpr const char* ALS_INIT         = "als_init";
    constexpr const char* ALS_SHUTDOWN     = "als_shutdown";
    constexpr const char* ALS_READ         = "als_read";
    constexpr const char* ALS_SET_GAIN     = "als_set_gain";
    constexpr const char* ALS_SET_EXPOSURE = "als_set_exposure";

    // LED
    constexpr const char* LED_INIT               = "led_init";
    constexpr const char* LED_SHUTDOWN           = "led_shutdown";
    constexpr const char* LED_TORCH              = "led_torch";
    constexpr const char* LED_FLASH              = "led_flash";
    constexpr const char* LED_OFF                = "led_off";
    constexpr const char* LED_STROBE_SYNC_ENABLE = "led_strobe_sync_enable";
    constexpr const char* LED_SET_FLASH_DURATION = "led_set_flash_duration";
    constexpr const char* LED_SET_FLASH_TIMEOUT  = "led_set_flash_timeout";
    constexpr const char* LED_SET_GPIO_MODE      = "led_set_gpio_mode";
    constexpr const char* LED_SELECT             = "led_select";
    constexpr const char* LED_DESELECT           = "led_deselect";
    constexpr const char* LED_GET_STATUS         = "led_get_status";
    constexpr const char* LED_GPIO_FLASH         = "led_gpio_flash";
    constexpr const char* LED_ALL_OFF = "led_all_off";
    // Interval capture
    constexpr const char* INTERVAL_STILL_START = "interval_still_start";
    constexpr const char* INTERVAL_STILL_STOP  = "interval_still_stop";

    // Focus
    constexpr const char* SET_LENS_POSITION = "set_lens_position";

    // System
    constexpr const char* GET_STATUS             = "get_status";
    constexpr const char* GET_SENSOR_TIMING      = "get_sensor_timing";
    constexpr const char* GET_SENSOR_TEMPERATURE = "get_sensor_temperature";
    #ifdef UNUSED_DELETE
    constexpr const char* DETECT_DEVICES         = "detect_devices";
    #endif
    constexpr const char* SET_CAMERA_PARAM       = "set_parameter";
    constexpr const char* GET_PARAMS             = "get_params";
    constexpr const char* GET_ALL_PARAMS         = "get_all_params";
    constexpr const char* RESET_SETTINGS         = "reset_settings";

    // IMU (LSM6DS3TR-C). Server-pushed streaming after IMU_START — no client
    // poll; data arrives as ResponseType::IMU_DATA / IMU_EVENT messages.
    constexpr const char* IMU_INIT       = "imu_init";
    constexpr const char* IMU_SHUTDOWN   = "imu_shutdown";
    constexpr const char* IMU_CONFIGURE  = "imu_configure";
    constexpr const char* IMU_START      = "imu_start";
    constexpr const char* IMU_STOP       = "imu_stop";
    constexpr const char* IMU_SOFT_RESET = "imu_soft_reset";
    constexpr const char* IMU_READ_REG   = "imu_read_reg";    // debug
    constexpr const char* IMU_WRITE_REG  = "imu_write_reg";   // debug
}

// ============================================================================
// DIAGNOSTIC COMMANDS
// ============================================================================
namespace DiagCommand {
    constexpr const char* RAW_CAPTURE     = "diag_raw_capture";
    constexpr const char* RAW_ROI_CAPTURE = "diag_raw_roi_capture";
}

// ============================================================================
// PARAMETER KEYS
// ============================================================================
namespace Param {
    // Common
    constexpr const char* COMMAND     = "command";
    constexpr const char* CAMERA      = "camera";
    constexpr const char* MODALITY    = "modality";
    constexpr const char* MODE        = "mode";
    constexpr const char* WIDTH       = "width";
    constexpr const char* HEIGHT      = "height";
    constexpr const char* QUALITY     = "quality";
    constexpr const char* PARAM       = "param";
    constexpr const char* VALUE       = "value";
    constexpr const char* GAIN        = "gain";
    constexpr const char* EXPOSURE_MS = "exposure_ms";

    // Exposure
    constexpr const char* AUTO_EXPOSURE    = "auto_exposure";
    constexpr const char* EXPOSURE_TIME_US = "exposure_time_us";
    constexpr const char* EV_COMPENSATION  = "ev_compensation";

    // Gain
    constexpr const char* AUTO_ANALOG_GAIN = "auto_analog_gain";
    constexpr const char* ANALOG_GAIN      = "analog_gain";
    constexpr const char* DIGITAL_GAIN     = "digital_gain";

    // White Balance
    constexpr const char* AUTO_WHITE_BALANCE = "auto_white_balance";
    constexpr const char* RED_GAIN           = "red_gain";
    constexpr const char* BLUE_GAIN          = "blue_gain";

    // Focus (IMX708)
    constexpr const char* AUTO_FOCUS    = "auto_focus";
    constexpr const char* LENS_POSITION = "lens_position";

    // Raw capture
    constexpr const char* RAW_MODE      = "raw_mode";
    constexpr const char* RAW_BIT_DEPTH = "raw_bit_depth";

#ifdef DELETEME
    // Enhancement
    constexpr const char* BRIGHTNESS = "brightness";
    constexpr const char* CONTRAST   = "contrast";
    constexpr const char* SATURATION = "saturation";
    constexpr const char* SHARPNESS  = "sharpness";
#endif

    // Thermal
    constexpr const char* EMISSIVITY    = "emissivity";
    constexpr const char* MIN_TEMP      = "min_temp";
    constexpr const char* MAX_TEMP      = "max_temp";
    constexpr const char* COLORMAP      = "colormap";
    constexpr const char* NUC_ENABLED   = "nuc_enabled";
    constexpr const char* ALARM_ENABLED = "alarm_enabled";
    constexpr const char* ALARM_TEMP    = "alarm_temp";
    constexpr const char* SCALE_WIDTH   = "scale_width";
    constexpr const char* SCALE_HEIGHT  = "scale_height";

    // Distance Sensor
    constexpr const char* TIMING_BUDGET         = "timing_budget";
    constexpr const char* INTER_MEASUREMENT     = "inter_measurement";
    constexpr const char* SIGMA_THRESHOLD       = "sigma_threshold";
    constexpr const char* SIGNAL_THRESHOLD      = "signal_threshold";
    constexpr const char* TARGET_DISTANCE       = "target_distance";
    constexpr const char* MEDIAN_FILTER_ENABLED = "median_filter_enabled";
    constexpr const char* MEDIAN_FILTER_SAMPLES = "median_filter_samples";

    // Stream frame info
    constexpr const char* FORMAT       = "format";
    constexpr const char* SIZE         = "size";
    constexpr const char* TIMESTAMP_MS = "timestamp_ms";

    // Advanced camera settings
    constexpr const char* DENOISE_MODE  = "denoise_mode";
    constexpr const char* HDR_MODE      = "hdr_mode";
    constexpr const char* METERING_MODE = "metering_mode";

    // LED GPIO mode
    constexpr const char* LED_GPIO_MODE          = "led_gpio_mode";
    constexpr const char* LED_PRE_FRAME_DELAY_MS = "led_pre_frame_delay_ms";
    constexpr const char* LED_POST_CAPTURE_OFF   = "led_post_capture_off";
    constexpr const char* LED_STROBE_LEAD_TIME_MS = "strobe_lead_time_ms";
    // LED selection for capture
    constexpr const char* LED_IDS          = "led_ids";
    constexpr const char* LED_BRIGHTNESSES = "led_brightnesses";

    // Frame duration lock (streaming only)
    constexpr const char* FRAME_DURATION_ENABLED = "frame_duration_enabled";
    constexpr const char* FRAME_DURATION_US      = "frame_duration_us";

    // Interval capture
    constexpr const char* INTERVAL_MS = "interval_ms";

    // Motion measurement (streaming).
    // Per-frame translation magnitude computed via phase correlation on a
    // centered ROI of the preview frame. Thresholding lives client-side.
    constexpr const char* MOTION_ENABLED   = "motion_enabled";     // bool, default false
    constexpr const char* MOTION_ROI_SIZE  = "motion_roi_size";    // pixels, default 512
    constexpr const char* MOTION_REFERENCE = "motion_reference";   // "previous" | "anchor"

    // UVBF burst motion check (used by UVBF_VBLANK_CAPTURE).
    // Server-side phase correlation between the illuminated frames of a
    // VBlank-timing burst. When false (default), the server skips the
    // computation entirely - no CPU cost, no log line, no motion sub-object
    // on the wire. Client decides PASS/FAIL/etc. from the returned drift
    // values.
    constexpr const char* UVBF_MOTION_CHECK = "uvbf_motion_check";  // bool, default false

    // Sensor temperature
    constexpr const char* TEMPERATURE_CELSIUS  = "celsius";
    constexpr const char* TEMPERATURE_RELIABLE = "reliable";
    constexpr const char* TEMPERATURE_AGE_S    = "age_seconds";

    // Capture mode
    constexpr const char* CAPTURE_MODE = "capture_mode";  // value: CaptureMode::STANDARD | CaptureMode::UVBF

    // Frame transfer
    constexpr const char* FRAME_ROLE    = "frame_role";    // value: FrameRole::BACKGROUND | FrameRole::UV
    constexpr const char* FRAME_INDEX   = "frame_index";
    constexpr const char* FRAME_COUNT   = "frame_count";
    constexpr const char* PAYLOAD_SIZE  = "payload_size";
    constexpr const char* PIXEL_FORMAT  = "pixel_format";
    constexpr const char* SESSION_ID    = "session_id";
}

// ============================================================================
// FRAME ROLES  (used in FRAME_TRANSFER payloads)
// ============================================================================
//
// Wire-format strings for the FRAME_ROLE field. The illuminated-frame role
// pattern is "illuminated_N" where N is 1-based; constants are provided up
// through 9 so a UVBF burst can carry up to 9 illuminated frames without
// stringifying the index on the fly. The LED flash timeout (~400 ms) caps
// the total burst length in practice. The standard UVBF capture uses
// ILLUMINATED_1..3; the VBlank timing experiment can produce variable N.
namespace FrameRole {
    constexpr const char* BACKGROUND    = "background";
    constexpr const char* UV            = "uv";          // legacy — keep for compatibility
    constexpr const char* ILLUMINATED   = "illuminated";
    constexpr const char* DARK          = "dark";
    constexpr const char* DARK_1        = "dark_1";
    constexpr const char* ILLUMINATED_1 = "illuminated_1";
    constexpr const char* ILLUMINATED_2 = "illuminated_2";
    constexpr const char* ILLUMINATED_3 = "illuminated_3";
    constexpr const char* ILLUMINATED_4 = "illuminated_4";
    constexpr const char* ILLUMINATED_5 = "illuminated_5";
    constexpr const char* ILLUMINATED_6 = "illuminated_6";
    constexpr const char* ILLUMINATED_7 = "illuminated_7";
    constexpr const char* ILLUMINATED_8 = "illuminated_8";
    constexpr const char* ILLUMINATED_9 = "illuminated_9";
    constexpr const char* DARK_2        = "dark_2";
}
// ============================================================================
// DEVICE FLAGS  (keys in the get_status response)
// ============================================================================
namespace Device {
    constexpr const char* IMX708_CAMERA   = "imx708";
    constexpr const char* IMX219_CAMERA   = "imx219";
    constexpr const char* THERMAL_CAMERA  = "thermal_camera";
    constexpr const char* DISTANCE_SENSOR = "distance_sensor";
    constexpr const char* UV_SENSOR       = "uv_sensor";
    constexpr const char* ALS_SENSOR      = "als_sensor";
    constexpr const char* LED_MANAGER     = "led_manager";
    constexpr const char* LED_GPIO_MODE   = "led_gpio_mode";
    constexpr const char* IMU_SENSOR      = "imu_sensor";
}

// ============================================================================
// DISTANCE RESPONSE FIELDS
// ============================================================================
namespace DistanceField {
    constexpr const char* DISTANCE_MM      = "distance_mm";
    constexpr const char* DISTANCE_CM      = "distance_cm";
    constexpr const char* DISTANCE_M       = "distance_m";
    constexpr const char* SIGNAL_PER_SPAD  = "signal_per_spad";
    constexpr const char* AMBIENT_PER_SPAD = "ambient_per_spad";
    constexpr const char* NUM_SPADS        = "num_spads";
    constexpr const char* RANGE_STATUS     = "range_status";
    constexpr const char* VALID            = "valid";
}

namespace FrameDurationField {
    constexpr auto MIN_US = "min_frame_duration_us";
    constexpr auto MAX_US = "max_frame_duration_us";
}

// ============================================================================
// MOTION MEASUREMENT FIELDS  (keys in the per-frame stream_frame motion sub-object)
// ============================================================================
//
// Wire shape inside a stream_frame header:
//   "motion": { "valid":true, "trans_px":0.42, "rot_deg":0.0,
//               "confidence":0.87, "ref":"previous" }
//
// valid==false means motion was not measured for this frame (feature disabled,
// first frame of a stream, ROI too large for frame, etc.) — clients should
// treat as "unknown", not "still".
//
// Thresholding is the client's responsibility; the server returns the raw
// scalar so thresholds can be tuned without a server release.
namespace MotionField {
    constexpr const char* OBJECT     = "motion";
    constexpr const char* VALID      = "valid";
    constexpr const char* TRANS_PX   = "trans_px";    // translation magnitude, pixels
    constexpr const char* ROT_DEG    = "rot_deg";     // reserved, 0 for phase correlation
    constexpr const char* CONFIDENCE = "confidence";  // phase correlation peak response
    constexpr const char* REFERENCE  = "ref";
}

namespace MotionReference {
    constexpr const char* PREVIOUS = "previous";  // measure against previous frame's ROI
    constexpr const char* ANCHOR   = "anchor";    // measure against first-frame ROI of stream
}

// ============================================================================
// UVBF BURST MOTION FIELDS  (keys in the per-frame UVBF frame_transfer
// motion sub-object — distinct from MotionField because the schema reports
// TWO pair measurements per frame: rolling "previous" and fixed "anchor".)
// ============================================================================
//
// Wire shape inside an illuminated frame's frame_transfer header (only for
// frames with illum-sequence index k >= 2):
//   "motion": {
//     "valid": true,
//     "prev_trans_px": 1.42,    // shift since the previous illum frame
//     "prev_confidence": 0.31,
//     "anchor_trans_px": 2.10,  // cumulative shift since illum1 (the anchor)
//     "anchor_confidence": 0.18
//   }
//
// Dark frames and the first illuminated frame do not carry a motion
// sub-object. For the second illuminated frame the prev_* and anchor_*
// pairs are identical (illum1 IS the previous frame); the schema is
// reported uniformly for client simplicity.
//
// Thresholding for capture verdict is the client's responsibility.
namespace UvbfMotionField {
    constexpr const char* OBJECT            = "motion";
    constexpr const char* VALID             = "valid";
    constexpr const char* PREV_TRANS_PX     = "prev_trans_px";
    constexpr const char* PREV_CONFIDENCE   = "prev_confidence";
    constexpr const char* ANCHOR_TRANS_PX   = "anchor_trans_px";
    constexpr const char* ANCHOR_CONFIDENCE = "anchor_confidence";
}

// ============================================================================
// VERSION FIELDS  (keys in the get_status response)
// ============================================================================
namespace VersionField {
    constexpr const char* KEY_GIT_HASH    = "git_hash";
    constexpr const char* KEY_GIT_BRANCH  = "git_branch";
    constexpr const char* KEY_BUILD_TIME  = "build_time";
    constexpr const char* KEY_VERSION_STR = "version";
}

// ============================================================================
// DIAGNOSTIC CAPTURE PARAMETERS
// ============================================================================
namespace DiagParam {
    constexpr const char* EXPOSURE_US              = "exposure_us";
    constexpr const char* ANALOG_GAIN              = "analog_gain";
    constexpr const char* DIGITAL_GAIN             = "digital_gain";
    constexpr const char* DISABLE_AWB              = "disable_awb";
    constexpr const char* DISABLE_AE               = "disable_ae";
    constexpr const char* DISABLE_DENOISE          = "disable_denoise";
    constexpr const char* FRAME_COUNT              = "frame_count";
    constexpr const char* LED_PRE_CAPTURE_DELAY_MS = "led_pre_capture_delay_ms";
    // ROI fields
    constexpr const char* ROI_X      = "roi_x";
    constexpr const char* ROI_Y      = "roi_y";
    constexpr const char* ROI_WIDTH  = "roi_width";
    constexpr const char* ROI_HEIGHT = "roi_height";
}

// ============================================================================
// RESPONSE TYPES
// ============================================================================
namespace ResponseType {
    constexpr const char* STATUS             = "status";
    constexpr const char* ERROR              = "error";
    constexpr const char* IMAGE              = "image";
    constexpr const char* STREAM_FRAME       = "stream_frame";
    constexpr const char* CAPTURE_COMPLETE   = "capture_complete";
    constexpr const char* DISTANCE_DATA      = "distance_data";
    constexpr const char* UV_DATA            = "uv_data";
    constexpr const char* ALS_DATA           = "als_data";
    constexpr const char* SENSOR_TIMING      = "sensor_timing";
    constexpr const char* SENSOR_TEMPERATURE = "sensor_temperature";
    constexpr const char* CAPTURE_MODE_ACK   = "set_capture_mode_ack";
    constexpr const char* FRAME_TRANSFER     = "frame_transfer";
    constexpr const char* AMBIENT_LIGHT_WARN = "ambient_light_warning";
    constexpr const char* UVBF_STARTED        = "uvbf_started";
    constexpr const char* UVBF_FRAME_CAPTURED = "uvbf_frame_captured";
    constexpr const char* UVBF_COMPLETE       = "uvbf_complete";
    constexpr const char* UVBF_VBLANK_COMPLETE = "uvbf_vblank_complete";
    constexpr const char* UVBF_ERROR          = "uvbf_error";

    // IMU (LSM6DS3TR-C) — server-pushed.
    constexpr const char* IMU_DATA            = "imu_data";   // batch of samples
    constexpr const char* IMU_EVENT           = "imu_event";  // tap/free-fall/wake
    constexpr const char* IMU_REG             = "imu_reg";    // debug register read result
}

namespace UVBFParam {
    constexpr const char* CAMERA           = "uvbf_camera";
    constexpr const char* EXPOSURE_US      = "uvbf_exposure_us";
    constexpr const char* ANALOG_GAIN      = "uvbf_analog_gain";
    constexpr const char* DIGITAL_GAIN     = "uvbf_digital_gain";  // IMX708 only
    constexpr const char* LED_IDS          = "uvbf_led_ids";       // JSON array of ints 0-31
    constexpr const char* LED_BRIGHTNESS   = "uvbf_led_brightness";// 0-255
    constexpr const char* LED_LEAD_TIME_MS = "uvbf_led_lead_time_ms";
}

// ============================================================================
// DIAGNOSTIC RESPONSE TYPES
// ============================================================================
namespace DiagResponseType {
    constexpr const char* RAW_FRAME = "diag_raw_frame";
    constexpr const char* RAW_ROI   = "diag_raw_roi";
    constexpr const char* RAW_ERROR = "diag_raw_error";
}

// ============================================================================
// DIAGNOSTIC RESPONSE FIELD NAMES
// ============================================================================
namespace DiagResponse {
    constexpr const char* FRAME_INDEX    = "frame_index";
    constexpr const char* FRAME_COUNT    = "frame_count";
    constexpr const char* BITS_PER_PIXEL = "bits_per_pixel";
    constexpr const char* BAYER_PATTERN  = "bayer_pattern";
    constexpr const char* PIXEL_FORMAT   = "pixel_format";
    constexpr const char* DATA_SIZE      = "data_size";
    constexpr const char* METADATA       = "metadata";
    constexpr const char* SENSOR_INFO    = "sensor_info";
    constexpr const char* ROI_X          = "roi_x";
    constexpr const char* ROI_Y          = "roi_y";
    constexpr const char* ROI_WIDTH      = "roi_width";
    constexpr const char* ROI_HEIGHT     = "roi_height";
}

// ============================================================================
// DIAGNOSTIC METADATA FIELD NAMES
// ============================================================================
namespace DiagMeta {
    constexpr const char* ACTUAL_EXPOSURE_US   = "actual_exposure_us";
    constexpr const char* ACTUAL_ANALOG_GAIN   = "actual_analog_gain";
    constexpr const char* ACTUAL_DIGITAL_GAIN  = "actual_digital_gain";
    constexpr const char* COLOUR_GAINS         = "colour_gains";
    constexpr const char* COLOUR_TEMPERATURE   = "colour_temperature";
    constexpr const char* BLACK_LEVEL          = "black_level";
    constexpr const char* SENSOR_BLACK_LEVELS  = "sensor_black_levels";
    constexpr const char* SENSOR_TIMESTAMP_NS  = "sensor_timestamp_ns";
    constexpr const char* LENS_SHADING_APPLIED = "lens_shading_applied";
    constexpr const char* AE_ENABLED           = "ae_enabled";
    constexpr const char* AWB_ENABLED          = "awb_enabled";
}

// ============================================================================
// DIAGNOSTIC SENSOR INFO FIELD NAMES
// ============================================================================
namespace DiagSensorInfo {
    constexpr const char* NAME               = "name";
    constexpr const char* NATIVE_BIT_DEPTH   = "native_bit_depth";
    constexpr const char* ACTIVE_AREA_WIDTH  = "active_area_width";
    constexpr const char* ACTIVE_AREA_HEIGHT = "active_area_height";
}

// ============================================================================
// DIAGNOSTIC LEPTON METADATA FIELD NAMES
// ============================================================================
namespace DiagLeptonMeta {
    constexpr const char* FPA_TEMPERATURE_K  = "fpa_temperature_k";
    constexpr const char* AUX_TEMPERATURE_K  = "aux_temperature_k";
    constexpr const char* RAW_MIN            = "raw_min";
    constexpr const char* RAW_MAX            = "raw_max";
    constexpr const char* FFC_DESIRED        = "ffc_desired";
    constexpr const char* FFC_FRAMES_SINCE   = "ffc_frames_since";
    constexpr const char* GAIN_MODE          = "gain_mode";
    constexpr const char* AGC_ENABLED        = "agc_enabled";
    constexpr const char* RADIOMETRY_ENABLED = "radiometry_enabled";
}

namespace LensRangeField {
    constexpr const char* IMX708_MIN = "imx708_lens_min";
    constexpr const char* IMX708_MAX = "imx708_lens_max";
    constexpr const char* IMX219_MIN = "imx219_lens_min";
    constexpr const char* IMX219_MAX = "imx219_lens_max";
}

// ============================================================================
// DIAGNOSTIC ERROR RESPONSE FIELD NAMES
// ============================================================================
namespace DiagErrorResponse {
    constexpr const char* ERROR_CODE    = "error_code";
    constexpr const char* ERROR_MESSAGE = "error_message";
}

// ============================================================================
// BAYER PATTERN
// ============================================================================
enum class BayerPattern : uint8_t {
    RGGB = 0,
    BGGR = 1,
    GBRG = 2,
    GRBG = 3,
    NONE = 255,
};

inline const char* bayerPatternToString(BayerPattern p)
{
    switch (p) {
        case BayerPattern::RGGB: return "RGGB";
        case BayerPattern::BGGR: return "BGGR";
        case BayerPattern::GBRG: return "GBRG";
        case BayerPattern::GRBG: return "GRBG";
        case BayerPattern::NONE: return "NONE";
        default:                 return "Unknown";
    }
}

inline BayerPattern bayerPatternFromString(const char* s)
{
    if (!s) return BayerPattern::NONE;
    if (std::strcmp(s, "RGGB") == 0) return BayerPattern::RGGB;
    if (std::strcmp(s, "BGGR") == 0) return BayerPattern::BGGR;
    if (std::strcmp(s, "GBRG") == 0) return BayerPattern::GBRG;
    if (std::strcmp(s, "GRBG") == 0) return BayerPattern::GRBG;
    return BayerPattern::NONE;
}

// ============================================================================
// DIAGNOSTIC ERROR CODES
// ============================================================================
enum class DiagError : uint8_t {
    SUCCESS                = 0,
    CAMERA_BUSY_STREAMING  = 1,
    CAMERA_NOT_FOUND       = 2,
    CAPTURE_FAILED         = 3,
    INVALID_PARAMETERS     = 4,
    SENSOR_ERROR           = 5,
};

inline const char* diagErrorToString(DiagError e)
{
    switch (e) {
        case DiagError::SUCCESS:               return "Success";
        case DiagError::CAMERA_BUSY_STREAMING: return "Camera is currently streaming. Stop streaming first.";
        case DiagError::CAMERA_NOT_FOUND:      return "Camera not found";
        case DiagError::CAPTURE_FAILED:        return "Capture failed";
        case DiagError::INVALID_PARAMETERS:    return "Invalid parameters";
        case DiagError::SENSOR_ERROR:          return "Sensor error";
        default:                               return "Unknown error";
    }
}

// ============================================================================
// THERMAL COLORMAPS
// ============================================================================
namespace Colormap {
    constexpr const char* IRONBOW   = "ironbow";
    constexpr const char* RAINBOW   = "rainbow";
    constexpr const char* GRAYSCALE = "grayscale";
    constexpr const char* HOT       = "hot";
    constexpr const char* JET       = "jet";
}

// ============================================================================
// STREAM FORMATS
// ============================================================================
namespace StreamFormat {
    constexpr const char* JPEG = "jpeg";
    constexpr const char* RAW  = "raw";
}

// ============================================================================
// PARAMETER MODES
// ============================================================================
namespace ParamMode {
    constexpr const char* STREAMING = "streaming";
    constexpr const char* CAPTURE   = "capture";
}

// ============================================================================
// STATUS MESSAGES
// ============================================================================
namespace StatusMessage {
    // Stream
    constexpr const char* STREAM_STARTED = "Stream started";
    constexpr const char* STREAM_STOPPED = "Stream stopped";

    // Distance sensor
    constexpr const char* DISTANCE_INITIALIZED = "Distance sensor initialized";
    constexpr const char* DISTANCE_STARTED     = "Distance ranging started";
    constexpr const char* DISTANCE_STOPPED     = "Distance ranging stopped";

    // UV sensor
    constexpr const char* UV_INITIALIZED = "UV sensor initialized";
    constexpr const char* UV_SHUTDOWN    = "UV sensor shutdown";

    // ALS sensor
    constexpr const char* ALS_INITIALIZED = "ALS sensor initialized";
    constexpr const char* ALS_SHUTDOWN    = "ALS sensor shutdown";

    // LED
    constexpr const char* LED_INITIALIZED = "LED manager initialized";
    constexpr const char* LED_SHUTDOWN    = "LED manager shutdown";

    // Lens
    constexpr const char* LENS_POSITION_SET = "Lens position set";

    // IMU (LSM6DS3TR-C)
    constexpr const char* IMU_INITIALIZED = "IMU initialized";
    constexpr const char* IMU_SHUTDOWN_OK = "IMU shutdown";
    constexpr const char* IMU_STARTED     = "IMU streaming started";
    constexpr const char* IMU_STOPPED     = "IMU streaming stopped";
    constexpr const char* IMU_CONFIGURED  = "IMU configured";
    constexpr const char* IMU_REG_WRITTEN = "IMU register written";
}

// ============================================================================
// IMU (LSM6DS3TR-C) — configuration parameters, response fields, event kinds
// ============================================================================
//
// Wire model: server-pushed. After IMU_START the server emits IMU_DATA
// batches and IMU_EVENT messages asynchronously via TCPServer::sendJson-
// Notification. There is no client poll. Streaming stops on client
// disconnect (CommandHandler::onClientDisconnect).

namespace ImuParam {
    // Sample rates — raw register-bit values mapping directly to
    // CTRL1_XL/CTRL2_G ODR field (bits 7:4).
    constexpr const char* ACCEL_ODR = "accel_odr";
    constexpr const char* GYRO_ODR  = "gyro_odr";

    // Full-scale ranges — raw register-bit values.
    constexpr const char* ACCEL_FS  = "accel_fs";
    constexpr const char* GYRO_FS   = "gyro_fs";

    // FIFO.
    constexpr const char* FIFO_MODE       = "fifo_mode";
    constexpr const char* FIFO_WATERMARK  = "fifo_watermark";    // 0..2047 words

    // Block-data-update.
    constexpr const char* BLOCK_DATA_UPDATE = "block_data_update";

    // Tap.
    constexpr const char* TAP_ENABLED   = "tap_enabled";
    constexpr const char* TAP_AXIS_X    = "tap_axis_x";
    constexpr const char* TAP_AXIS_Y    = "tap_axis_y";
    constexpr const char* TAP_AXIS_Z    = "tap_axis_z";
    constexpr const char* TAP_DOUBLE    = "tap_double";
    constexpr const char* TAP_THRESHOLD = "tap_threshold";    // 0..31
    constexpr const char* TAP_SHOCK     = "tap_shock";        // 0..3
    constexpr const char* TAP_QUIET     = "tap_quiet";        // 0..3
    constexpr const char* TAP_DURATION  = "tap_duration";     // 0..15

    // Free-fall.
    constexpr const char* FREE_FALL_ENABLED   = "freefall_enabled";
    constexpr const char* FREE_FALL_THRESHOLD = "freefall_threshold";  // 0..7
    constexpr const char* FREE_FALL_DURATION  = "freefall_duration";   // 0..63

    // Wake-on-motion.
    constexpr const char* WAKE_ENABLED   = "wake_enabled";
    constexpr const char* WAKE_THRESHOLD = "wake_threshold";    // 0..63
    constexpr const char* WAKE_DURATION  = "wake_duration";     // 0..3

    // Interrupt routing.
    constexpr const char* INT1_FIFO_WATERMARK = "int1_fifo_watermark";
    constexpr const char* INT1_FIFO_OVERRUN   = "int1_fifo_overrun";
    constexpr const char* INT1_DATA_READY     = "int1_data_ready";
    constexpr const char* INT2_FREE_FALL      = "int2_free_fall";
    constexpr const char* INT2_SINGLE_TAP     = "int2_single_tap";
    constexpr const char* INT2_DOUBLE_TAP     = "int2_double_tap";

    // Coordinate frame (locked at IMU_START).
    constexpr const char* FRAME_X_SOURCE = "frame_x_source";   // "X"|"Y"|"Z"
    constexpr const char* FRAME_X_SIGN   = "frame_x_sign";     // +1 or -1
    constexpr const char* FRAME_Y_SOURCE = "frame_y_source";
    constexpr const char* FRAME_Y_SIGN   = "frame_y_sign";
    constexpr const char* FRAME_Z_SOURCE = "frame_z_source";
    constexpr const char* FRAME_Z_SIGN   = "frame_z_sign";

    // imu_read_reg / imu_write_reg.
    constexpr const char* REG_ADDRESS = "reg_address";   // 0..0xFF
    constexpr const char* REG_VALUE   = "reg_value";     // 0..0xFF
}

namespace ImuField {
    constexpr const char* VALID = "valid";

    // imu_data — parallel arrays. All have the same length; entries at the
    // same index belong to one sample. Timestamps are host monotonic ns
    // back-interpolated from the FIFO drain time at the configured ODR.
    constexpr const char* COUNT          = "count";
    constexpr const char* TIMESTAMPS_NS  = "t_ns";
    constexpr const char* ACCEL_X        = "ax";
    constexpr const char* ACCEL_Y        = "ay";
    constexpr const char* ACCEL_Z        = "az";
    constexpr const char* GYRO_X         = "gx";
    constexpr const char* GYRO_Y         = "gy";
    constexpr const char* GYRO_Z         = "gz";
    constexpr const char* ACCEL_LSB_TO_G  = "accel_lsb_to_g";
    constexpr const char* GYRO_LSB_TO_DPS = "gyro_lsb_to_dps";
    constexpr const char* FIFO_OVERRUNS  = "fifo_overruns";
    constexpr const char* BUS_ERRORS     = "bus_errors";

    // imu_event.
    constexpr const char* EVENT_KIND        = "kind";
    constexpr const char* EVENT_TIMESTAMP   = "t_ns";
    constexpr const char* EVENT_AXIS_X      = "axis_x";
    constexpr const char* EVENT_AXIS_Y      = "axis_y";
    constexpr const char* EVENT_AXIS_Z      = "axis_z";
    constexpr const char* EVENT_SIGN        = "sign";
    constexpr const char* EVENT_RAW_TAP_SRC  = "raw_tap_src";
    constexpr const char* EVENT_RAW_WAKE_SRC = "raw_wake_src";

    // imu_reg (debug register read response).
    constexpr const char* REG_ADDRESS = "reg_address";
    constexpr const char* REG_VALUE   = "reg_value";
}

namespace ImuEventKind {
    constexpr const char* SINGLE_TAP = "single_tap";
    constexpr const char* DOUBLE_TAP = "double_tap";
    constexpr const char* FREE_FALL  = "free_fall";
    constexpr const char* WAKE_UP    = "wake_up";
}

} // namespace protocol
} // namespace sanuwave
