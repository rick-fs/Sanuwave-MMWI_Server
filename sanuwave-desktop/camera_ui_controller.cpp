// camera_ui_controller.cpp
#include "camera_ui_controller.h"
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>

void CameraUIController::registerWidget(CameraParam param, QWidget* widget) {
    if (widget) {
        widgets_[param] = widget;
    }
}

QWidget* CameraUIController::getWidget(CameraParam param) const {
    return widgets_.value(param, nullptr);
}

void CameraUIController::registerAwbModeCombo(CameraParam autoWbParam, QComboBox* combo) {
    if (combo) {
        awbModeCombos_[autoWbParam] = combo;
    }
}

void CameraUIController::handleAutoToggle(CameraParam autoParam, bool autoEnabled) {
    // Define the relationships between auto toggles and their dependent controls
    static const QMap<CameraParam, std::vector<CameraParam>> dependencies = {
        // RGB Streaming
        {CameraParam::RGB_STREAMING_AUTO_EXPOSURE, 
            {CameraParam::RGB_STREAMING_EXPOSURE}},
        {CameraParam::RGB_STREAMING_AUTO_FOCUS, 
            {CameraParam::RGB_STREAMING_LENS_POSITION}},
        {CameraParam::RGB_STREAMING_AUTO_ANALOG_GAIN, 
            {CameraParam::RGB_STREAMING_ANALOG_GAIN, CameraParam::RGB_STREAMING_DIGITAL_GAIN}},
        {CameraParam::RGB_STREAMING_AUTO_WB, 
            {CameraParam::RGB_STREAMING_RED_GAIN, CameraParam::RGB_STREAMING_BLUE_GAIN}},

        // RGB Capture
        {CameraParam::RGB_CAPTURE_AUTO_EXPOSURE, 
            {CameraParam::RGB_CAPTURE_EXPOSURE}},
        {CameraParam::RGB_CAPTURE_AUTO_FOCUS, 
            {CameraParam::RGB_CAPTURE_LENS_POSITION}},
        {CameraParam::RGB_CAPTURE_AUTO_ANALOG_GAIN, 
            {CameraParam::RGB_CAPTURE_ANALOG_GAIN, CameraParam::RGB_CAPTURE_DIGITAL_GAIN}},
        {CameraParam::RGB_CAPTURE_AUTO_WB, 
            {CameraParam::RGB_CAPTURE_RED_GAIN, CameraParam::RGB_CAPTURE_BLUE_GAIN}},
        // Raw mode enables bit depth selection
        {CameraParam::RGB_CAPTURE_RAW_MODE, 
            {CameraParam::RGB_CAPTURE_RAW_BIT_DEPTH}},

        // Arducam Streaming
        {CameraParam::ARDUCAM_STREAMING_AUTO_EXPOSURE, 
            {CameraParam::ARDUCAM_STREAMING_EXPOSURE}},
        {CameraParam::ARDUCAM_STREAMING_AUTO_WB, 
            {CameraParam::ARDUCAM_STREAMING_RED_GAIN, CameraParam::ARDUCAM_STREAMING_BLUE_GAIN}},

        // Arducam Capture
        {CameraParam::ARDUCAM_CAPTURE_AUTO_EXPOSURE, 
            {CameraParam::ARDUCAM_CAPTURE_EXPOSURE}},
        {CameraParam::ARDUCAM_CAPTURE_AUTO_WB, 
            {CameraParam::ARDUCAM_CAPTURE_RED_GAIN, CameraParam::ARDUCAM_CAPTURE_BLUE_GAIN}},
        // Raw mode enables bit depth selection
        {CameraParam::ARDUCAM_CAPTURE_RAW_MODE, 
            {CameraParam::ARDUCAM_CAPTURE_RAW_BIT_DEPTH}},

        // Thermal
        {CameraParam::THERMAL_STREAMING_ALARM_ENABLED, 
            {CameraParam::THERMAL_STREAMING_ALARM_TEMP}},
        {CameraParam::THERMAL_CAPTURE_ALARM_ENABLED, 
            {CameraParam::THERMAL_CAPTURE_ALARM_TEMP}},
    };

    // For most auto toggles: disable dependent controls when auto is enabled
    // For raw mode: ENABLE dependent controls when raw mode is enabled
    bool isRawModeToggle = (autoParam == CameraParam::RGB_CAPTURE_RAW_MODE ||
                            autoParam == CameraParam::ARDUCAM_CAPTURE_RAW_MODE);

    auto it = dependencies.find(autoParam);
    if (it != dependencies.end()) {
        for (CameraParam dep : it.value()) {
            if (QWidget* w = getWidget(dep)) {
                if (isRawModeToggle) {
                    // Raw mode: enable bit depth when raw is ON
                    w->setEnabled(autoEnabled);
                } else {
                    // Auto toggles: disable manual control when auto is ON
                    w->setEnabled(!autoEnabled);
                }
            }
        }
    }

    // Special case: AWB mode combo is ENABLED when auto WB is on
    if (awbModeCombos_.contains(autoParam)) {
        awbModeCombos_[autoParam]->setEnabled(autoEnabled);
    }
}