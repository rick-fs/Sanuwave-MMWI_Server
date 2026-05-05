#ifndef CAMERA_SETTINGS_MANAGER_H
#define CAMERA_SETTINGS_MANAGER_H

// Copyright 2026 Sanuwave Medical LLC.

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QWidget>
#include <QMap>

class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;

class CameraSettingsManager : public QObject
{
    Q_OBJECT

public:
    static constexpr int CURRENT_VERSION = 1;

    explicit CameraSettingsManager(QObject* parent = nullptr);

    void registerSpinBox(const QString& key, QSpinBox* widget);
    void registerDoubleSpinBox(const QString& key, QDoubleSpinBox* widget);
    void registerCheckBox(const QString& key, QCheckBox* widget);
    void registerComboBox(const QString& key, QComboBox* widget);

    bool saveToFile(const QString& filePath, QString* errorMsg = nullptr);
    bool loadFromFile(const QString& filePath, QString* errorMsg = nullptr);

    static QString getSaveFilePath(QWidget* parent);
    static QString getLoadFilePath(QWidget* parent);
    static int getCurrentVersion() { return CURRENT_VERSION; }

signals:
    // Emitted after settings are loaded so UI can update dependent states
    void settingsLoaded();

private:
    QJsonObject serializeAll() const;
    bool deserializeAll(const QJsonObject& root, QString* errorMsg);
    bool migrateSettings(QJsonObject& settings, int fromVersion, QString* errorMsg);

    QMap<QString, QSpinBox*> spinBoxes;
    QMap<QString, QDoubleSpinBox*> doubleSpinBoxes;
    QMap<QString, QCheckBox*> checkBoxes;
    QMap<QString, QComboBox*> comboBoxes;
};

#endif // CAMERA_SETTINGS_MANAGER_H
