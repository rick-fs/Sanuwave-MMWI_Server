// cameratestpanel.cpp
#include "cameratestpanel.h"
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QScrollArea>

CameraTestPanel::CameraTestPanel(QWidget *parent)
    : QWidget(parent)
{
    m_testRunner = new CameraTestRunner(this);
    setupUI();
    populateTestList();

    // Connect runner signals
    connect(m_testRunner, &CameraTestRunner::testStarted, this, &CameraTestPanel::onTestStarted);
    connect(m_testRunner, &CameraTestRunner::testCompleted, this, &CameraTestPanel::onTestCompleted);
    connect(m_testRunner, &CameraTestRunner::testAborted, this, &CameraTestPanel::onTestAborted);
    connect(m_testRunner, &CameraTestRunner::testPaused, this, &CameraTestPanel::onTestPaused);
    connect(m_testRunner, &CameraTestRunner::testResumed, this, &CameraTestPanel::onTestResumed);
    connect(m_testRunner, &CameraTestRunner::stepStarted, this, &CameraTestPanel::onStepStarted);
    connect(m_testRunner, &CameraTestRunner::stepCompleted, this, &CameraTestPanel::onStepCompleted);
    connect(m_testRunner, &CameraTestRunner::stepTimerTick, this, &CameraTestPanel::onStepTimerTick);
    connect(m_testRunner, &CameraTestRunner::awaitingUserResponse, this, &CameraTestPanel::onAwaitingUserResponse);
    connect(m_testRunner, &CameraTestRunner::statusMessage, this, &CameraTestPanel::onStatusMessage);
    connect(m_testRunner, &CameraTestRunner::instructionUpdate, this, &CameraTestPanel::onInstructionUpdate);

    // Forward runner signals to MainWindow
    connect(m_testRunner, &CameraTestRunner::requestParameterChange,
            this, &CameraTestPanel::requestParameterChange);
    connect(m_testRunner, &CameraTestRunner::requestHighlightControl,
            this, &CameraTestPanel::requestHighlightControl);
    connect(m_testRunner, &CameraTestRunner::requestStreamStart,
            this, &CameraTestPanel::requestStreamStart);
    connect(m_testRunner, &CameraTestRunner::requestStreamStop,
            this, &CameraTestPanel::requestStreamStop);
    connect(m_testRunner, &CameraTestRunner::requestRestoreParameters,
            this, &CameraTestPanel::requestRestoreParameters);

    updateUIForState();
}

CameraTestPanel::~CameraTestPanel()
{
}

void CameraTestPanel::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // TEST SELECTION GROUP
    QGroupBox *selectionGroup = new QGroupBox("Test Selection");
    QVBoxLayout *selLayout = new QVBoxLayout(selectionGroup);

    QHBoxLayout *comboLayout = new QHBoxLayout();
    comboLayout->addWidget(new QLabel("Category:"));
    m_categoryCombo = new QComboBox();
    m_categoryCombo->addItem("All Tests", -1);
    m_categoryCombo->addItem("Exposure Control", (int)CameraTest::TestCategory::ExposureControl);
    m_categoryCombo->addItem("Gain Control", (int)CameraTest::TestCategory::GainControl);
    m_categoryCombo->addItem("White Balance", (int)CameraTest::TestCategory::WhiteBalance);
    m_categoryCombo->addItem("RGB Capture", (int)CameraTest::TestCategory::RGBCapture);
    m_categoryCombo->addItem("Thermal", (int)CameraTest::TestCategory::ColorMap);
    m_categoryCombo->addItem("Distance Sensor", (int)CameraTest::TestCategory::DistanceSensor);
    comboLayout->addWidget(m_categoryCombo);

    comboLayout->addSpacing(20);
    comboLayout->addWidget(new QLabel("Test:"));
    m_testCombo = new QComboBox();
    m_testCombo->setMinimumWidth(200);
    comboLayout->addWidget(m_testCombo);
    comboLayout->addStretch();
    selLayout->addLayout(comboLayout);

    m_testDescriptionLabel = new QLabel();
    m_testDescriptionLabel->setWordWrap(true);
    m_testDescriptionLabel->setStyleSheet("color: #666; font-style: italic;");
    selLayout->addWidget(m_testDescriptionLabel);

    QHBoxLayout *infoLayout = new QHBoxLayout();
    m_estimatedTimeLabel = new QLabel("Estimated time: --");
    infoLayout->addWidget(m_estimatedTimeLabel);
    infoLayout->addStretch();

    infoLayout->addWidget(new QLabel("Operator:"));
    m_operatorNameEdit = new QLineEdit();
    m_operatorNameEdit->setPlaceholderText("Name");
    m_operatorNameEdit->setMaximumWidth(120);
    infoLayout->addWidget(m_operatorNameEdit);

    infoLayout->addWidget(new QLabel("Device:"));
    m_deviceSerialEdit = new QLineEdit();
    m_deviceSerialEdit->setPlaceholderText("Serial #");
    m_deviceSerialEdit->setMaximumWidth(120);
    infoLayout->addWidget(m_deviceSerialEdit);
    selLayout->addLayout(infoLayout);

    mainLayout->addWidget(selectionGroup);

    // CONTROL BUTTONS
    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_runButton = new QPushButton("▶ Run Test");
    m_runButton->setMinimumHeight(36);
    btnLayout->addWidget(m_runButton);

    m_pauseResumeButton = new QPushButton("⏸ Pause");
    m_pauseResumeButton->setMinimumHeight(36);
    m_pauseResumeButton->setEnabled(false);
    btnLayout->addWidget(m_pauseResumeButton);

    m_abortButton = new QPushButton("⏹ Abort");
    m_abortButton->setMinimumHeight(36);
    m_abortButton->setEnabled(false);
    btnLayout->addWidget(m_abortButton);

    btnLayout->addStretch();

    m_saveReportButton = new QPushButton("💾 Save Report");
    m_saveReportButton->setMinimumHeight(36);
    m_saveReportButton->setEnabled(false);
    btnLayout->addWidget(m_saveReportButton);

    mainLayout->addLayout(btnLayout);

    // PROGRESS
    QHBoxLayout *progressLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Ready");
    progressLayout->addWidget(m_statusLabel);

    m_stepLabel = new QLabel("Step: --/--");
    progressLayout->addWidget(m_stepLabel);

    m_timerLabel = new QLabel("⏱ --");
    m_timerLabel->setMinimumWidth(60);
    progressLayout->addWidget(m_timerLabel);

    progressLayout->addStretch();
    mainLayout->addLayout(progressLayout);

    m_progressBar = new QProgressBar();
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat("%v / %m steps");
    mainLayout->addWidget(m_progressBar);

    // CURRENT STEP INFO
    m_currentStepGroup = new QGroupBox("Current Step");
    QVBoxLayout *stepLayout = new QVBoxLayout(m_currentStepGroup);

    m_stepNameLabel = new QLabel("--");
    m_stepNameLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
    stepLayout->addWidget(m_stepNameLabel);

    m_instructionLabel = new QLabel("--");
    m_instructionLabel->setWordWrap(true);
    m_instructionLabel->setStyleSheet("font-size: 16px; color: #1976D2; padding: 10px; background: #E3F2FD; border-radius: 4px;");
    m_instructionLabel->setMinimumHeight(60);
    stepLayout->addWidget(m_instructionLabel);

    QHBoxLayout *expectedLayout = new QHBoxLayout();
    expectedLayout->addWidget(new QLabel("Expected:"));
    m_expectedLabel = new QLabel("--");
    m_expectedLabel->setStyleSheet("font-style: italic; color: #666;");
    expectedLayout->addWidget(m_expectedLabel);
    expectedLayout->addStretch();
    stepLayout->addLayout(expectedLayout);

    mainLayout->addWidget(m_currentStepGroup);

    // USER RESPONSE
    m_responseGroup = new QGroupBox("Verify Result");
    QHBoxLayout *respLayout = new QHBoxLayout(m_responseGroup);

    m_passButton = new QPushButton("✓ Pass");
    m_passButton->setMinimumSize(80, 40);
    m_passButton->setEnabled(false);
    respLayout->addWidget(m_passButton);

    m_failButton = new QPushButton("✗ Fail");
    m_failButton->setMinimumSize(80, 40);
    m_failButton->setEnabled(false);
    respLayout->addWidget(m_failButton);

    m_skipButton = new QPushButton("⊘ Skip");
    m_skipButton->setMinimumSize(80, 40);
    m_skipButton->setEnabled(false);
    respLayout->addWidget(m_skipButton);

    respLayout->addSpacing(20);

    mainLayout->addWidget(m_responseGroup);

    // RESULTS TABLE
    QGroupBox *resultsGroup = new QGroupBox("Results");
    QVBoxLayout *resultsLayout = new QVBoxLayout(resultsGroup);

    m_resultsTable = new QTableWidget();
    m_resultsTable->setColumnCount(4);
    m_resultsTable->setHorizontalHeaderLabels({"#", "Step Name", "Result", "Notes"});
    m_resultsTable->horizontalHeader()->setStretchLastSection(true);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_resultsTable->setColumnWidth(0, 30);
    m_resultsTable->setColumnWidth(2, 80);
    m_resultsTable->setMaximumHeight(150);
    m_resultsTable->setAlternatingRowColors(true);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsLayout->addWidget(m_resultsTable);

    mainLayout->addWidget(resultsGroup);

    // STATUS LOG
    QGroupBox *logGroup = new QGroupBox("Log");
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    m_logText = new QTextEdit();
    m_logText->setReadOnly(true);
    m_logText->setMaximumHeight(100);
    m_logText->setStyleSheet("font-family: monospace; font-size: 11px;");
    logLayout->addWidget(m_logText);
    mainLayout->addWidget(logGroup);

    // CONNECTIONS
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraTestPanel::populateTestList);
    connect(m_testCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraTestPanel::onTestSelectionChanged);
    connect(m_runButton, &QPushButton::clicked, this, &CameraTestPanel::onRunTestClicked);
    connect(m_abortButton, &QPushButton::clicked, this, &CameraTestPanel::onAbortTestClicked);
    connect(m_pauseResumeButton, &QPushButton::clicked, this, &CameraTestPanel::onPauseResumeClicked);
    connect(m_passButton, &QPushButton::clicked, this, &CameraTestPanel::onPassClicked);
    connect(m_failButton, &QPushButton::clicked, this, &CameraTestPanel::onFailClicked);
    connect(m_skipButton, &QPushButton::clicked, this, &CameraTestPanel::onSkipClicked);
    connect(m_saveReportButton, &QPushButton::clicked, this, &CameraTestPanel::onSaveReportClicked);
}

void CameraTestPanel::populateTestList()
{
    m_testCombo->clear();

    int catIndex = m_categoryCombo->currentData().toInt();
    QVector<CameraTest::TestDefinition> tests;

    if (catIndex < 0) {
        tests = m_testRunner->getAvailableTests();
    } else {
        tests = m_testRunner->getTestsByCategory(static_cast<CameraTest::TestCategory>(catIndex));
    }

    for (const auto &test : tests) {
        m_testCombo->addItem(test.name, test.id);
    }

    if (m_testCombo->count() > 0) {
        onTestSelectionChanged(0);
    }
}

void CameraTestPanel::onTestSelectionChanged(int index)
{
    if (index < 0) return;

    QString testId = m_testCombo->currentData().toString();
    auto test = m_testRunner->getTestById(testId);

    m_testDescriptionLabel->setText(test.description);
    m_estimatedTimeLabel->setText(QString("Estimated time: %1 sec").arg(test.estimatedDurationSec));
}

void CameraTestPanel::onRunTestClicked()
{
    QString testId = m_testCombo->currentData().toString();
    if (testId.isEmpty()) return;

    m_pendingTestId = testId;
    emit requestConnectionCheck();
}

void CameraTestPanel::onConnectionCheckResult(bool connected)
{
    if (!connected) {
        onTestFailedToStart("Not connected to server");
        m_pendingTestId.clear();
        return;
    }
    
    if (m_pendingTestId.isEmpty()) return;

    emit requestSaveCurrentValues();

    m_resultsTable->setRowCount(0);
    m_logText->clear();

    m_testRunner->startTest(m_pendingTestId);
    m_pendingTestId.clear();
}

void CameraTestPanel::onTestFailedToStart(const QString &reason)
{
    m_statusLabel->setText("Failed to start: " + reason);
    m_statusLabel->setStyleSheet("font-weight: bold; color: #f44336;");
    
    QString log = QString("[%1] Test failed to start: %2")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(reason);
    m_logText->append(log);
    
    // Reset UI to ready state
    m_stepLabel->setText("Step: --/--");
    m_progressBar->setValue(0);
    m_stepNameLabel->setText("--");
    m_instructionLabel->setText("--");
    m_expectedLabel->setText("--");
    m_timerLabel->setText("⏱ --");
    
    updateUIForState();
}

void CameraTestPanel::setOperatorName(const QString &name)
{
    m_operatorNameEdit->setText(name);
}

void CameraTestPanel::setDeviceSerial(const QString &serial)
{
    m_deviceSerialEdit->setText(serial);
}


void CameraTestPanel::onValuesSaved()
{
}

void CameraTestPanel::onAbortTestClicked()
{
    m_testRunner->abortTest();
}

void CameraTestPanel::onPauseResumeClicked()
{
    if (m_testRunner->isPaused()) {
        m_testRunner->resumeTest();
    } else {
        m_testRunner->pauseTest();
    }
}

void CameraTestPanel::onPassClicked()
{
    m_testRunner->recordStepResult(CameraTest::StepResult::Pass, tr("Passed"));
}

void CameraTestPanel::onFailClicked()
{
    m_testRunner->recordStepResult(CameraTest::StepResult::Fail, tr("Failed"));
}

void CameraTestPanel::onSkipClicked()
{
    m_testRunner->skipCurrentStep();
}

void CameraTestPanel::onSaveReportClicked()
{
    QString filename = QFileDialog::getSaveFileName(this, "Save Test Report",
        QString("test_report_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        "Text Files (*.txt);;All Files (*)");

    if (!filename.isEmpty()) {
        if (m_testRunner->saveReportToFile(m_lastResult, filename)) {
            QMessageBox::information(this, "Report Saved", "Test report saved successfully.");
        } else {
            QMessageBox::warning(this, "Save Failed", "Failed to save test report.");
        }
    }
}

void CameraTestPanel::onTestStarted(const QString &testId, const QString &testName)
{
    m_statusLabel->setText("Running: " + testName);
    m_statusLabel->setStyleSheet("font-weight: bold; color: #1976D2;");

    auto test = m_testRunner->getTestById(testId);
    m_progressBar->setMaximum(test.steps.size());
    m_progressBar->setValue(0);

    updateUIForState();

    QString log = QString("[%1] Test started: %2")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(testName);
    m_logText->append(log);
}

void CameraTestPanel::onTestCompleted(const CameraTest::TestResult &result)
{
    m_lastResult = result;
    m_lastResult.operatorName = m_operatorNameEdit->text();
    m_lastResult.deviceSerial = m_deviceSerialEdit->text();

    QString status = QString("Completed - Pass: %1, Fail: %2, Skip: %3 (%.1f%%)")
        .arg(result.passedSteps)
        .arg(result.failedSteps)
        .arg(result.skippedSteps)
        .arg(result.passRate());

    m_statusLabel->setText(status);

    if (result.failedSteps == 0) {
        m_statusLabel->setStyleSheet("font-weight: bold; color: #4CAF50;");
    } else {
        m_statusLabel->setStyleSheet("font-weight: bold; color: #f44336;");
    }

    updateUIForState();
    m_saveReportButton->setEnabled(true);

    QString log = QString("[%1] Test completed - %2")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(status);
    m_logText->append(log);
}

void CameraTestPanel::onTestAborted(const CameraTest::TestResult &partialResult)
{
    m_lastResult = partialResult;
    m_statusLabel->setText("Aborted");
    m_statusLabel->setStyleSheet("font-weight: bold; color: #FF9800;");
    updateUIForState();

    QString log = QString("[%1] Test aborted")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"));
    m_logText->append(log);
}

void CameraTestPanel::onTestPaused()
{
    m_statusLabel->setText("Paused");
    m_statusLabel->setStyleSheet("font-weight: bold; color: #FF9800;");
    m_pauseResumeButton->setText("▶ Resume");
}

void CameraTestPanel::onTestResumed()
{
    m_statusLabel->setText("Running");
    m_statusLabel->setStyleSheet("font-weight: bold; color: #1976D2;");
    m_pauseResumeButton->setText("⏸ Pause");
}

void CameraTestPanel::onStepStarted(int stepIndex, const CameraTest::TestStep &step)
{
    m_stepLabel->setText(QString("Step: %1/%2").arg(stepIndex + 1).arg(m_testRunner->totalSteps()));
    m_progressBar->setValue(stepIndex);

    m_stepNameLabel->setText(step.name);
    m_instructionLabel->setText(step.instruction);
    m_expectedLabel->setText(step.expectedObservation);

    m_timerLabel->setText(QString("⏱ %1s").arg(step.durationMs / 1000));

    addResultRow(stepIndex, step);

    QString log = QString("[%1] Step %2: %3")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(stepIndex + 1)
        .arg(step.name);
    m_logText->append(log);
}

void CameraTestPanel::onStepCompleted(int stepIndex, CameraTest::StepResult result)
{
    updateResultRow(stepIndex, result);
    m_progressBar->setValue(stepIndex + 1);

    QString log = QString("[%1] Step %2 result: %3")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(stepIndex + 1)
        .arg(CameraTest::stepResultToString(result));
    m_logText->append(log);
}

void CameraTestPanel::onStepTimerTick(int remainingMs)
{
    m_timerLabel->setText(QString("⏱ %1s").arg(remainingMs / 1000));
}

void CameraTestPanel::onAwaitingUserResponse(const CameraTest::TestStep &step)
{
    m_passButton->setEnabled(true);
    m_failButton->setEnabled(true);
    m_skipButton->setEnabled(true);
    m_timerLabel->setText("⏱ Waiting...");

    m_instructionLabel->setStyleSheet("font-size: 16px; color: #FF6F00; padding: 10px; background: #FFF3E0; border-radius: 4px; border: 2px solid #FF9800;");
}

void CameraTestPanel::onStatusMessage(const QString &message)
{
    QString log = QString("[%1] %2")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(message);
    m_logText->append(log);
}

void CameraTestPanel::onInstructionUpdate(const QString &instruction)
{
    m_instructionLabel->setText(instruction);
    emit instructionUpdated(instruction);
}

void CameraTestPanel::updateUIForState()
{
    bool running = m_testRunner->isRunning();
    bool paused = m_testRunner->isPaused();

    m_runButton->setEnabled(!running);
    m_abortButton->setEnabled(running);
    m_pauseResumeButton->setEnabled(running);
    m_pauseResumeButton->setText(paused ? "▶ Resume" : "⏸ Pause");

    m_categoryCombo->setEnabled(!running);
    m_testCombo->setEnabled(!running);

    if (!running) {
        m_passButton->setEnabled(false);
        m_failButton->setEnabled(false);
        m_skipButton->setEnabled(false);
        m_instructionLabel->setStyleSheet("font-size: 16px; color: #1976D2; padding: 10px; background: #E3F2FD; border-radius: 4px;");
    }
}

void CameraTestPanel::addResultRow(int stepIndex, const CameraTest::TestStep &step)
{
    int row = m_resultsTable->rowCount();
    m_resultsTable->insertRow(row);

    m_resultsTable->setItem(row, 0, new QTableWidgetItem(QString::number(stepIndex + 1)));
    m_resultsTable->setItem(row, 1, new QTableWidgetItem(step.name));
    
    QTableWidgetItem *resultItem = new QTableWidgetItem("...");
    resultItem->setTextAlignment(Qt::AlignCenter);
    m_resultsTable->setItem(row, 2, resultItem);
    
    m_resultsTable->setItem(row, 3, new QTableWidgetItem(""));

    m_resultsTable->scrollToBottom();
}

void CameraTestPanel::updateResultRow(int stepIndex, CameraTest::StepResult result)
{
    if (stepIndex >= m_resultsTable->rowCount()) return;

    QTableWidgetItem *resultItem = m_resultsTable->item(stepIndex, 2);
    if (resultItem) {
        resultItem->setText(CameraTest::stepResultToString(result));

        switch (result) {
            case CameraTest::StepResult::Pass:
                resultItem->setBackground(QColor("#C8E6C9"));
                resultItem->setForeground(QColor("#2E7D32"));
                break;
            case CameraTest::StepResult::Fail:
                resultItem->setBackground(QColor("#FFCDD2"));
                resultItem->setForeground(QColor("#C62828"));
                break;
            case CameraTest::StepResult::Skip:
                resultItem->setBackground(QColor("#FFE0B2"));
                resultItem->setForeground(QColor("#E65100"));
                break;
            default:
                break;
        }
    }

    // Update notes from current result
    auto currentResult = m_testRunner->currentResult();
    if (stepIndex < currentResult.stepResults.size()) {
        QTableWidgetItem *notesItem = m_resultsTable->item(stepIndex, 3);
        if (notesItem) {
            notesItem->setText(currentResult.stepResults[stepIndex].notes);
        }
    }
}
