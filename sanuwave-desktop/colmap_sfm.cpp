#include "colmap_sfm.h"
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStandardPaths>
#include <QComboBox>
#include <unordered_set>
#include <algorithm>

// COLMAP includes
#include <colmap/scene/database.h>
#include <colmap/scene/database_cache.h>
#include <colmap/scene/camera.h>
#include <colmap/scene/reconstruction.h>
#include <colmap/scene/reconstruction_manager.h>
#include <colmap/sfm/incremental_mapper.h>
#include <colmap/controllers/feature_extraction.h>
#include <colmap/controllers/feature_matching.h>
#include <colmap/controllers/automatic_reconstruction.h>
#include <colmap/feature/sift.h>
#include <colmap/feature/types.h>
#include <memory>

// Undefine glog macros that conflict with our logger
#ifdef LOG
#undef LOG
#endif
#ifdef LOG_INFO
#undef LOG_INFO
#endif
#ifdef LOG_WARNING
#undef LOG_WARNING
#endif
#ifdef LOG_ERROR
#undef LOG_ERROR
#endif
#ifdef LOG_CRITICAL
#undef LOG_CRITICAL
#endif

// Now include our logger
#include "logger.h"

// ============================================================================
// SfMWorker Implementation
// ============================================================================

SfMWorker::SfMWorker(const SfMConfig& config, QObject* parent)
    : QThread(parent)
    , config_(config)
    , cancelled_(0)
{
}

SfMWorker::~SfMWorker() {
    cancel();
    wait();
}

void SfMWorker::cancel() {
    cancelled_.storeRelease(1);
}

void SfMWorker::run() {
    try {
        emit stageStarted("Initializing workspace...");
        emit progressUpdate(0, "Creating directories");
        
        if (!createWorkspace()) {
            emit finished(false, "Failed to create workspace directory");
            return;
        }
        
        if (isCancelled()) {
            emit finished(false, "Cancelled by user");
            return;
        }
        
        emit stageStarted("Initializing database...");
        emit progressUpdate(5, "Setting up database");
        
        if (!initializeDatabase()) {
            emit finished(false, "Failed to initialize database");
            return;
        }
        
        if (isCancelled()) {
            emit finished(false, "Cancelled by user");
            return;
        }
        
        emit stageStarted("Extracting features...");
        emit progressUpdate(10, "Extracting SIFT features from images");
        
        if (!extractFeatures()) {
            emit finished(false, "Failed to extract features");
            return;
        }
        
        if (isCancelled()) {
            emit finished(false, "Cancelled by user");
            return;
        }
        
        emit stageStarted("Matching features...");
        emit progressUpdate(40, "Matching features between images");
        
        if (!matchFeatures()) {
            emit finished(false, "Failed to match features");
            return;
        }
        
        if (isCancelled()) {
            emit finished(false, "Cancelled by user");
            return;
        }
        
        emit stageStarted("Reconstructing 3D structure...");
        emit progressUpdate(60, "Running incremental mapping");
        
        if (!incrementalMapping()) {
            emit finished(false, "Failed to reconstruct 3D structure");
            return;
        }
        
        if (isCancelled()) {
            emit finished(false, "Cancelled by user");
            return;
        }
        
        emit progressUpdate(95, "Saving results");
        
        if (!saveResults()) {
            emit finished(false, "Failed to save results");
            return;
        }
        
        emit progressUpdate(100, "Complete");
        
        QString successMsg = QString("Successfully reconstructed scene:\n"
                                     "Registered %1/%2 images\n"
                                     "%3 3D points\n"
                                     "%4 observations")
                                .arg(results_.numRegisteredImages)
                                .arg(results_.numImages)
                                .arg(results_.numPoints3D)
                                .arg(results_.numObservations);
        
        emit finished(true, successMsg);
        
    } catch (const std::exception& e) {
        emit finished(false, QString("Exception: %1").arg(e.what()));
    }
}

bool SfMWorker::createWorkspace() {
    // Create output directory structure
    QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QString baseDir = homeDir + "/StructureFromMotion";
    
    // Create timestamped project directory
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QDir inputDir(config_.imageFolderPath);
    QString projectName = inputDir.dirName() + "_" + timestamp;
    
    workspaceDir_ = baseDir + "/" + projectName;
    
    QDir dir;
    if (!dir.mkpath(workspaceDir_)) {
        LOG_CRITICAL << "Failed to create workspace directory: " << workspaceDir_.toStdString() << std::endl;
        return false;
    }
    
    // Create subdirectories
    imagesDir_ = workspaceDir_ + "/images";
    sparseDir_ = workspaceDir_ + "/sparse/0";
    databasePath_ = workspaceDir_ + "/database.db";
    
    if (!dir.mkpath(imagesDir_) || !dir.mkpath(sparseDir_)) {
        LOG_CRITICAL << "Failed to create subdirectories" << std::endl;
        return false;
    }
    
    // Copy or symlink images
    QDir sourceDir(config_.imageFolderPath);
    QStringList filters;
    filters << "*.png" << "*.PNG" << "*.jpg" << "*.JPG" << "*.jpeg" << "*.JPEG";
    QFileInfoList imageFiles = sourceDir.entryInfoList(filters, QDir::Files);
    
    results_.numImages = imageFiles.size();
    
    if (imageFiles.isEmpty()) {
        LOG_CRITICAL << "No images found in " << config_.imageFolderPath.toStdString() << std::endl;
        return false;
    }
    
    for (const QFileInfo& fileInfo : imageFiles) {
        QString destPath = imagesDir_ + "/" + fileInfo.fileName();
        QFile::link(fileInfo.absoluteFilePath(), destPath);
    }
    
    LOG_INFO << "Workspace created: " << workspaceDir_.toStdString() << std::endl;
    LOG_INFO << "Found " << results_.numImages << " images" << std::endl;
    
    return true;
}

bool SfMWorker::initializeDatabase() {
    try {
        // Delete existing database if it exists
        QFile dbFile(databasePath_);
        if (dbFile.exists()) {
            LOG_INFO << "Deleting existing database: " << databasePath_.toStdString() << std::endl;
            dbFile.remove();
        }
        
        // Create database
        colmap::Database database(databasePath_.toStdString());
        
        LOG_INFO << "Database initialized: " << databasePath_.toStdString() << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        LOG_CRITICAL << "Database initialization failed: " << e.what() << std::endl;
        return false;
    }
}

bool SfMWorker::extractFeatures() {
    try {
        colmap::ImageReaderOptions reader_options;
        reader_options.image_path = imagesDir_.toStdString();
        reader_options.single_camera = false;
        reader_options.camera_model = config_.cameraModel.toStdString();
        
        colmap::SiftExtractionOptions sift_options;
        // Match COLMAP GUI parameters exactly
        sift_options.max_image_size = 3200;
        sift_options.max_num_features = 8192;
        sift_options.first_octave = -1;
        sift_options.num_octaves = 4;
        sift_options.octave_resolution = 3;
        sift_options.peak_threshold = 0.00667;
        sift_options.edge_threshold = 10.0;
        sift_options.estimate_affine_shape = false;
        sift_options.max_num_orientations = 2;
        sift_options.upright = false;
        sift_options.domain_size_pooling = false;
        
        LOG_INFO << "Starting feature extraction..." << std::endl;
        LOG_INFO << "  Camera model: " << config_.cameraModel.toStdString() << std::endl;
        LOG_INFO << "  Max image size: " << sift_options.max_image_size << std::endl;
        LOG_INFO << "  Max features per image: " << sift_options.max_num_features << std::endl;
        LOG_INFO << "  First octave: " << sift_options.first_octave << std::endl;
        LOG_INFO << "  Num octaves: " << sift_options.num_octaves << std::endl;
        LOG_INFO << "  Peak threshold: " << sift_options.peak_threshold << std::endl;
        
        // Use the factory function to create controller
        auto extractor = colmap::CreateFeatureExtractorController(
            databasePath_.toStdString(),
            reader_options,
            sift_options
        );
        
        extractor->Start();
        extractor->Wait();
        
        LOG_INFO << "Feature extraction complete" << std::endl;
        
        // Check how many features were extracted - open new database connection
        {
            colmap::Database database(databasePath_.toStdString());
            const auto images = database.ReadAllImages();
            LOG_INFO << "  Processed " << images.size() << " images" << std::endl;
            
            int total_features = 0;
            for (const auto& image : images) {
                const auto keypoints = database.ReadKeypoints(image.ImageId());
                total_features += keypoints.size();
                LOG_INFO << "  Image " << image.ImageId() << " (" << image.Name() << "): " 
                         << keypoints.size() << " features" << std::endl;
            }
            LOG_INFO << "  Total features extracted: " << total_features << std::endl;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_CRITICAL << "Feature extraction failed: " << e.what() << std::endl;
        return false;
    }
}

bool SfMWorker::matchFeatures() {
    try {
        // Exhaustive matching options
        colmap::ExhaustiveMatchingOptions exhaustive_options;
        exhaustive_options.block_size = 50;
        
        // General matching options - match COLMAP GUI exactly
        colmap::SiftMatchingOptions matching_options;
        matching_options.num_threads = -1;
        matching_options.use_gpu = true;
        matching_options.gpu_index = "-1";
        matching_options.max_ratio = 0.80;
        matching_options.max_distance = 0.70;
        matching_options.cross_check = true;
        matching_options.max_num_matches = 32768;
        matching_options.guided_matching = false;
        
        // Two-view geometry options
        colmap::TwoViewGeometryOptions geometry_options;
        geometry_options.min_num_inliers = 15;
        geometry_options.multiple_models = false;
        // RANSAC options (these are what the GUI "General Options" actually set)
        geometry_options.ransac_options.max_error = 4.0;
        geometry_options.ransac_options.confidence = 0.99900;
        geometry_options.ransac_options.max_num_trials = 10000;
        geometry_options.ransac_options.min_inlier_ratio = 0.250;
        
        LOG_INFO << "Starting feature matching (Exhaustive)..." << std::endl;
        LOG_INFO << "  Block size: " << exhaustive_options.block_size << std::endl;
        LOG_INFO << "  Max ratio: " << matching_options.max_ratio << std::endl;
        LOG_INFO << "  Max distance: " << matching_options.max_distance << std::endl;
        LOG_INFO << "  Cross check: " << (matching_options.cross_check ? "yes" : "no") << std::endl;
        LOG_INFO << "  Max num matches: " << matching_options.max_num_matches << std::endl;
        LOG_INFO << "  RANSAC max error: " << geometry_options.ransac_options.max_error << std::endl;
        LOG_INFO << "  RANSAC confidence: " << geometry_options.ransac_options.confidence << std::endl;
        
        // Create the exhaustive matcher with correct parameter order
        auto matcher = colmap::CreateExhaustiveFeatureMatcher(
            exhaustive_options,
            matching_options,
            geometry_options,
            databasePath_.toStdString()
        );
        
        matcher->Start();
        matcher->Wait();
        
        // Check how many matches were found
        colmap::Database database(databasePath_.toStdString());
        const auto num_matches = database.NumMatches();
        const auto num_inlier_matches = database.NumInlierMatches();
        
        LOG_INFO << "Feature matching complete" << std::endl;
        LOG_INFO << "  Total matches: " << num_matches << std::endl;
        LOG_INFO << "  Inlier matches: " << num_inlier_matches << std::endl;
        
        if (num_inlier_matches == 0) {
            LOG_WARNING << "WARNING: No inlier matches found! Images may not overlap or lack features." << std::endl;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_CRITICAL << "Feature matching failed: " << e.what() << std::endl;
        return false;
    }
}

bool SfMWorker::incrementalMapping() {
    try {
        // Create database cache
        auto database_cache = std::make_shared<colmap::DatabaseCache>();
        
        // Load database into cache
        colmap::Database database(databasePath_.toStdString());
        std::unordered_set<std::string> image_names;  // Empty set = load all images
        database_cache->Load(database, 0, false, image_names);
        
        LOG_INFO << "Loaded " << database_cache->NumImages() << " images from database" << std::endl;
        
        // Create reconstruction
        auto reconstruction = std::make_shared<colmap::Reconstruction>();
        
        // Create mapper options
        colmap::IncrementalMapper::Options mapper_options;
        
        // Create mapper
        colmap::IncrementalMapper mapper(database_cache);
        
        // Begin reconstruction
        mapper.BeginReconstruction(reconstruction);
        
        LOG_INFO << "Finding initial image pair..." << std::endl;
        
        // Find good initial image pair
        colmap::image_t image_id1, image_id2;
        colmap::Rigid3d two_view_geometry;
        const bool find_init_success = mapper.FindInitialImagePair(
            mapper_options, image_id1, image_id2, two_view_geometry);
        
        if (!find_init_success) {
            LOG_CRITICAL << "Could not find good initial image pair" << std::endl;
            return false;
        }
        
        LOG_INFO << "Registering initial image pair #" << image_id1 << " and #" << image_id2 << std::endl;
        
        // Register initial pair
        mapper.RegisterInitialImagePair(mapper_options, image_id1, image_id2, two_view_geometry);
        
        // Triangulate points for initial pair
        colmap::IncrementalTriangulator::Options tri_options;
        LOG_INFO << "Triangulating initial pair..." << std::endl;
        size_t num_tris = mapper.TriangulateImage(tri_options, image_id1);
        LOG_INFO << "  Triangulated " << num_tris << " points for image " << image_id1 << std::endl;
        num_tris = mapper.TriangulateImage(tri_options, image_id2);
        LOG_INFO << "  Triangulated " << num_tris << " points for image " << image_id2 << std::endl;
        
        LOG_INFO << "Global bundle adjustment" << std::endl;
        mapper.AdjustGlobalBundle(mapper_options, colmap::BundleAdjustmentOptions());
        
        LOG_INFO << "Registered " << reconstruction->NumRegImages() << " images, " 
                 << reconstruction->NumPoints3D() << " points" << std::endl;
        
        if (reconstruction->NumPoints3D() == 0) {
            LOG_CRITICAL << "Initial pair created 0 3D points!" << std::endl;
            return false;
        }
        
        // Incrementally register remaining images
        int num_reg_images_per_ba = 0;
        const int kMaxNumRegImagesPerBA = 10;  // Do BA every 10 images like GUI
        
        LOG_INFO << "Starting incremental registration of remaining images..." << std::endl;
        LOG_INFO << "Total images in database: " << database_cache->NumImages() << std::endl;
        LOG_INFO << "Currently registered: " << reconstruction->NumRegImages() << std::endl;
        
        // Debug: check which images are actually registered
        int num_already_registered = 0;
        for (const auto& image : database_cache->Images()) {
            if (reconstruction->ExistsImage(image.first)) {
                const auto& img = reconstruction->Image(image.first);
                if (img.HasPose()) {
                    LOG_INFO << "Image #" << image.first << " is already registered" << std::endl;
                    num_already_registered++;
                }
            }
        }
        LOG_INFO << "Already registered: " << num_already_registered << " images" << std::endl;
        
        bool registered_any = true;
        int pass = 0;
        while (registered_any) {
            registered_any = false;
            pass++;
            LOG_INFO << "Registration pass " << pass << std::endl;
            
            // Try to find and register next images using mapper's selection
            auto next_images = mapper.FindNextImages(mapper_options);
            
            if (next_images.empty()) {
                LOG_INFO << "No more images found to register" << std::endl;
                break;
            }
            
            LOG_INFO << "Found " << next_images.size() << " candidate images for registration" << std::endl;
            
            for (const auto& image_id : next_images) {
                if (isCancelled()) {
                    LOG_WARNING << "Mapping cancelled" << std::endl;
                    return false;
                }
                
                // Skip if already registered (has a valid pose)
                if (reconstruction->ExistsImage(image_id)) {
                    const auto& img = reconstruction->Image(image_id);
                    if (img.HasPose()) {
                        continue;
                    }
                }
                
                LOG_INFO << "Trying to register image #" << image_id << "..." << std::endl;
                
                if (mapper.RegisterNextImage(mapper_options, image_id)) {
                    const auto& reg_image = reconstruction->Image(image_id);
                    LOG_INFO << "Successfully registered image #" << image_id 
                             << " (total registered: " << reconstruction->NumRegImages() << ")" << std::endl;
                    LOG_INFO << "=> Image sees " << reg_image.NumPoints3D() 
                             << " / " << reconstruction->NumPoints3D() << " points" << std::endl;
                    
                    registered_any = true;
                    num_reg_images_per_ba++;
                    
                    // Retriangulate and bundle adjustment periodically
                    if (num_reg_images_per_ba >= kMaxNumRegImagesPerBA) {
                        LOG_INFO << "Retriangulation and Global bundle adjustment" << std::endl;
                        mapper.Retriangulate(tri_options);
                        mapper.AdjustGlobalBundle(mapper_options, colmap::BundleAdjustmentOptions());
                        num_reg_images_per_ba = 0;
                    }
                } else {
                    LOG_INFO << "Failed to register image #" << image_id << std::endl;
                }
            }
            
            LOG_INFO << "Pass " << pass << " complete. Registered any: " << registered_any << std::endl;
            
            // Safety limit: avoid infinite loops
            if (pass > 100) {
                LOG_WARNING << "Reached maximum number of passes (100), stopping" << std::endl;
                break;
            }
        }
        
        // Final retriangulation and bundle adjustment
        if (num_reg_images_per_ba > 0) {
            LOG_INFO << "Final retriangulation and Global bundle adjustment" << std::endl;
            mapper.Retriangulate(tri_options);
            mapper.AdjustGlobalBundle(mapper_options, colmap::BundleAdjustmentOptions());
        }
        
        // End reconstruction
        mapper.EndReconstruction(false);
        
        // Get results
        results_.numRegisteredImages = reconstruction->NumRegImages();
        results_.numPoints3D = reconstruction->NumPoints3D();
        
        // Calculate observations
        results_.numObservations = 0;
        for (const auto& point3D : reconstruction->Points3D()) {
            results_.numObservations += point3D.second.track.Length();
        }
        
        LOG_INFO << "Final reconstruction:" << std::endl;
        LOG_INFO << "  Registered: " << results_.numRegisteredImages << " images" << std::endl;
        LOG_INFO << "  3D points: " << results_.numPoints3D << std::endl;
        LOG_INFO << "  Observations: " << results_.numObservations << std::endl;
        
        // Write reconstruction
        reconstruction->Write(sparseDir_.toStdString());
        
        LOG_INFO << "Reconstruction saved to: " << sparseDir_.toStdString() << std::endl;
        
        return results_.numRegisteredImages > 0;
        
    } catch (const std::exception& e) {
        LOG_CRITICAL << "Incremental mapping failed: " << e.what() << std::endl;
        return false;
    }
}

bool SfMWorker::saveResults() {
    results_.success = true;
    results_.databasePath = databasePath_;
    results_.sparsePath = sparseDir_;
    
    LOG_INFO << "Results saved to: " << workspaceDir_.toStdString() << std::endl;
    return true;
}

void SfMWorker::cleanup() {
    // Optional: cleanup temporary files if needed
}

// ============================================================================
// SfMCameraDialog Implementation
// ============================================================================

SfMCameraDialog::SfMCameraDialog(QWidget* parent)
    : QDialog(parent)
    , selectedModel_("SIMPLE_RADIAL")
{
    setupUI();
}

void SfMCameraDialog::setupUI() {
    setWindowTitle("Select Camera Model");
    setModal(true);
    setMinimumWidth(400);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Description label
    QLabel* descLabel = new QLabel(
        "Select the camera model for Structure from Motion reconstruction:", this);
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);
    
    mainLayout->addSpacing(10);
    
    // Camera model selection
    QHBoxLayout* comboLayout = new QHBoxLayout();
    QLabel* modelLabel = new QLabel("Camera Model:", this);
    comboLayout->addWidget(modelLabel);
    
    cameraComboBox_ = new QComboBox(this);
    cameraComboBox_->addItem("SIMPLE_RADIAL (Default - 1 focal length, 1 radial distortion)", "SIMPLE_RADIAL");
    cameraComboBox_->addItem("PINHOLE (No distortion - for calibrated/rectified images)", "PINHOLE");
    cameraComboBox_->addItem("SIMPLE_PINHOLE (1 focal length, no distortion)", "SIMPLE_PINHOLE");
    cameraComboBox_->addItem("RADIAL (1 focal length, 2 radial distortion)", "RADIAL");
    cameraComboBox_->addItem("OPENCV (Full OpenCV distortion model)", "OPENCV");
    cameraComboBox_->addItem("OPENCV_FISHEYE (Fisheye distortion model)", "OPENCV_FISHEYE");
    cameraComboBox_->setCurrentIndex(0);
    comboLayout->addWidget(cameraComboBox_, 1);
    
    mainLayout->addLayout(comboLayout);
    
    mainLayout->addSpacing(10);
    
    // Info text
    QLabel* infoLabel = new QLabel(
        "<b>Recommendations:</b><br>"
        "• <b>SIMPLE_RADIAL</b>: Best for most cameras (default)<br>"
        "• <b>PINHOLE</b>: Use if images are already undistorted<br>"
        "• <b>OPENCV</b>: Use if you need full distortion correction<br>"
        "• <b>OPENCV_FISHEYE</b>: Use for wide-angle/fisheye lenses",
        this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 10px; border-radius: 5px; }");
    mainLayout->addWidget(infoLabel);
    
    mainLayout->addSpacing(20);
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    QPushButton* okBtn = new QPushButton("OK", this);
    okBtn->setDefault(true);
    
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(okBtn);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect buttons
    connect(okBtn, &QPushButton::clicked, this, &SfMCameraDialog::onOkClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &SfMCameraDialog::onCancelClicked);
}

void SfMCameraDialog::onOkClicked() {
    selectedModel_ = cameraComboBox_->currentData().toString();
    accept();
}

void SfMCameraDialog::onCancelClicked() {
    reject();
}

// ============================================================================
// SfMProgressDialog Implementation
// ============================================================================

SfMProgressDialog::SfMProgressDialog(const QString& imageFolder, QWidget* parent)
    : QDialog(parent)
    , worker_(nullptr)
    , isFinished_(false)
{
    setupUI();
    
    // Show camera selection dialog first
    SfMCameraDialog cameraDialog(this);
    if (cameraDialog.exec() != QDialog::Accepted) {
        // User cancelled camera selection
        reject();
        return;
    }
    
    // Create worker with config
    SfMConfig config;
    config.imageFolderPath = imageFolder;
    config.cameraModel = cameraDialog.getSelectedCameraModel();
    
    worker_ = new SfMWorker(config, this);
    
    connectSignals();
    
    // Start processing
    worker_->start();
}

SfMProgressDialog::~SfMProgressDialog() {
    if (worker_) {
        worker_->cancel();
        worker_->wait();
    }
}

void SfMProgressDialog::setupUI() {
    setWindowTitle("Structure from Motion Processing");
    setModal(true);
    setMinimumWidth(500);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Stage label
    stageLabel_ = new QLabel("Initializing...", this);
    QFont font = stageLabel_->font();
    font.setBold(true);
    stageLabel_->setFont(font);
    mainLayout->addWidget(stageLabel_);
    
    // Status label
    statusLabel_ = new QLabel("Starting processing...", this);
    mainLayout->addWidget(statusLabel_);
    
    // Progress bar
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    mainLayout->addWidget(progressBar_);
    
    // Cancel button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    cancelButton_ = new QPushButton("Cancel", this);
    buttonLayout->addWidget(cancelButton_);
    
    mainLayout->addLayout(buttonLayout);
}

void SfMProgressDialog::connectSignals() {
    connect(worker_, &SfMWorker::progressUpdate,
            this, &SfMProgressDialog::onProgressUpdate);
    
    connect(worker_, &SfMWorker::stageStarted,
            this, &SfMProgressDialog::onStageStarted);
    
    connect(worker_, &SfMWorker::finished,
            this, &SfMProgressDialog::onFinished);
    
    connect(cancelButton_, &QPushButton::clicked,
            this, &SfMProgressDialog::onCancelClicked);
}

void SfMProgressDialog::onProgressUpdate(int percentage, const QString& stageName) {
    progressBar_->setValue(percentage);
    statusLabel_->setText(stageName);
}

void SfMProgressDialog::onStageStarted(const QString& stageName) {
    stageLabel_->setText(stageName);
}

void SfMProgressDialog::onFinished(bool success, const QString& message) {
    isFinished_ = true;
    
    cancelButton_->setText("Close");
    cancelButton_->setEnabled(true);
    
    if (success) {
        progressBar_->setValue(100);
        stageLabel_->setText("Complete!");
        statusLabel_->setText("Reconstruction successful");
        
        QMessageBox::information(this, "Success", message);
    } else {
        stageLabel_->setText("Failed");
        statusLabel_->setText(message);
        
        QMessageBox::critical(this, "Error", message);
    }
    
    accept();
}

void SfMProgressDialog::onCancelClicked() {
    if (isFinished_) {
        accept();
    } else {
        if (QMessageBox::question(this, "Cancel", 
                                  "Are you sure you want to cancel?",
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            cancelButton_->setEnabled(false);
            stageLabel_->setText("Cancelling...");
            worker_->cancel();
        }
    }
}