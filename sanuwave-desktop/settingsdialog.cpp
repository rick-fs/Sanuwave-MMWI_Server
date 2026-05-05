// settingsdialog.cpp
#include "settingsdialog.h"
#include "logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , settings(new QSettings("Sanuwave", "SanuwaveClient", this))
{
    setupUI();
    loadSettings();
}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::setupUI()
{
    setWindowTitle("Settings");
    setModal(true);
    resize(500, 250);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Server Settings Group
    QGroupBox* serverGroup = new QGroupBox("Server Connection", this);
    QFormLayout* serverLayout = new QFormLayout(serverGroup);
    
    serverIPEdit = new QLineEdit(this);
    serverIPEdit->setPlaceholderText("e.g., 192.168.1.100 or raspberrypi.local");
    serverLayout->addRow("Server Address:", serverIPEdit);
    
    serverPortSpinBox = new QSpinBox(this);
    serverPortSpinBox->setRange(1, 65535);
    serverPortSpinBox->setValue(DEFAULT_SERVER_PORT);
    serverLayout->addRow("Server Port:", serverPortSpinBox);
    
    // Add test connection button
    testBtn = new QPushButton("Test Connection", this);
    connect(testBtn, &QPushButton::clicked, this, &SettingsDialog::onTestConnection);
    serverLayout->addRow("", testBtn);
    
    mainLayout->addWidget(serverGroup);
    
    // Connection Options Group
    QGroupBox* optionsGroup = new QGroupBox("Connection Options", this);
    QFormLayout* optionsLayout = new QFormLayout(optionsGroup);
    
    autoReconnectCheckBox = new QCheckBox("Enable automatic reconnection", this);
    optionsLayout->addRow("Auto-Reconnect:", autoReconnectCheckBox);
    
    connectionTimeoutSpinBox = new QSpinBox(this);
    connectionTimeoutSpinBox->setRange(1000, 30000);
    connectionTimeoutSpinBox->setSingleStep(1000);
    connectionTimeoutSpinBox->setSuffix(" ms");
    connectionTimeoutSpinBox->setValue(DEFAULT_CONNECTION_TIMEOUT);
    optionsLayout->addRow("Connection Timeout:", connectionTimeoutSpinBox);
    
    mainLayout->addWidget(optionsGroup);
    
    // Info label
    QLabel* infoLabel = new QLabel(
        "💡 <i>You can use IP addresses (192.168.1.100) or hostnames (raspberrypi.local)</i>", 
        this
    );
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);
    
    mainLayout->addStretch();
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    defaultsBtn = new QPushButton("Restore Defaults", this);
    connect(defaultsBtn, &QPushButton::clicked, this, &SettingsDialog::onRestoreDefaults);
    buttonLayout->addWidget(defaultsBtn);
    
    buttonLayout->addStretch();
    
    cancelBtn = new QPushButton("Cancel", this);
    connect(cancelBtn, &QPushButton::clicked, this, &SettingsDialog::reject);
    buttonLayout->addWidget(cancelBtn);
    
    okBtn = new QPushButton("OK", this);
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, &SettingsDialog::accept);
    buttonLayout->addWidget(okBtn);
    
    mainLayout->addLayout(buttonLayout);
}

void SettingsDialog::loadSettings()
{
    QString ip = settings->value("server/ip", DEFAULT_SERVER_IP).toString();
    int port = settings->value("server/port", DEFAULT_SERVER_PORT).toInt();
    bool autoReconnect = settings->value("connection/auto_reconnect", DEFAULT_AUTO_RECONNECT).toBool();
    int timeout = settings->value("connection/timeout", DEFAULT_CONNECTION_TIMEOUT).toInt();
    
    LOG_DEBUG << "Loading settings - IP: " << ip.toStdString() 
              << ", Port: " << port << std::endl;
    
    serverIPEdit->setText(ip);
    serverPortSpinBox->setValue(port);
    autoReconnectCheckBox->setChecked(autoReconnect);
    connectionTimeoutSpinBox->setValue(timeout);
}

void SettingsDialog::saveSettings()
{
    settings->setValue("server/ip", serverIPEdit->text());
    settings->setValue("server/port", serverPortSpinBox->value());
    settings->setValue("connection/auto_reconnect", autoReconnectCheckBox->isChecked());
    settings->setValue("connection/timeout", connectionTimeoutSpinBox->value());
    settings->sync();
    
    LOG_INFO << "Settings saved - Server: " << serverIPEdit->text().toStdString() 
             << ":" << serverPortSpinBox->value() << std::endl;
}

void SettingsDialog::accept()
{
    if (serverIPEdit->text().isEmpty())
    {
        QMessageBox::warning(this, "Invalid Input", "Server address cannot be empty.");
        serverIPEdit->setFocus();
        return;
    }
    
    saveSettings();
    QDialog::accept();
}

void SettingsDialog::reject()
{
    loadSettings(); // Revert changes
    QDialog::reject();
}

void SettingsDialog::onTestConnection()
{
    QString ip = serverIPEdit->text();
    int port = serverPortSpinBox->value();
    
    if (ip.isEmpty())
    {
        QMessageBox::warning(this, "Invalid Input", "Please enter a server address.");
        return;
    }
    
    LOG_INFO << "Testing connection to " << ip.toStdString() << ":" << port << std::endl;
    
    testBtn->setEnabled(false);
    testBtn->setText("Testing...");
    
    QTcpSocket socket;
    socket.connectToHost(ip, port);
    
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(connectionTimeoutSpinBox->value());
    
    connect(&socket, &QTcpSocket::connected, &loop, &QEventLoop::quit);
    connect(&socket, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    
    timer.start();
    loop.exec();
    
    testBtn->setEnabled(true);
    testBtn->setText("Test Connection");
    
    if (socket.state() == QAbstractSocket::ConnectedState)
    {
        LOG_INFO << "Test connection successful to " << ip.toStdString() << ":" << port << std::endl;
        QMessageBox::information(this, "Connection Successful", 
            QString("✔ Successfully connected to %1:%2").arg(ip).arg(port));
        socket.disconnectFromHost();
    }
    else if (timer.isActive())
    {
        LOG_ERROR << "Test connection failed: " << socket.errorString().toStdString() << std::endl;
        QMessageBox::critical(this, "Connection Failed", 
            QString("✗ Failed to connect to %1:%2\n\nError: %3")
                .arg(ip)
                .arg(port)
                .arg(socket.errorString()));
    }
    else
    {
        LOG_WARNING << "Test connection timed out" << std::endl;
        QMessageBox::critical(this, "Connection Timeout", 
            QString("✗ Connection to %1:%2 timed out after %3 ms")
                .arg(ip)
                .arg(port)
                .arg(connectionTimeoutSpinBox->value()));
    }
    
    timer.stop();
}

void SettingsDialog::onRestoreDefaults()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Restore Defaults",
        "Are you sure you want to restore all settings to their default values?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes)
    {
        LOG_INFO << "Restoring default settings" << std::endl;
        
        serverIPEdit->setText(DEFAULT_SERVER_IP);
        serverPortSpinBox->setValue(DEFAULT_SERVER_PORT);
        autoReconnectCheckBox->setChecked(DEFAULT_AUTO_RECONNECT);
        connectionTimeoutSpinBox->setValue(DEFAULT_CONNECTION_TIMEOUT);
    }
}

QString SettingsDialog::getServerIP() const
{
    return serverIPEdit->text();
}

int SettingsDialog::getServerPort() const
{
    return serverPortSpinBox->value();
}

bool SettingsDialog::getAutoReconnect() const
{
    return autoReconnectCheckBox->isChecked();
}

int SettingsDialog::getConnectionTimeout() const
{
    return connectionTimeoutSpinBox->value();
}