#pragma once
// Copyright 2026 Sanuwave Medical LLC.
// 
// Portions of this code were generated with Claude.ai and
// reviewed and edited.
//


#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() = default;

private:
    void setupUI();
    QString getVersionString() const;
    QString getBuildInfo() const;
};
