// settingsdialog.h
#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSettings>

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog();
    
    QString getServerIP() const;
    int getServerPort() const;
    bool getAutoReconnect() const;
    int getConnectionTimeout() const;

public slots:
    void accept() override;
    void reject() override;

private slots:
    void onTestConnection();
    void onRestoreDefaults();

private:
    void setupUI();
    void loadSettings();
    void saveSettings();
    
    QLineEdit* serverIPEdit;
    QSpinBox* serverPortSpinBox;
    QCheckBox* autoReconnectCheckBox;
    QSpinBox* connectionTimeoutSpinBox;
    
    QPushButton* testBtn;
    QPushButton* defaultsBtn;
    QPushButton* okBtn;
    QPushButton* cancelBtn;
    
    QSettings* settings;
    
    // Default values
    static constexpr const char* DEFAULT_SERVER_IP = "192.168.1.100";
    static constexpr int DEFAULT_SERVER_PORT = 8080;
    static constexpr bool DEFAULT_AUTO_RECONNECT = false;
    static constexpr int DEFAULT_CONNECTION_TIMEOUT = 5000;
};

#endif // SETTINGSDIALOG_H