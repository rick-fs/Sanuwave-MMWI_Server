// camera_param_router.cpp
#include "camera_param_router.h"
#include <QMap>
#include "protocol_constants.h"

using namespace sanuwave::protocol;

namespace {

const QMap<CameraParam, CameraParamInfo>& getParamMap() {
    static const QMap<CameraParam, CameraParamInfo> paramMap = {
        // RGB Streaming
        {CameraParam::RGB_STREAMING_EXPOSURE,           {Camera::IMX708, Param::EXPOSURE_TIME_US, ParamMode::STREAMING}},
        {CameraParam::RGB_STREAMING_AUTO_EXPOSURE,      {Camera::IMX708, Param::AUTO_EXPOSURE, ParamMode::STREAMING}},
        {CameraParam::RGB_STREAMING_EV_COMPENSATION,    {Camera::IMX708, Param::EV_COMPENSATION, ParamMode::STREAMING}},
        {CameraParam::RGB_STREAMING_AUTO_FOCUS,         {Camera::IMX708, Param::AUTO_FOCUS, ParamMode::STREAMING}},
        {CameraParam::RGB_STREAMING_LENS_POSITION,      {Camera::IMX708, Param::LENS_POSITION, ParamMode::STREAMING}},
        {CameraParam::RGB_STREAMING_AUTO_ANALOG_GAIN,   {Camera::IMX708, Param::AUTO_ANALOG_GAIN, ParamMode::STREAMING}},
        {CameraParam::RGB_STREAMING_ANALOG_GAIN,        {Camera::IMX708, Param::ANALOG_GAIN, ParamMode::STREAMING}},
        {CameraParam::RGB_STREAMING_DIGITAL_GAIN,       {Camera::IMX708, Param::DIGITAL_GAIN, ParamMode::STREAMING}},
        {CameraParam::RGB_STREAMING_AUTO_WB,            {Camera::IMX708, Param::AUTO_WHITE_BALANCE, ParamMode::STREAMING}},
        {CameraParam::RGB_STREAMING_RED_GAIN,           {Camera::IMX708, Param::RED_GAIN, ParamMode::STREAMING}},
        {CameraParam::RGB_STREAMING_BLUE_GAIN,          {Camera::IMX708, Param::BLUE_GAIN, ParamMode::STREAMING}},
        { CameraParam::RGB_STREAMING_FRAME_DURATION_ENABLED,
        { Camera::IMX708, Param::FRAME_DURATION_ENABLED, ParamMode::STREAMING } },
        { CameraParam::RGB_STREAMING_FRAME_DURATION_US,
        { Camera::IMX708, Param::FRAME_DURATION_US,      ParamMode::STREAMING } },
        // RGB Capture
        {CameraParam::RGB_CAPTURE_EXPOSURE,             {Camera::IMX708, Param::EXPOSURE_TIME_US, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_AUTO_EXPOSURE,        {Camera::IMX708, Param::AUTO_EXPOSURE, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_EV_COMPENSATION,      {Camera::IMX708, Param::EV_COMPENSATION, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_AUTO_FOCUS,           {Camera::IMX708, Param::AUTO_FOCUS, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_LENS_POSITION,        {Camera::IMX708, Param::LENS_POSITION, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_AUTO_ANALOG_GAIN,     {Camera::IMX708, Param::AUTO_ANALOG_GAIN, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_ANALOG_GAIN,          {Camera::IMX708, Param::ANALOG_GAIN, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_DIGITAL_GAIN,         {Camera::IMX708, Param::DIGITAL_GAIN, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_AUTO_WB,              {Camera::IMX708, Param::AUTO_WHITE_BALANCE, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_RED_GAIN,             {Camera::IMX708, Param::RED_GAIN, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_BLUE_GAIN,            {Camera::IMX708, Param::BLUE_GAIN, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_RAW_MODE,             {Camera::IMX708, Param::RAW_MODE, ParamMode::CAPTURE}},
        {CameraParam::RGB_CAPTURE_RAW_BIT_DEPTH,        {Camera::IMX708, Param::RAW_BIT_DEPTH, ParamMode::CAPTURE}},

        // Arducam Streaming
        {CameraParam::ARDUCAM_STREAMING_EXPOSURE,       {Camera::IMX219, Param::EXPOSURE_TIME_US, ParamMode::STREAMING}},
        {CameraParam::ARDUCAM_STREAMING_EV_COMPENSATION,{Camera::IMX219, Param::EV_COMPENSATION, ParamMode::STREAMING}},
        {CameraParam::ARDUCAM_STREAMING_AUTO_EXPOSURE,  {Camera::IMX219, Param::AUTO_EXPOSURE, ParamMode::STREAMING}},
        {CameraParam::ARDUCAM_STREAMING_AUTO_FOCUS,       {Camera::IMX219, Param::AUTO_FOCUS, ParamMode::STREAMING}},     // ADD
        {CameraParam::ARDUCAM_STREAMING_LENS_POSITION,  {Camera::IMX219, Param::LENS_POSITION, ParamMode::STREAMING}},         {CameraParam::ARDUCAM_STREAMING_ANALOG_GAIN,    {Camera::IMX219, Param::ANALOG_GAIN, ParamMode::STREAMING}},
        {CameraParam::ARDUCAM_STREAMING_DIGITAL_GAIN,   {Camera::IMX219, Param::DIGITAL_GAIN, ParamMode::STREAMING}},
        {CameraParam::ARDUCAM_STREAMING_AUTO_WB,        {Camera::IMX219, Param::AUTO_WHITE_BALANCE, ParamMode::STREAMING}},
        {CameraParam::ARDUCAM_STREAMING_RED_GAIN,       {Camera::IMX219, Param::RED_GAIN, ParamMode::STREAMING}},
        {CameraParam::ARDUCAM_STREAMING_BLUE_GAIN,      {Camera::IMX219, Param::BLUE_GAIN, ParamMode::STREAMING}},
        { CameraParam::ARDUCAM_STREAMING_FRAME_DURATION_ENABLED,
        { Camera::IMX219, Param::FRAME_DURATION_ENABLED, ParamMode::STREAMING } },
        { CameraParam::ARDUCAM_STREAMING_FRAME_DURATION_US,
        { Camera::IMX219, Param::FRAME_DURATION_US,      ParamMode::STREAMING } },
        // Arducam Capture
        {CameraParam::ARDUCAM_CAPTURE_EXPOSURE,         {Camera::IMX219, Param::EXPOSURE_TIME_US, ParamMode::CAPTURE}},
        {CameraParam::ARDUCAM_CAPTURE_EV_COMPENSATION,  {Camera::IMX219, Param::EV_COMPENSATION, ParamMode::CAPTURE}},
        {CameraParam::ARDUCAM_CAPTURE_AUTO_EXPOSURE,    {Camera::IMX219, Param::AUTO_EXPOSURE, ParamMode::CAPTURE}},
        {CameraParam::ARDUCAM_CAPTURE_AUTO_FOCUS,       {Camera::IMX219, Param::AUTO_FOCUS, ParamMode::CAPTURE}},   
        {CameraParam::ARDUCAM_CAPTURE_LENS_POSITION,    {Camera::IMX219, Param::LENS_POSITION, ParamMode::CAPTURE}},    
        {CameraParam::ARDUCAM_CAPTURE_ANALOG_GAIN,      {Camera::IMX219, Param::ANALOG_GAIN, ParamMode::CAPTURE}},
        {CameraParam::ARDUCAM_CAPTURE_DIGITAL_GAIN,     {Camera::IMX219, Param::DIGITAL_GAIN, ParamMode::CAPTURE}},
        {CameraParam::ARDUCAM_CAPTURE_AUTO_WB,          {Camera::IMX219, Param::AUTO_WHITE_BALANCE, ParamMode::CAPTURE}},
        {CameraParam::ARDUCAM_CAPTURE_RED_GAIN,         {Camera::IMX219, Param::RED_GAIN, ParamMode::CAPTURE}},
        {CameraParam::ARDUCAM_CAPTURE_BLUE_GAIN,        {Camera::IMX219, Param::BLUE_GAIN, ParamMode::CAPTURE}},
        {CameraParam::ARDUCAM_CAPTURE_RAW_MODE,         {Camera::IMX219, Param::RAW_MODE, ParamMode::CAPTURE}},
        {CameraParam::ARDUCAM_CAPTURE_RAW_BIT_DEPTH,    {Camera::IMX219, Param::RAW_BIT_DEPTH, ParamMode::CAPTURE}},

        // Thermal Streaming
        {CameraParam::THERMAL_STREAMING_EMISSIVITY,     {Camera::THERMAL, Param::EMISSIVITY, ParamMode::STREAMING}},
        {CameraParam::THERMAL_STREAMING_MIN_TEMP,       {Camera::THERMAL, Param::MIN_TEMP, ParamMode::STREAMING}},
        {CameraParam::THERMAL_STREAMING_MAX_TEMP,       {Camera::THERMAL, Param::MAX_TEMP, ParamMode::STREAMING}},
        {CameraParam::THERMAL_STREAMING_COLORMAP,       {Camera::THERMAL, Param::COLORMAP, ParamMode::STREAMING}},
        {CameraParam::THERMAL_STREAMING_NUC_ENABLED,    {Camera::THERMAL, Param::NUC_ENABLED, ParamMode::STREAMING}},
        {CameraParam::THERMAL_STREAMING_ALARM_ENABLED,  {Camera::THERMAL, Param::ALARM_ENABLED, ParamMode::STREAMING}},
        {CameraParam::THERMAL_STREAMING_ALARM_TEMP,     {Camera::THERMAL, Param::ALARM_TEMP, ParamMode::STREAMING}},

        // Thermal Capture
        {CameraParam::THERMAL_CAPTURE_EMISSIVITY,       {Camera::THERMAL, Param::EMISSIVITY, ParamMode::CAPTURE}},
        {CameraParam::THERMAL_CAPTURE_MIN_TEMP,         {Camera::THERMAL, Param::MIN_TEMP, ParamMode::CAPTURE}},
        {CameraParam::THERMAL_CAPTURE_MAX_TEMP,         {Camera::THERMAL, Param::MAX_TEMP, ParamMode::CAPTURE}},
        {CameraParam::THERMAL_CAPTURE_COLORMAP,         {Camera::THERMAL, Param::COLORMAP, ParamMode::CAPTURE}},
        {CameraParam::THERMAL_CAPTURE_NUC_ENABLED,      {Camera::THERMAL, Param::NUC_ENABLED, ParamMode::CAPTURE}},
        {CameraParam::THERMAL_CAPTURE_ALARM_ENABLED,    {Camera::THERMAL, Param::ALARM_ENABLED, ParamMode::CAPTURE}},
        {CameraParam::THERMAL_CAPTURE_ALARM_TEMP,       {Camera::THERMAL, Param::ALARM_TEMP, ParamMode::CAPTURE}},

        // Depth Sensor
        {CameraParam::DEPTH_TIMING_BUDGET,              {Camera::DEPTH, Param::TIMING_BUDGET, ParamMode::CAPTURE}},
        {CameraParam::DEPTH_INTER_MEASUREMENT,          {Camera::DEPTH, Param::INTER_MEASUREMENT, ParamMode::CAPTURE}},
        {CameraParam::DEPTH_SIGMA_THRESHOLD,            {Camera::DEPTH, Param::SIGMA_THRESHOLD, ParamMode::CAPTURE}},
        {CameraParam::DEPTH_SIGNAL_THRESHOLD,           {Camera::DEPTH, Param::SIGNAL_THRESHOLD, ParamMode::CAPTURE}},
        {CameraParam::DEPTH_MEDIAN_FILTER_ENABLED,      {Camera::DEPTH, Param::MEDIAN_FILTER_ENABLED, ParamMode::CAPTURE}},
        {CameraParam::DEPTH_MEDIAN_FILTER_SAMPLES,      {Camera::DEPTH, Param::MEDIAN_FILTER_SAMPLES, ParamMode::CAPTURE}},
    };
    return paramMap;
}

// Map auto toggles to their dependent controls
struct AutoToggleInfo {
    std::vector<CameraParam> dependentParams;
    bool enableWhenAutoOn;  // For AWB mode combo, enabled when auto is ON
};

const QMap<CameraParam, AutoToggleInfo>& getAutoToggleMap() {
    static const QMap<CameraParam, AutoToggleInfo> autoToggleMap = {
        // RGB Streaming
        {CameraParam::RGB_STREAMING_AUTO_EXPOSURE, {
            {CameraParam::RGB_STREAMING_EXPOSURE}, false
        }},
        {CameraParam::RGB_STREAMING_AUTO_FOCUS, {
            {CameraParam::RGB_STREAMING_LENS_POSITION}, false
        }},
        {CameraParam::RGB_STREAMING_AUTO_ANALOG_GAIN, {
            {CameraParam::RGB_STREAMING_ANALOG_GAIN, CameraParam::RGB_STREAMING_DIGITAL_GAIN}, false
        }},
        {CameraParam::RGB_STREAMING_AUTO_WB, {
            {CameraParam::RGB_STREAMING_RED_GAIN, CameraParam::RGB_STREAMING_BLUE_GAIN}, false
        }},

        // RGB Capture
        {CameraParam::RGB_CAPTURE_AUTO_EXPOSURE, {
            {CameraParam::RGB_CAPTURE_EXPOSURE}, false
        }},
        {CameraParam::RGB_CAPTURE_AUTO_FOCUS, {
            {CameraParam::RGB_CAPTURE_LENS_POSITION}, false
        }},
        {CameraParam::RGB_CAPTURE_AUTO_ANALOG_GAIN, {
            {CameraParam::RGB_CAPTURE_ANALOG_GAIN, CameraParam::RGB_CAPTURE_DIGITAL_GAIN}, false
        }},
        {CameraParam::RGB_CAPTURE_AUTO_WB, {
            {CameraParam::RGB_CAPTURE_RED_GAIN, CameraParam::RGB_CAPTURE_BLUE_GAIN}, false
        }},
        {CameraParam::RGB_CAPTURE_RAW_MODE, {
            {CameraParam::RGB_CAPTURE_RAW_BIT_DEPTH}, false
        }},

        // Arducam Streaming
        {CameraParam::ARDUCAM_STREAMING_AUTO_EXPOSURE, {
            {CameraParam::ARDUCAM_STREAMING_EXPOSURE}, false
        }},
        {CameraParam::ARDUCAM_STREAMING_AUTO_WB, {
            {CameraParam::ARDUCAM_STREAMING_RED_GAIN, CameraParam::ARDUCAM_STREAMING_BLUE_GAIN}, false
        }},

        // Arducam Capture
        {CameraParam::ARDUCAM_CAPTURE_AUTO_EXPOSURE, {
            {CameraParam::ARDUCAM_CAPTURE_EXPOSURE}, false
        }},
        {CameraParam::ARDUCAM_CAPTURE_AUTO_WB, {
            {CameraParam::ARDUCAM_CAPTURE_RED_GAIN, CameraParam::ARDUCAM_CAPTURE_BLUE_GAIN}, false
        }},
        {CameraParam::ARDUCAM_CAPTURE_RAW_MODE, {
            {CameraParam::ARDUCAM_CAPTURE_RAW_BIT_DEPTH}, false
        }},

        // Thermal Streaming
        {CameraParam::THERMAL_STREAMING_ALARM_ENABLED, {
            {CameraParam::THERMAL_STREAMING_ALARM_TEMP}, false
        }},

        // Thermal Capture
        {CameraParam::THERMAL_CAPTURE_ALARM_ENABLED, {
            {CameraParam::THERMAL_CAPTURE_ALARM_TEMP}, false
        }},
    };
    return autoToggleMap;
}

} // anonymous namespace

std::optional<CameraParamInfo> CameraParamRouter::lookup(CameraParam param) {
    const auto& map = getParamMap();
    auto it = map.find(param);
    if (it != map.end()) {
        return *it;
    }
    return std::nullopt;
}

bool CameraParamRouter::isAutoToggle(CameraParam param) {
    return getAutoToggleMap().contains(param);
}

std::optional<CameraParam> CameraParamRouter::getRelatedManualParam(CameraParam autoParam) {
    const auto& map = getAutoToggleMap();
    auto it = map.find(autoParam);
    if (it != map.end() && !it->dependentParams.empty()) {
        return it->dependentParams[0];
    }
    return std::nullopt;
}
