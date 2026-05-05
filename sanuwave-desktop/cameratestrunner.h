// cameratestrunner.h
#ifndef CAMERATESTRUNNER_H
#define CAMERATESTRUNNER_H

#include <QObject>
#include <QTimer>
#include <QMap>
#include <QVariant>
#include "cameratestdefinitions.h"

class CameraTestRunner : public QObject
{
    Q_OBJECT

public:
    explicit CameraTestRunner(QObject *parent = nullptr);
    ~CameraTestRunner();

    // Test library management
    void registerTest(const CameraTest::TestDefinition &test);
    QVector<CameraTest::TestDefinition> getAvailableTests() const;
    QVector<CameraTest::TestDefinition> getTestsByCategory(CameraTest::TestCategory category) const;
    CameraTest::TestDefinition getTestById(const QString &id) const;

    // Test execution
    void startTest(const QString &testId);
    void abortTest();
    void pauseTest();
    void resumeTest();

    // User response for current step
    void recordStepResult(CameraTest::StepResult result, const QString &notes = QString());
    void skipCurrentStep();

    // State queries
    bool isRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }
    int currentStepIndex() const { return m_currentStepIndex; }
    int totalSteps() const { return m_currentTest.steps.size(); }
    CameraTest::TestStep currentStep() const;
    CameraTest::TestResult currentResult() const { return m_currentResult; }

    // Saved parameter values (for restore after test)
    void setSavedValue(const QString &key, const QVariant &value);
    QVariant getSavedValue(const QString &key) const;

    // Report generation
    QString generateReport(const CameraTest::TestResult &result) const;
    bool saveReportToFile(const CameraTest::TestResult &result, const QString &filepath) const;

    // Built-in test definitions
    static QVector<CameraTest::TestDefinition> createBuiltInTests();

signals:
    // Test lifecycle
    void testStarted(const QString &testId, const QString &testName);
    void testCompleted(const CameraTest::TestResult &result);
    void testAborted(const CameraTest::TestResult &partialResult);
    void testPaused();
    void testResumed();

    // Step execution
    void stepStarted(int stepIndex, const CameraTest::TestStep &step);
    void stepCompleted(int stepIndex, CameraTest::StepResult result);
    void stepTimerTick(int remainingMs);
    void awaitingUserResponse(const CameraTest::TestStep &step);

    // Parameter changes (MainWindow should connect to apply these)
    void requestParameterChange(const QString &camera, const QString &parameter, 
                                const QVariant &value, const QString &mode);
    void requestHighlightControl(const QString &controlName, bool highlight);
    void requestStreamStart(const QString &camera);
    void requestStreamStop();

    // Restore original values
    void requestRestoreParameters();

    // Status updates
    void statusMessage(const QString &message);
    void instructionUpdate(const QString &instruction);

private slots:
    void onStepTimerTimeout();
    void onStepDurationTimeout();

private:
    void executeNextStep();
    void executeStep(const CameraTest::TestStep &step);
    void completeCurrentStep(CameraTest::StepResult result);
    void finishTest(CameraTest::TestStatus status);
    void initializeTestLibrary();

    // Test library
    QMap<QString, CameraTest::TestDefinition> m_testLibrary;

    // Current test state
    CameraTest::TestDefinition m_currentTest;
    CameraTest::TestResult m_currentResult;
    int m_currentStepIndex;
    bool m_running;
    bool m_paused;
    bool m_awaitingUserResponse;

    // Timers
    QTimer *m_stepTimer;           // For step duration countdown
    QTimer *m_tickTimer;           // For UI updates (1 second ticks)
    int m_remainingMs;

    // Saved parameter values for restoration
    QMap<QString, QVariant> m_savedValues;
};

#endif // CAMERATESTRUNNER_H
