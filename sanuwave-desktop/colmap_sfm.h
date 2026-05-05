#ifndef COLMAP_SFM_H
#define COLMAP_SFM_H

#include <QThread>
#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QString>
#include <QAtomicInt>
#include <memory>

// Forward declarations for COLMAP types
namespace colmap {
    class Database;
    class ReconstructionManager;
}

// Configuration for SfM processing
struct SfMConfig {
    QString imageFolderPath;
    QString outputPath;
    
    // Camera model parameters
    QString cameraModel = "SIMPLE_RADIAL";  // SIMPLE_RADIAL, PINHOLE, etc.
    
    // Feature extraction settings
    int maxImageSize = 3200;
    int maxNumFeatures = 8192;
    
    // Matching settings
    bool exhaustiveMatching = true;  // vs sequential matching
    int vocabTreeMatching = 0;  // 0 = exhaustive, >0 = vocab tree
    
    SfMConfig() = default;
    SfMConfig(const QString& images, const QString& output) 
        : imageFolderPath(images), outputPath(output) {}
};

// Results from SfM processing
struct SfMResults {
    bool success = false;
    QString errorMessage;
    
    int numImages = 0;
    int numRegisteredImages = 0;
    int numPoints3D = 0;
    int numObservations = 0;
    
    QString databasePath;
    QString sparsePath;
};

// Worker thread that performs COLMAP processing
class SfMWorker : public QThread {
    Q_OBJECT
    
public:
    explicit SfMWorker(const SfMConfig& config, QObject* parent = nullptr);
    ~SfMWorker() override;
    
    void cancel();
    const SfMResults& getResults() const { return results_; }
    
signals:
    void progressUpdate(int percentage, const QString& stageName);
    void finished(bool success, const QString& message);
    void stageStarted(const QString& stageName);
    
protected:
    void run() override;
    
private:
    bool createWorkspace();
    bool initializeDatabase();
    bool extractFeatures();
    bool matchFeatures();
    bool incrementalMapping();
    bool saveResults();
    void cleanup();
    
    bool isCancelled() const { return cancelled_.loadAcquire() != 0; }
    
    SfMConfig config_;
    SfMResults results_;
    QAtomicInt cancelled_;
    
    QString workspaceDir_;
    QString databasePath_;
    QString imagesDir_;
    QString sparseDir_;
};

// Progress dialog with cancel button
class SfMProgressDialog : public QDialog {
    Q_OBJECT
    
public:
    explicit SfMProgressDialog(const QString& imageFolder, QWidget* parent = nullptr);
    ~SfMProgressDialog() override;
    
    const SfMResults& getResults() const { return worker_->getResults(); }
    
private slots:
    void onProgressUpdate(int percentage, const QString& stageName);
    void onStageStarted(const QString& stageName);
    void onFinished(bool success, const QString& message);
    void onCancelClicked();
    
private:
    void setupUI();
    void connectSignals();
    
    SfMWorker* worker_;
    QProgressBar* progressBar_;
    QLabel* statusLabel_;
    QLabel* stageLabel_;
    QPushButton* cancelButton_;
    
    bool isFinished_;
};

// Camera model selection dialog
class SfMCameraDialog : public QDialog {
    Q_OBJECT
    
public:
    explicit SfMCameraDialog(QWidget* parent = nullptr);
    
    QString getSelectedCameraModel() const { return selectedModel_; }
    
private slots:
    void onOkClicked();
    void onCancelClicked();
    
private:
    void setupUI();
    
    QComboBox* cameraComboBox_;
    QString selectedModel_;
};

#endif // COLMAP_SFM_H