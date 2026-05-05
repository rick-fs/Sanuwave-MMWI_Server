// Copyright 2026 Sanuwave Medical LLC.

#include "camera_settings_manager.h"
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QDateTime>
#include <QStandardPaths>

CameraSettingsManager::CameraSettingsManager(QObject* parent)
    : QObject(parent)
{
}

void CameraSettingsManager::registerSpinBox(const QString& key, QSpinBox* widget)
{
    if (widget) spinBoxes[key] = widget;
}

void CameraSettingsManager::registerDoubleSpinBox(const QString& key, QDoubleSpinBox* widget)
{
    if (widget) doubleSpinBoxes[key] = widget;
}

void CameraSettingsManager::registerCheckBox(const QString& key, QCheckBox* widget)
{
    if (widget) checkBoxes[key] = widget;
}

void CameraSettingsManager::registerComboBox(const QString& key, QComboBox* widget)
{
    if (widget) comboBoxes[key] = widget;
}

QJsonObject CameraSettingsManager::serializeAll() const
{
    QJsonObject root;
    root["version"] = CURRENT_VERSION;
    root["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["application"] = "SanuwaveClient";

    QJsonObject settings;

    QJsonObject spinBoxData;
    for (auto it = spinBoxes.constBegin(); it != spinBoxes.constEnd(); ++it)
        spinBoxData[it.key()] = it.value()->value();
    settings["spinBoxes"] = spinBoxData;

    QJsonObject doubleSpinBoxData;
    for (auto it = doubleSpinBoxes.constBegin(); it != doubleSpinBoxes.constEnd(); ++it)
        doubleSpinBoxData[it.key()] = it.value()->value();
    settings["doubleSpinBoxes"] = doubleSpinBoxData;

    QJsonObject checkBoxData;
    for (auto it = checkBoxes.constBegin(); it != checkBoxes.constEnd(); ++it)
        checkBoxData[it.key()] = it.value()->isChecked();
    settings["checkBoxes"] = checkBoxData;

    QJsonObject comboBoxData;
    for (auto it = comboBoxes.constBegin(); it != comboBoxes.constEnd(); ++it) {
        QJsonObject combo;
        combo["index"] = it.value()->currentIndex();
        combo["data"] = it.value()->currentData().toString();
        comboBoxData[it.key()] = combo;
    }
    settings["comboBoxes"] = comboBoxData;

    root["settings"] = settings;
    return root;
}

bool CameraSettingsManager::deserializeAll(const QJsonObject& root, QString* errorMsg)
{
    int fileVersion = root["version"].toInt(0);
    if (fileVersion > CURRENT_VERSION) {
        if (errorMsg)
            *errorMsg = QString("Settings file version %1 is newer than supported version %2. "
                               "Please update the application.").arg(fileVersion).arg(CURRENT_VERSION);
        return false;
    }

    if (fileVersion < 1) {
        if (errorMsg)
            *errorMsg = "Invalid or missing version in settings file.";
        return false;
    }

    QJsonObject settings = root["settings"].toObject();

    if (fileVersion < CURRENT_VERSION) {
        if (!migrateSettings(settings, fileVersion, errorMsg))
            return false;
    }

    // Block signals during bulk update to prevent cascading signal handlers
    // We'll emit settingsLoaded() at the end so the UI can update properly
    
    // Set spin boxes
    QJsonObject spinBoxData = settings["spinBoxes"].toObject();
    for (auto it = spinBoxData.constBegin(); it != spinBoxData.constEnd(); ++it) {
        if (spinBoxes.contains(it.key())) {
            QSpinBox* sb = spinBoxes[it.key()];
            sb->blockSignals(true);
            sb->setValue(it.value().toInt());
            sb->blockSignals(false);
        }
    }

    // Set double spin boxes
    QJsonObject doubleSpinBoxData = settings["doubleSpinBoxes"].toObject();
    for (auto it = doubleSpinBoxData.constBegin(); it != doubleSpinBoxData.constEnd(); ++it) {
        if (doubleSpinBoxes.contains(it.key())) {
            QDoubleSpinBox* dsb = doubleSpinBoxes[it.key()];
            dsb->blockSignals(true);
            dsb->setValue(it.value().toDouble());
            dsb->blockSignals(false);
        }
    }

    // Set combo boxes
    QJsonObject comboBoxData = settings["comboBoxes"].toObject();
    for (auto it = comboBoxData.constBegin(); it != comboBoxData.constEnd(); ++it) {
        if (!comboBoxes.contains(it.key()))
            continue;
        
        QComboBox* combo = comboBoxes[it.key()];
        QJsonObject comboInfo = it.value().toObject();
        QString savedData = comboInfo["data"].toString();
        int savedIndex = comboInfo["index"].toInt();

        combo->blockSignals(true);
        int foundIndex = combo->findData(savedData);
        if (foundIndex >= 0)
            combo->setCurrentIndex(foundIndex);
        else if (savedIndex >= 0 && savedIndex < combo->count())
            combo->setCurrentIndex(savedIndex);
        combo->blockSignals(false);
    }

    // Set check boxes LAST - these control enable/disable state of other widgets
    // Don't block signals here so the toggled handlers run and update UI state
    QJsonObject checkBoxData = settings["checkBoxes"].toObject();
    for (auto it = checkBoxData.constBegin(); it != checkBoxData.constEnd(); ++it) {
        if (checkBoxes.contains(it.key())) {
            QCheckBox* cb = checkBoxes[it.key()];
            cb->setChecked(it.value().toBool());
        }
    }

    // Emit signal so MainWindow can do any additional UI updates
    emit settingsLoaded();

    return true;
}

bool CameraSettingsManager::migrateSettings(QJsonObject& settings, int fromVersion, QString* errorMsg)
{
    Q_UNUSED(settings);
    Q_UNUSED(errorMsg);
    Q_UNUSED(fromVersion);
    // Future migration: if (fromVersion < 2) { migrate v1 to v2 }
    return true;
}

bool CameraSettingsManager::saveToFile(const QString& filePath, QString* errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMsg)
            *errorMsg = QString("Could not open file for writing: %1").arg(file.errorString());
        return false;
    }

    QJsonDocument doc(serializeAll());
    qint64 written = file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    if (written < 0) {
        if (errorMsg)
            *errorMsg = QString("Error writing to file: %1").arg(file.errorString());
        return false;
    }
    return true;
}

bool CameraSettingsManager::loadFromFile(const QString& filePath, QString* errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg)
            *errorMsg = QString("Could not open file: %1").arg(file.errorString());
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        if (errorMsg)
            *errorMsg = QString("JSON parse error: %1").arg(parseError.errorString());
        return false;
    }

    if (!doc.isObject()) {
        if (errorMsg)
            *errorMsg = "Invalid settings file format.";
        return false;
    }

    return deserializeAll(doc.object(), errorMsg);
}

QString CameraSettingsManager::getSaveFilePath(QWidget* parent)
{
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultName = QString("sanuwave_settings_%1.json").arg(timestamp);
    
    return QFileDialog::getSaveFileName(parent, "Save Camera Settings",
        defaultDir + "/" + defaultName, "JSON Files (*.json);;All Files (*)");
}

QString CameraSettingsManager::getLoadFilePath(QWidget* parent)
{
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QFileDialog::getOpenFileName(parent, "Load Camera Settings",
        defaultDir, "JSON Files (*.json);;All Files (*)");
}
