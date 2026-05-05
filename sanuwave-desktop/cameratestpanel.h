// cameratestpanel.h
#ifndef CAMERATESTPANEL_H
#define CAMERATESTPANEL_H

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include "cameratestrunner.h"
#include "cameratestdefinitions.h"

class CameraTestPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CameraTestPanel(QWidget *parent = nullptr);
    ~CameraTestPanel();

    // Access to the test runner
    CameraTestRunner* testRunner() { return m_testRunner; }

    // Set operator info
    void setOperatorName(const QString &name);
    void setDeviceSerial(const QString &serial);

signals:
    // Forward signals from runner to MainWindow
    void requestParameterChange(const QString &camera, const QString &parameter,
                                const QVariant &value, const QString &mode);
    void requestHighlightControl(const QString &controlName, bool highlight);
    void requestStreamStart(const QString &camera);
    void requestStreamStop();
    void requestRestoreParameters();
    void requestSaveCurrentValues();

    // Test panel specific
    void testStartRequested(const QString &testId);
    void instructionUpdated(const QString &instruction);
    void requestConnectionCheck();
public slots:
    // Called from MainWindow when values are saved
    void onValuesSaved();
    void onConnectionCheckResult(bool connected);
    void onTestFailedToStart(const QString &reason);
private slots:
    void onTestSelectionChanged(int index);
    void onRunTestClicked();
    void onAbortTestClicked();
    void onPauseResumeClicked();
    void onPassClicked();
    void onFailClicked();
    void onSkipClicked();
    void onSaveReportClicked();

    // Slots connected to CameraTestRunner
    void onTestStarted(const QString &testId, const QString &testName);
    void onTestCompleted(const CameraTest::TestResult &result);
    void onTestAborted(const CameraTest::TestResult &partialResult);
    void onTestPaused();
    void onTestResumed();
    void onStepStarted(int stepIndex, const CameraTest::TestStep &step);
    void onStepCompleted(int stepIndex, CameraTest::StepResult result);
    void onStepTimerTick(int remainingMs);
    void onAwaitingUserResponse(const CameraTest::TestStep &step);
    void onStatusMessage(const QString &message);
    void onInstructionUpdate(const QString &instruction);

private:
    void setupUI();
    void populateTestList();
    void updateUIForState();
    void updateResultsTable();
    void addResultRow(int stepIndex, const CameraTest::TestStep &step);
    void updateResultRow(int stepIndex, CameraTest::StepResult result);

    // Test runner
    CameraTestRunner *m_testRunner;

    // Test selection
    QComboBox *m_categoryCombo;
    QComboBox *m_testCombo;
    QLabel *m_testDescriptionLabel;
    QLabel *m_estimatedTimeLabel;

    // Operator info
    QLineEdit *m_operatorNameEdit;
    QLineEdit *m_deviceSerialEdit;

    // Control buttons
    QPushButton *m_runButton;
    QPushButton *m_abortButton;
    QPushButton *m_pauseResumeButton;
    QPushButton *m_saveReportButton;

    // Progress
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QLabel *m_timerLabel;
    QLabel *m_stepLabel;

    // Current step info
    QGroupBox *m_currentStepGroup;
    QLabel *m_stepNameLabel;
    QLabel *m_instructionLabel;
    QLabel *m_expectedLabel;

    // User response buttons
    QGroupBox *m_responseGroup;
    QPushButton *m_passButton;
    QPushButton *m_failButton;
    QPushButton *m_skipButton;
    QLineEdit *m_notesEdit;

    // Results table
    QTableWidget *m_resultsTable;

    // Status log
    QTextEdit *m_logText;

    // Last test result for report saving
    CameraTest::TestResult m_lastResult;
    QString m_pendingTestId;  // Store test ID while waiting for connection check
};

#endif // CAMERATESTPANEL_H
