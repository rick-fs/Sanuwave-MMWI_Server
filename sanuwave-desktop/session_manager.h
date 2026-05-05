// Copyright 2026 Sanuwave Medical LLC.
//
// session_manager.h
//
// Client-side session lifecycle manager. Owns the current session ID,
// per-camera frame counters, output folder, and file writing.

#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include "server_connection.h"  // CaptureResultInfo, LedStateInfo

class SessionManager : public QObject
{
    Q_OBJECT

public:
    explicit SessionManager(QObject* parent = nullptr);

    // --- Session lifecycle ---

    // Call before firing capture commands. Generates a session_id,
    // creates the session directory, writes session.json.
    // Returns the session_id to embed in capture commands.
    QString beginSession(const QStringList& cameraIds,
                         const QString& captureMode = "single");

    // Call when all captures in a run are complete (optional — session
    // stays valid until beginSession() is called again).
    void endSession();

    // Returns the current session_id, empty if no session active.
    QString currentSessionId() const { return sessionId; }

    bool hasActiveSession() const { return !sessionId.isEmpty(); }

    // --- Output folder ---
    void    setOutputFolder(const QString& folder) { outputFolder = folder; }
    QString getOutputFolder() const { return outputFolder; }

    // --- Frame finalization ---
    // Call from MainWindow::onCaptureComplete().
    // Assigns frame_index, writes JPEG to session dir, writes sidecar JSON.
    // Returns the path the JPEG was written to, or empty on failure.
    QString finalizeFrame(const CaptureResultInfo& info,
                          const QByteArray& jpegData);

signals:
    void sessionStarted(const QString& sessionId, const QString& sessionDir);
    void frameSaved(const QString& filePath, const QString& sidecarPath);
    void sessionError(const QString& message);

private:
    QString buildFrameStem(const CaptureResultInfo& info, uint32_t frameIndex) const;
    QJsonObject buildFrameSidecar(const CaptureResultInfo& info,
                                  uint32_t frameIndex) const;
    bool writeSessionJson() const;

    QString             sessionId;
    QString             outputFolder;
    QString             sessionDir;       // outputFolder/sessions/sessionId/
    QStringList         sessionCameraIds;
    QString             sessionCaptureMode;
    QDateTime           sessionStartTime;
    QMap<QString, uint32_t> frameCounters; // per camera_id
};
