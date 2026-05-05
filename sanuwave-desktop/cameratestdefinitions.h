// cameratestdefinitions.h
#ifndef CAMERATESTDEFINITIONS_H
#define CAMERATESTDEFINITIONS_H

#include <QString>
#include <QVariant>
#include <QVector>
#include <QDateTime>
#include <functional>

namespace CameraTest {

// Test result for individual steps
enum class StepResult {
    Pending,
    Pass,
    Fail,
    Skip,
    Timeout
};

// Overall test status
enum class TestStatus {
    NotStarted,
    Running,
    Paused,
    Completed,
    Aborted
};

// Test categories
enum class TestCategory {
    Connection,
    RGBCapture,
    RGBStreaming,
    ThermalCapture,
    ThermalStreaming,
    ArducamCapture,
    ArducamStreaming,
    DistanceSensor,
    UVSensor,
    ExposureControl,
    GainControl,
    WhiteBalance,
    Focus,
    ColorMap,
    FullSystem
};

// Single test step
struct TestStep {
    QString name;                    // Step name for display
    QString instruction;             // User instruction text
    QString controlToHighlight;      // Widget object name to highlight
    QString parameter;               // Parameter name to modify
    QString camera;                  // Camera: "rgb", "arducam", "thermal"
    QString mode;                    // Mode: "streaming" or "capture"
    QVariant targetValue;            // Value to set
    int durationMs;                  // How long to hold this value
    bool requiresUserVerification;   // If true, wait for Pass/Fail/Skip
    QString expectedObservation;     // What user should see
    StepResult result;               // Result after execution
    QString notes;                   // User notes/comments
    
    TestStep() : durationMs(2000), requiresUserVerification(true), result(StepResult::Pending) {}
};

// Complete test definition
struct TestDefinition {
    QString id;                      // Unique identifier
    QString name;                    // Display name
    QString description;             // What this test verifies
    TestCategory category;           // Category for grouping
    QVector<TestStep> steps;         // Sequence of steps
    bool requiresStreaming;          // Auto-start stream if not active?
    QString requiredCamera;          // "rgb", "arducam", "thermal", or empty for any
    int estimatedDurationSec;        // Estimated total time
    
    TestDefinition() : category(TestCategory::FullSystem), requiresStreaming(true), estimatedDurationSec(30) {}
};

// Test result record
struct TestResult {
    QString testId;
    QString testName;
    TestCategory category;
    TestStatus status;
    QDateTime startTime;
    QDateTime endTime;
    int totalSteps;
    int passedSteps;
    int failedSteps;
    int skippedSteps;
    QVector<TestStep> stepResults;   // Copy of steps with results filled in
    QString operatorName;
    QString deviceSerial;
    QString notes;
    
    TestResult() : status(TestStatus::NotStarted), totalSteps(0), passedSteps(0), failedSteps(0), skippedSteps(0) {}
    
    double passRate() const {
        int completed = passedSteps + failedSteps;
        return completed > 0 ? (double)passedSteps / completed * 100.0 : 0.0;
    }
};

// Helper to convert enums to strings
inline QString categoryToString(TestCategory cat) {
    switch (cat) {
        case TestCategory::Connection: return "Connection";
        case TestCategory::RGBCapture: return "RGB Capture";
        case TestCategory::RGBStreaming: return "RGB Streaming";
        case TestCategory::ThermalCapture: return "Thermal Capture";
        case TestCategory::ThermalStreaming: return "Thermal Streaming";
        case TestCategory::ArducamCapture: return "Arducam Capture";
        case TestCategory::ArducamStreaming: return "Arducam Streaming";
        case TestCategory::DistanceSensor: return "Distance Sensor";
        case TestCategory::UVSensor: return "UV Sensor";
        case TestCategory::ExposureControl: return "Exposure Control";
        case TestCategory::GainControl: return "Gain Control";
        case TestCategory::WhiteBalance: return "White Balance";
        case TestCategory::Focus: return "Focus";
        case TestCategory::ColorMap: return "Color Map";
        case TestCategory::FullSystem: return "Full System";
    }
    return "Unknown";
}

inline QString stepResultToString(StepResult result) {
    switch (result) {
        case StepResult::Pending: return "Pending";
        case StepResult::Pass: return "✓ Pass";
        case StepResult::Fail: return "✗ Fail";
        case StepResult::Skip: return "⊘ Skip";
        case StepResult::Timeout: return "⏱ Timeout";
    }
    return "Unknown";
}

inline QString testStatusToString(TestStatus status) {
    switch (status) {
        case TestStatus::NotStarted: return "Not Started";
        case TestStatus::Running: return "Running";
        case TestStatus::Paused: return "Paused";
        case TestStatus::Completed: return "Completed";
        case TestStatus::Aborted: return "Aborted";
    }
    return "Unknown";
}

} // namespace CameraTest

#endif // CAMERATESTDEFINITIONS_H
