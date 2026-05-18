// Copyright 2026 Sanuwave Medical LLC.
// 
// Portions of this code were generated with Claude.ai and
// reviewed and edited.


// main.cpp
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include "mainwindow.h"
#include "logger.h"

#include "sensor_calibration.h"
#include <QCoreApplication>
#include <QDir>

#define NO_TIMESTAMP

#ifdef ENABLE_TESTS
void testNewlineBugFix();
#endif

// Initialize sensor calibration store
void initializeSensorCalibration()
{
    auto& store = sanuwave::SensorCalibrationStore::instance();
    
    // First, set up hardcoded fallback values
    store.initializeDefaults();
    
    if (store.loadFromTuningFile("imx708", ":/calibration/imx708.json")) 
    {
        qDebug() << "Loaded IMX708 calibration from resources";
    }
    if (store.loadFromTuningFile("imx219", ":/calibration/imx219.json")) 
    {
        qDebug() << "Loaded IMX219 calibration from resources";
    }
}




int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    initializeSensorCalibration(); 
    app.setApplicationName("SanuwaveClient");
    app.setOrganizationName("Sanuwave");
    app.setApplicationVersion(VERSION_STRING);
    app.setApplicationDisplayName("Sanuwave Medical Imaging");    
    // Get platform-appropriate log directory
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir logDir(logPath);
    if (!logDir.exists()) 
    {
        logDir.mkpath(".");
    }
    
    // Initialize logger before anything else
    Logger::getInstance().setLogLevel(LogLevel::DEBUG);
#ifdef NO_TIMESTAMP
    Logger::getInstance().setLogFileWithNoTimestamp(logPath.toStdString(), "sanuwave_client",std::ios_base::trunc);
#else 
    Logger::getInstance().setLogFileWithTimestamp(logPath.toStdString(), "sanuwave_client");
#endif
    Logger::getInstance().enableConsoleOutput(true);
    
    LOG_INFO << "=== Sanuwave Medical Imaging Client v1.0.0 ===" << std::endl;
    LOG_INFO << "Application starting..." << std::endl;
    LOG_INFO << "Log file location: " << logPath.toStdString() << std::endl;
    std::cout << "Log file location: " << logPath.toStdString() << std::endl;


    #ifdef ENABLE_TESTS
        testNewlineBugFix();
        return 0;
    #endif

    MainWindow window;
    window.show();
    
    LOG_INFO << "Main window displayed" << std::endl;
    
    int result = app.exec();
    
    LOG_INFO << "Application exiting with code: " << result << std::endl;
    
    return result;
}