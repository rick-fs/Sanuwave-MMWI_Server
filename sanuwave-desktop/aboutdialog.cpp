// Copyright 2026 Sanuwave Medical LLC.
// 
// Portions of this code were generated with Claude.ai and
// reviewed and edited.
//


#include "aboutdialog.h"
#include "version.h"
#include <QApplication>
#include <QFont>
#include <QFrame>
#include <QDateTime>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("About Sanuwave Client");
    setFixedSize(450, 350);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setupUI();
}

void AboutDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(25, 25, 25, 25);

    // Application title
    QLabel *titleLabel = new QLabel("Sanuwave Medical Imaging Client");
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    // Version info - use VERSION_DISPLAY for cleaner look
    QLabel *versionLabel = new QLabel(Version::displayVersion());
    versionLabel->setAlignment(Qt::AlignCenter);
    QFont versionFont = versionLabel->font();
    versionFont.setPointSize(11);
    versionLabel->setFont(versionFont);
    mainLayout->addWidget(versionLabel);

    // Separator
    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line);

    // Build info (platform-specific details)
    QLabel *buildLabel = new QLabel(getBuildInfo());
    buildLabel->setAlignment(Qt::AlignCenter);
    buildLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    buildLabel->setWordWrap(true);
    QFont buildFont = buildLabel->font();
    buildFont.setFamily("Consolas, Monaco, monospace");
    buildFont.setPointSize(9);
    buildLabel->setFont(buildFont);
    buildLabel->setStyleSheet("color: #666;");
    mainLayout->addWidget(buildLabel);

    mainLayout->addStretch();

    // Copyright
    QLabel *copyrightLabel = new QLabel("Copyright © 2025 Sanuwave\nAll rights reserved.");
    copyrightLabel->setAlignment(Qt::AlignCenter);
    copyrightLabel->setStyleSheet("color: #888;");
    mainLayout->addWidget(copyrightLabel);

    // OK button
    QPushButton *okButton = new QPushButton("OK");
    okButton->setFixedWidth(100);
    okButton->setDefault(true);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

QString AboutDialog::getBuildInfo() const
{
    QStringList info;
    
    // Full version string with git hash
    info << QString("Version: %1").arg(Version::fullVersion());
    
    // Branch info
    QString branch = Version::branch();
    if (!branch.isEmpty() && branch != "unknown") {
        info << QString("Branch: %1").arg(branch);
    }
    
    // Git commit
    QString gitHash = Version::gitHashFull();
    if (!gitHash.isEmpty() && gitHash != "unknown") {
        info << QString("Commit: %1").arg(gitHash.left(12));
    }
    
    // Build number (commit count)
    int buildNum = Version::commitCount();
    if (buildNum > 0) {
        info << QString("Build: %1").arg(buildNum);
    }
    
    // Check for dirty build
    QString versionStr = Version::fullVersion();
    if (versionStr.contains("-dirty")) {
        info << "Status: Development (uncommitted changes)";
    }

    // Platform
#ifdef Q_OS_WIN
    info << "Platform: Windows";
#elif defined(Q_OS_LINUX)
    info << "Platform: Linux";
#elif defined(Q_OS_MACOS)
    info << "Platform: macOS";
#else
    info << "Platform: Unknown";
#endif

    // Qt version
    info << QString("Qt: %1").arg(QT_VERSION_STR);
    
    // Compiler info
#if defined(__clang__)
    info << QString("Compiler: Clang %1.%2.%3")
        .arg(__clang_major__)
        .arg(__clang_minor__)
        .arg(__clang_patchlevel__);
#elif defined(__GNUC__)
    info << QString("Compiler: GCC %1.%2.%3")
        .arg(__GNUC__)
        .arg(__GNUC_MINOR__)
        .arg(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    info << QString("Compiler: MSVC %1").arg(_MSC_VER);
#endif

    // Build date/time
    info << QString("Built: %1 %2").arg(__DATE__).arg(__TIME__);

    return info.join("\n");
}
