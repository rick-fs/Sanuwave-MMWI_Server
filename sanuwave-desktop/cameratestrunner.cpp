// cameratestrunner.cpp
#include "cameratestrunner.h"
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

CameraTestRunner::CameraTestRunner(QObject *parent)
    : QObject(parent)
    , m_currentStepIndex(-1)
    , m_running(false)
    , m_paused(false)
    , m_awaitingUserResponse(false)
    , m_remainingMs(0)
{
    m_stepTimer = new QTimer(this);
    m_stepTimer->setSingleShot(true);
    connect(m_stepTimer, &QTimer::timeout, this, &CameraTestRunner::onStepDurationTimeout);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(1000);
    connect(m_tickTimer, &QTimer::timeout, this, &CameraTestRunner::onStepTimerTimeout);

    initializeTestLibrary();
}

CameraTestRunner::~CameraTestRunner()
{
}

void CameraTestRunner::initializeTestLibrary()
{
    auto builtIn = createBuiltInTests();
    for (const auto &test : builtIn) {
        m_testLibrary.insert(test.id, test);
    }
}

void CameraTestRunner::registerTest(const CameraTest::TestDefinition &test)
{
    m_testLibrary.insert(test.id, test);
}

QVector<CameraTest::TestDefinition> CameraTestRunner::getAvailableTests() const
{
    return m_testLibrary.values().toVector();
}

QVector<CameraTest::TestDefinition> CameraTestRunner::getTestsByCategory(CameraTest::TestCategory category) const
{
    QVector<CameraTest::TestDefinition> result;
    for (const auto &test : m_testLibrary) {
        if (test.category == category) {
            result.append(test);
        }
    }
    return result;
}

CameraTest::TestDefinition CameraTestRunner::getTestById(const QString &id) const
{
    return m_testLibrary.value(id);
}

void CameraTestRunner::startTest(const QString &testId)
{
    if (m_running) {
        emit statusMessage("Test already running. Abort current test first.");
        return;
    }

    if (!m_testLibrary.contains(testId)) {
        emit statusMessage("Unknown test ID: " + testId);
        return;
    }

    m_currentTest = m_testLibrary.value(testId);
    m_currentStepIndex = -1;
    m_running = true;
    m_paused = false;
    m_awaitingUserResponse = false;
    m_savedValues.clear();

    // Initialize result
    m_currentResult = CameraTest::TestResult();
    m_currentResult.testId = m_currentTest.id;
    m_currentResult.testName = m_currentTest.name;
    m_currentResult.category = m_currentTest.category;
    m_currentResult.status = CameraTest::TestStatus::Running;
    m_currentResult.startTime = QDateTime::currentDateTime();
    m_currentResult.totalSteps = m_currentTest.steps.size();
    m_currentResult.stepResults = m_currentTest.steps;

    emit testStarted(m_currentTest.id, m_currentTest.name);
    emit statusMessage("Starting test: " + m_currentTest.name);

    // Request stream start if needed
    if (m_currentTest.requiresStreaming && !m_currentTest.requiredCamera.isEmpty()) {
        emit requestStreamStart(m_currentTest.requiredCamera);
    }

    executeNextStep();
}

void CameraTestRunner::abortTest()
{
    if (!m_running) return;

    m_stepTimer->stop();
    m_tickTimer->stop();
    emit requestHighlightControl(QString(), false);
    emit requestRestoreParameters();

    finishTest(CameraTest::TestStatus::Aborted);
    emit testAborted(m_currentResult);
}

void CameraTestRunner::pauseTest()
{
    if (!m_running || m_paused) return;

    m_paused = true;
    m_stepTimer->stop();
    m_tickTimer->stop();
    emit testPaused();
    emit statusMessage("Test paused");
}

void CameraTestRunner::resumeTest()
{
    if (!m_running || !m_paused) return;

    m_paused = false;
    if (m_awaitingUserResponse) {
        emit awaitingUserResponse(currentStep());
    } else if (m_remainingMs > 0) {
        m_stepTimer->start(m_remainingMs);
        m_tickTimer->start();
    }
    emit testResumed();
    emit statusMessage("Test resumed");
}

void CameraTestRunner::recordStepResult(CameraTest::StepResult result, const QString &notes)
{
    if (!m_running || !m_awaitingUserResponse) return;

    m_currentResult.stepResults[m_currentStepIndex].result = result;
    m_currentResult.stepResults[m_currentStepIndex].notes = notes;

    completeCurrentStep(result);
}

void CameraTestRunner::skipCurrentStep()
{
    recordStepResult(CameraTest::StepResult::Skip, "Skipped by operator");
}

CameraTest::TestStep CameraTestRunner::currentStep() const
{
    if (m_currentStepIndex >= 0 && m_currentStepIndex < m_currentTest.steps.size()) {
        return m_currentTest.steps[m_currentStepIndex];
    }
    return CameraTest::TestStep();
}

void CameraTestRunner::setSavedValue(const QString &key, const QVariant &value)
{
    m_savedValues.insert(key, value);
}

QVariant CameraTestRunner::getSavedValue(const QString &key) const
{
    return m_savedValues.value(key);
}

void CameraTestRunner::executeNextStep()
{
    m_currentStepIndex++;

    if (m_currentStepIndex >= m_currentTest.steps.size()) {
        emit requestHighlightControl(QString(), false);
        emit requestRestoreParameters();
        finishTest(CameraTest::TestStatus::Completed);
        emit testCompleted(m_currentResult);
        return;
    }

    executeStep(m_currentTest.steps[m_currentStepIndex]);
}

void CameraTestRunner::executeStep(const CameraTest::TestStep &step)
{
    emit stepStarted(m_currentStepIndex, step);
    emit statusMessage(QString("Step %1/%2: %3")
        .arg(m_currentStepIndex + 1)
        .arg(m_currentTest.steps.size())
        .arg(step.name));
    emit instructionUpdate(step.instruction);

    // Highlight the control
    if (!step.controlToHighlight.isEmpty()) {
        emit requestHighlightControl(step.controlToHighlight, true);
    }

    // Apply parameter change
    if (!step.parameter.isEmpty()) {
        emit requestParameterChange(step.camera, step.parameter, step.targetValue, step.mode);
    }

    // Start duration timer
    m_remainingMs = step.durationMs;
    m_awaitingUserResponse = false;

    if (step.durationMs > 0) {
        m_stepTimer->start(step.durationMs);
        m_tickTimer->start();
    } else if (step.requiresUserVerification) {
        m_awaitingUserResponse = true;
        emit awaitingUserResponse(step);
    } else {
        completeCurrentStep(CameraTest::StepResult::Pass);
    }
}

void CameraTestRunner::onStepTimerTimeout()
{
    m_remainingMs -= 1000;
    if (m_remainingMs < 0) m_remainingMs = 0;
    emit stepTimerTick(m_remainingMs);
}

void CameraTestRunner::onStepDurationTimeout()
{
    m_tickTimer->stop();
    
    const auto &step = m_currentTest.steps[m_currentStepIndex];
    
    // Clear highlight before moving to user response or next step
    emit requestHighlightControl(step.controlToHighlight, false);

    if (step.requiresUserVerification) {
        m_awaitingUserResponse = true;
        emit awaitingUserResponse(step);
    } else {
        completeCurrentStep(CameraTest::StepResult::Pass);
    }
}

void CameraTestRunner::completeCurrentStep(CameraTest::StepResult result)
{
    m_awaitingUserResponse = false;
    
    switch (result) {
        case CameraTest::StepResult::Pass: m_currentResult.passedSteps++; break;
        case CameraTest::StepResult::Fail: m_currentResult.failedSteps++; break;
        case CameraTest::StepResult::Skip: m_currentResult.skippedSteps++; break;
        default: break;
    }

    emit stepCompleted(m_currentStepIndex, result);
    executeNextStep();
}

void CameraTestRunner::finishTest(CameraTest::TestStatus status)
{
    m_running = false;
    m_paused = false;
    m_currentResult.status = status;
    m_currentResult.endTime = QDateTime::currentDateTime();
    emit statusMessage("Test " + CameraTest::testStatusToString(status).toLower());
}

QString CameraTestRunner::generateReport(const CameraTest::TestResult &result) const
{
    QString report;
    QTextStream ts(&report);

    ts << "═══════════════════════════════════════════════════════════════\n";
    ts << "                    CAMERA TEST REPORT\n";
    ts << "═══════════════════════════════════════════════════════════════\n\n";

    ts << "Test: " << result.testName << "\n";
    ts << "ID: " << result.testId << "\n";
    ts << "Category: " << CameraTest::categoryToString(result.category) << "\n";
    ts << "Status: " << CameraTest::testStatusToString(result.status) << "\n\n";

    ts << "Started: " << result.startTime.toString("yyyy-MM-dd hh:mm:ss") << "\n";
    ts << "Ended: " << result.endTime.toString("yyyy-MM-dd hh:mm:ss") << "\n";
    ts << "Duration: " << result.startTime.secsTo(result.endTime) << " seconds\n\n";

    if (!result.operatorName.isEmpty())
        ts << "Operator: " << result.operatorName << "\n";
    if (!result.deviceSerial.isEmpty())
        ts << "Device Serial: " << result.deviceSerial << "\n";
    ts << "\n";

    ts << "───────────────────────────────────────────────────────────────\n";
    ts << "                         SUMMARY\n";
    ts << "───────────────────────────────────────────────────────────────\n";
    ts << "Total Steps: " << result.totalSteps << "\n";
    ts << "Passed: " << result.passedSteps << "\n";
    ts << "Failed: " << result.failedSteps << "\n";
    ts << "Skipped: " << result.skippedSteps << "\n";
    ts << QString("Pass Rate: %1%\n\n").arg(result.passRate(), 0, 'f', 1);

    ts << "───────────────────────────────────────────────────────────────\n";
    ts << "                      STEP DETAILS\n";
    ts << "───────────────────────────────────────────────────────────────\n";

    for (int i = 0; i < result.stepResults.size(); ++i) {
        const auto &step = result.stepResults[i];
        ts << QString("\n[%1] %2\n").arg(i + 1).arg(step.name);
        ts << "    Result: " << CameraTest::stepResultToString(step.result) << "\n";
        if (!step.notes.isEmpty())
            ts << "    Notes: " << step.notes << "\n";
    }

    ts << "\n═══════════════════════════════════════════════════════════════\n";
    if (!result.notes.isEmpty()) {
        ts << "General Notes:\n" << result.notes << "\n";
    }

    return report;
}

bool CameraTestRunner::saveReportToFile(const CameraTest::TestResult &result, const QString &filepath) const
{
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << generateReport(result);
    file.close();
    return true;
}

QVector<CameraTest::TestDefinition> CameraTestRunner::createBuiltInTests()
{
    using namespace CameraTest;
    QVector<TestDefinition> tests;

    // ═══════════════════════════════════════════════════════════════
    // EXPOSURE CONTROL TEST
    // ═══════════════════════════════════════════════════════════════
    {
        TestDefinition test;
        test.id = "exposure_control";
        test.name = "Exposure Control Test";
        test.description = "Verifies exposure time adjustment affects image brightness";
        test.category = TestCategory::ExposureControl;
        test.requiresStreaming = true;
        test.requiredCamera = "rgb";
        test.estimatedDurationSec = 45;

        TestStep s0;
        s0.name = "Disable Auto Exposure";
        s0.instruction = "Disabling auto exposure for manual control...";
        s0.controlToHighlight = "rgbStreamingAutoExposureCheckBox";
        s0.parameter = "auto_exposure";
        s0.camera = "rgb";
        s0.mode = "streaming";
        s0.targetValue = false;  // <-- DISABLE AUTO
        s0.durationMs = 1000;
        s0.requiresUserVerification = false;
        s0.expectedObservation = "Auto exposure disabled";
        test.steps.append(s0);
        
        TestStep s1;
        s1.name = "Low Exposure";
        s1.instruction = "Image should appear DARKER than normal";
        s1.controlToHighlight = "rgbStreamingExposureSpinBox";
        s1.parameter = "exposure_time_us";
        s1.camera = "rgb";
        s1.mode = "streaming";
        s1.targetValue = 5000;
        s1.durationMs = 3000;
        s1.expectedObservation = "Image darkens noticeably";
        test.steps.append(s1);

        TestStep s2;
        s2.name = "Medium Exposure";
        s2.instruction = "Image should appear at NORMAL brightness";
        s2.controlToHighlight = "rgbStreamingExposureSpinBox";
        s2.parameter = "exposure_time_us";
        s2.camera = "rgb";
        s2.mode = "streaming";
        s2.targetValue = 20000;
        s2.durationMs = 3000;
        s2.expectedObservation = "Image at normal brightness";
        test.steps.append(s2);

        TestStep s3;
        s3.name = "High Exposure";
        s3.instruction = "Image should appear BRIGHTER than normal";
        s3.controlToHighlight = "rgbStreamingExposureSpinBox";
        s3.parameter = "exposure_time_us";
        s3.camera = "rgb";
        s3.mode = "streaming";
        s3.targetValue = 50000;
        s3.durationMs = 3000;
        s3.expectedObservation = "Image brightens noticeably";
        test.steps.append(s3);

        tests.append(test);
    }

    // ═══════════════════════════════════════════════════════════════
    // GAIN CONTROL TEST
    // ═══════════════════════════════════════════════════════════════
    {
        TestDefinition test;
        test.id = "gain_control";
        test.name = "Gain Control Test";
        test.description = "Verifies analog gain adjustment affects image brightness and noise";
        test.category = TestCategory::GainControl;
        test.requiresStreaming = true;
        test.requiredCamera = "rgb";
        test.estimatedDurationSec = 45;

        TestStep s1;
        s1.name = "Low Gain (1.0x)";
        s1.instruction = "Image should be at baseline brightness, minimal noise";
        s1.controlToHighlight = "rgbStreamingAnalogGainSpinBox";
        s1.parameter = "analog_gain";
        s1.camera = "rgb";
        s1.mode = "streaming";
        s1.targetValue = 1.0;
        s1.durationMs = 3000;
        s1.expectedObservation = "Clean image with low noise";
        test.steps.append(s1);

        TestStep s2;
        s2.name = "Medium Gain (4.0x)";
        s2.instruction = "Image should be brighter, possibly some noise visible";
        s2.controlToHighlight = "rgbStreamingAnalogGainSpinBox";
        s2.parameter = "analog_gain";
        s2.camera = "rgb";
        s2.mode = "streaming";
        s2.targetValue = 4.0;
        s2.durationMs = 3000;
        s2.expectedObservation = "Brighter image, slight noise increase";
        test.steps.append(s2);

        TestStep s3;
        s3.name = "High Gain (8.0x)";
        s3.instruction = "Image should be significantly brighter with visible noise";
        s3.controlToHighlight = "rgbStreamingAnalogGainSpinBox";
        s3.parameter = "analog_gain";
        s3.camera = "rgb";
        s3.mode = "streaming";
        s3.targetValue = 8.0;
        s3.durationMs = 3000;
        s3.expectedObservation = "Much brighter, noticeable noise/grain";
        test.steps.append(s3);

        tests.append(test);
    }

    // ═══════════════════════════════════════════════════════════════
    // WHITE BALANCE TEST
    // ═══════════════════════════════════════════════════════════════
    {
        TestDefinition test;
        test.id = "white_balance";
        test.name = "White Balance Test";
        test.description = "Verifies white balance presets affect color temperature";
        test.category = TestCategory::WhiteBalance;
        test.requiresStreaming = true;
        test.requiredCamera = "rgb";
        test.estimatedDurationSec = 60;

        TestStep s1;
        s1.name = "Auto White Balance";
        s1.instruction = "Colors should appear natural and balanced";
        s1.controlToHighlight = "rgbStreamingAutoWBCheckBox";
        s1.parameter = "awb_enable";
        s1.camera = "rgb";
        s1.mode = "streaming";
        s1.targetValue = true;
        s1.durationMs = 3000;
        s1.expectedObservation = "Natural color reproduction";
        test.steps.append(s1);

        TestStep s2;
        s2.name = "Tungsten (Warm)";
        s2.instruction = "Image should have a COOLER/BLUER tint";
        s2.controlToHighlight = "rgbStreamingAWBModeCombo";
        s2.parameter = "awb_mode";
        s2.camera = "rgb";
        s2.mode = "streaming";
        s2.targetValue = 1;  // Tungsten
        s2.durationMs = 3000;
        s2.expectedObservation = "Blue/cool color cast";
        test.steps.append(s2);

        TestStep s3;
        s3.name = "Daylight";
        s3.instruction = "Image should appear natural for daylight conditions";
        s3.controlToHighlight = "rgbStreamingAWBModeCombo";
        s3.parameter = "awb_mode";
        s3.camera = "rgb";
        s3.mode = "streaming";
        s3.targetValue = 4;  // Daylight
        s3.durationMs = 3000;
        s3.expectedObservation = "Neutral/warm daylight colors";
        test.steps.append(s3);

        tests.append(test);
    }

    // ═══════════════════════════════════════════════════════════════
    // RGB CAPTURE TEST
    // ═══════════════════════════════════════════════════════════════
    {
        TestDefinition test;
        test.id = "rgb_capture";
        test.name = "RGB Single Capture Test";
        test.description = "Verifies RGB camera can capture still images at various resolutions";
        test.category = TestCategory::RGBCapture;
        test.requiresStreaming = false;
        test.requiredCamera = "rgb";
        test.estimatedDurationSec = 30;

        TestStep s1;
        s1.name = "Capture Full Resolution";
        s1.instruction = "Click Capture button. Verify image appears in viewer.";
        s1.controlToHighlight = "singleCaptureButton";
        s1.parameter = "";
        s1.camera = "rgb";
        s1.mode = "capture";
        s1.durationMs = 5000;
        s1.expectedObservation = "Full resolution image captured and displayed";
        test.steps.append(s1);

        tests.append(test);
    }

    // ═══════════════════════════════════════════════════════════════
    // THERMAL COLORMAP TEST
    // ═══════════════════════════════════════════════════════════════
    {
        TestDefinition test;
        test.id = "thermal_colormap";
        test.name = "Thermal Colormap Test";
        test.description = "Verifies thermal colormap changes are applied correctly";
        test.category = TestCategory::ColorMap;
        test.requiresStreaming = true;
        test.requiredCamera = "thermal";
        test.estimatedDurationSec = 45;

        TestStep s1;
        s1.name = "Grayscale Colormap";
        s1.instruction = "Thermal image should display in GRAYSCALE";
        s1.controlToHighlight = "thermalColormapCombo";
        s1.parameter = "colormap";
        s1.camera = "thermal";
        s1.mode = "streaming";
        s1.targetValue = 0;  // Grayscale
        s1.durationMs = 3000;
        s1.expectedObservation = "Black and white thermal image";
        test.steps.append(s1);

        TestStep s2;
        s2.name = "Jet Colormap";
        s2.instruction = "Thermal image should display in JET colors (blue-green-red)";
        s2.controlToHighlight = "thermalColormapCombo";
        s2.parameter = "colormap";
        s2.camera = "thermal";
        s2.mode = "streaming";
        s2.targetValue = 2;  // Jet
        s2.durationMs = 3000;
        s2.expectedObservation = "Rainbow colors: cold=blue, hot=red";
        test.steps.append(s2);

        TestStep s3;
        s3.name = "Inferno Colormap";
        s3.instruction = "Thermal image should display in INFERNO colors";
        s3.controlToHighlight = "thermalColormapCombo";
        s3.parameter = "colormap";
        s3.camera = "thermal";
        s3.mode = "streaming";
        s3.targetValue = 9;  // Inferno
        s3.durationMs = 3000;
        s3.expectedObservation = "Black-red-yellow-white gradient";
        test.steps.append(s3);

        tests.append(test);
    }

    // ═══════════════════════════════════════════════════════════════
    // DISTANCE SENSOR TEST
    // ═══════════════════════════════════════════════════════════════
    {
        TestDefinition test;
        test.id = "distance_sensor";
        test.name = "Distance Sensor Test";
        test.description = "Verifies VL53L4CD distance sensor responds to objects";
        test.category = TestCategory::DistanceSensor;
        test.requiresStreaming = false;
        test.requiredCamera = "";
        test.estimatedDurationSec = 30;

        TestStep s1;
        s1.name = "Initialize Sensor";
        s1.instruction = "Click Initialize. Status should show 'Initialized'";
        s1.controlToHighlight = "distanceInitButton";
        s1.parameter = "";
        s1.durationMs = 3000;
        s1.expectedObservation = "Status shows ✓ Initialized";
        test.steps.append(s1);

        TestStep s2;
        s2.name = "Start Streaming";
        s2.instruction = "Click Start Stream. Distance values should update.";
        s2.controlToHighlight = "distanceStartButton";
        s2.parameter = "";
        s2.durationMs = 5000;
        s2.expectedObservation = "Distance display shows changing values";
        test.steps.append(s2);

        TestStep s3;
        s3.name = "Near Object Test";
        s3.instruction = "Place hand ~10cm from sensor. Value should be ~100mm";
        s3.controlToHighlight = "distanceValueLabel";
        s3.parameter = "";
        s3.durationMs = 5000;
        s3.expectedObservation = "Display shows approximately 100mm";
        test.steps.append(s3);

        tests.append(test);
    }

    return tests;
}
