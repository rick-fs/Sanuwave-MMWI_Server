// camera_ui_controller.h
#ifndef CAMERA_UI_CONTROLLER_H
#define CAMERA_UI_CONTROLLER_H

#include "camera_param_router.h"
#include <QWidget>
#include <QMap>
#include <functional>

class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;

// Manages the relationship between camera parameters and UI widgets
// Handles enable/disable logic when auto toggles change
class CameraUIController {
public:
    CameraUIController() = default;
    
    // Register a widget for a specific parameter
    void registerWidget(CameraParam param, QWidget* widget);
    
    // Get widget for a parameter
    QWidget* getWidget(CameraParam param) const;
    
    // Handle auto-toggle state changes - enables/disables dependent widgets
    void handleAutoToggle(CameraParam autoParam, bool autoEnabled);
    
    // Special cases where AWB mode combo should be enabled when auto IS on
    void registerAwbModeCombo(CameraParam autoWbParam, QComboBox* combo);

private:
    QMap<CameraParam, QWidget*> widgets_;
    QMap<CameraParam, QComboBox*> awbModeCombos_;  // Special handling for AWB mode
};

#endif // CAMERA_UI_CONTROLLER_H
