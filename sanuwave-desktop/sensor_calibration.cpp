// Copyright 2026 Sanuwave Medical LLC.
//
// Portions of this code were generated with Claude.ai and
// reviewed and edited.

#include "sensor_calibration.h"
#include "logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>

namespace sanuwave
{

    // sRGB to XYZ D65 matrix (for CCM conversion)
    static const double kSrgbToXyz[9] = {
        0.4124564, 0.3575761, 0.1804375,
        0.2126729, 0.7151522, 0.0721750,
        0.0193339, 0.1191920, 0.9503041};

    // Matrix multiplication: C = A * B (3x3)
    static void matMul3x3(const double *A, const double *B, double *C)
    {
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                C[i * 3 + j] = 0;
                for (int k = 0; k < 3; ++k)
                {
                    C[i * 3 + j] += A[i * 3 + k] * B[k * 3 + j];
                }
            }
        }
    }

    // Matrix inversion (3x3)
    static bool matInv3x3(const double *M, double *inv)
    {
        double det = M[0] * (M[4] * M[8] - M[5] * M[7]) - M[1] * (M[3] * M[8] - M[5] * M[6]) + M[2] * (M[3] * M[7] - M[4] * M[6]);

        if (std::abs(det) < 1e-10)
            return false;

        double invDet = 1.0 / det;
        inv[0] = (M[4] * M[8] - M[5] * M[7]) * invDet;
        inv[1] = (M[2] * M[7] - M[1] * M[8]) * invDet;
        inv[2] = (M[1] * M[5] - M[2] * M[4]) * invDet;
        inv[3] = (M[5] * M[6] - M[3] * M[8]) * invDet;
        inv[4] = (M[0] * M[8] - M[2] * M[6]) * invDet;
        inv[5] = (M[2] * M[3] - M[0] * M[5]) * invDet;
        inv[6] = (M[3] * M[7] - M[4] * M[6]) * invDet;
        inv[7] = (M[1] * M[6] - M[0] * M[7]) * invDet;
        inv[8] = (M[0] * M[4] - M[1] * M[3]) * invDet;
        return true;
    }

    SensorCalibrationStore &SensorCalibrationStore::instance()
    {
        static SensorCalibrationStore store;
        return store;
    }

    void SensorCalibrationStore::initializeDefaults()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // IMX708 fallback (approximate D65 values)
        SensorCalibration imx708;
        imx708.sensorModel = "imx708";
        imx708.cameraModel = "Sony IMX708";
        imx708.blackLevel = 64;
        imx708.whiteLevel = 1023;
        imx708.bitsPerSample = 10;
        imx708.cfaPattern = {0, 1, 1, 2}; // RGGB
        imx708.d65.colorTemp = 6500;
        imx708.d65.colorMatrix = {
            1.5546, -0.5640, -0.1256,
            -0.2489, 1.3432, 0.0259,
            -0.0311, -0.2571, 1.0424};
        imx708.d65.valid = true;
        imx708.neutralR = 0.5;
        imx708.neutralB = 0.5;
        calibrations_["imx708"] = imx708;

        // IMX219 fallback
        SensorCalibration imx219;
        imx219.sensorModel = "imx219";
        imx219.cameraModel = "Sony IMX219";
        imx219.blackLevel = 64;
        imx219.whiteLevel = 1023;
        imx219.bitsPerSample = 10;
        imx219.cfaPattern = {0, 1, 1, 2}; // RGGB
        imx219.d65.colorTemp = 6500;
        imx219.d65.colorMatrix = {
            1.6243, -0.6003, -0.1535,
            -0.2297, 1.3622, -0.0156,
            0.0137, -0.3528, 1.1655};
        imx219.d65.valid = true;
        imx219.neutralR = 0.5;
        imx219.neutralB = 0.5;
        calibrations_["imx219"] = imx219;

        LOG_INFO << "SensorCalibrationStore: initialized with fallback defaults" << std::endl;
    }

    bool SensorCalibrationStore::loadFromTuningFile(const std::string &sensorModel,
                                                    const QString &jsonPath)
    {
        QFile file(jsonPath);
        if (!file.open(QIODevice::ReadOnly))
        {
            LOG_WARNING << "Cannot open tuning file: " << jsonPath.toStdString() << std::endl;
            return false;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();

        if (parseError.error != QJsonParseError::NoError)
        {
            LOG_ERROR << "JSON parse error in " << jsonPath.toStdString()
                      << ": " << parseError.errorString().toStdString() << std::endl;
            return false;
        }

        QJsonObject root = doc.object();

        // Build a map from algorithm name to its config object
        // The structure is: { "algorithms": [ {"rpi.black_level": {...}}, {"rpi.ccm": {...}}, ... ] }
        QMap<QString, QJsonObject> algoMap;
        if (root.contains("algorithms"))
        {
            QJsonArray algorithms = root["algorithms"].toArray();
            for (const auto &algoVal : algorithms)
            {
                QJsonObject algoObj = algoVal.toObject();
                // Each object has exactly one key (the algorithm name)
                for (const QString &key : algoObj.keys())
                {
                    algoMap[key] = algoObj[key].toObject();
                }
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Start with existing calibration (fallback) or create new
        SensorCalibration cal;
        if (calibrations_.count(sensorModel))
        {
            cal = calibrations_[sensorModel];
        }
        else
        {
            cal.sensorModel = sensorModel;
            cal.cameraModel = (sensorModel == "imx708") ? "Sony IMX708" : "Sony IMX219";
        }

        // Parse black level
        if (algoMap.contains("rpi.black_level"))
        {
            parseBlackLevel(algoMap["rpi.black_level"], cal);
        }

        // Parse CCM (color correction matrices)
        if (algoMap.contains("rpi.ccm"))
        {
            parseCCMSection(algoMap["rpi.ccm"], cal);
        }

        // Parse AWB for neutral point / white balance info
        if (algoMap.contains("rpi.awb"))
        {
            QJsonObject awb = algoMap["rpi.awb"];
            if (awb.contains("ct_curve"))
            {
                QJsonArray ctCurve = awb["ct_curve"].toArray();
                // Format: [ct1, r_gain1, b_gain1, ct2, r_gain2, b_gain2, ...]
                // Find entry closest to 6500K (D65)
                double bestDist = 1e9;
                for (int i = 0; i + 2 < ctCurve.size(); i += 3)
                {
                    double ct = ctCurve[i].toDouble();
                    double dist = std::abs(ct - 6500.0);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        cal.neutralR = ctCurve[i + 1].toDouble();
                        cal.neutralB = ctCurve[i + 2].toDouble();
                    }
                }
                LOG_DEBUG << "  AWB gains for D65: R=" << cal.neutralR
                          << " B=" << cal.neutralB << std::endl;
            }
        }

        calibrations_[sensorModel] = cal;

        LOG_INFO << "Loaded calibration for " << sensorModel
                 << " from " << jsonPath.toStdString()
                 << " (D65 valid: " << (cal.d65.valid ? "yes" : "no") << ")" << std::endl;

        return cal.d65.valid;
    }

    bool SensorCalibrationStore::parseBlackLevel(const QJsonObject &blObj,
                                                 SensorCalibration &cal)
    {
        if (blObj.contains("black_level"))
        {
            // RPi tuning uses 16-bit scaled value, we need 10-bit
            int bl16 = blObj["black_level"].toInt();
            cal.blackLevel = static_cast<uint16_t>(bl16 >> 6); // 16-bit to 10-bit
            return true;
        }
        return false;
    }

    bool SensorCalibrationStore::parseCCMSection(const QJsonObject &ccmObj,
                                                 SensorCalibration &cal)
    {
        if (!ccmObj.contains("ccms"))
            return false;

        QJsonArray ccms = ccmObj["ccms"].toArray();

        for (const auto &entry : ccms)
        {
            QJsonObject ccmEntry = entry.toObject();
            int ct = ccmEntry["ct"].toInt();
            QJsonArray matrix = ccmEntry["ccm"].toArray();

            if (matrix.size() != 9)
                continue;

            std::array<double, 9> ccm;
            for (int i = 0; i < 9; ++i)
            {
                ccm[i] = matrix[i].toDouble();
            }

            ColorCalibration *target = nullptr;

            if (ct >= 5500 && ct <= 7500)
            {
                target = &cal.d65;
            }
            else if (ct >= 3500 && ct <= 4500)
            {
                target = &cal.tl84;
            }
            else if (ct >= 2500 && ct <= 3200)
            {
                target = &cal.incandescent;
            }

            if (target)
            {
                target->colorTemp = ct;
                target->ccm = ccm;
                target->colorMatrix = ccmToColorMatrix(ccm);
                target->valid = true;

                LOG_DEBUG << "  Parsed CCM at " << ct << "K" << std::endl;
            }
        }

        return cal.d65.valid;
    }

    std::array<double, 9> SensorCalibrationStore::ccmToColorMatrix(
        const std::array<double, 9> &ccm)
    {
        // CCM is camera RGB -> sRGB
        // ColorMatrix1 for DNG is XYZ -> camera RGB
        //
        // We need: ColorMatrix1 = inv(CCM) * inv(sRGB_to_XYZ)
        //        = inv(CCM * sRGB_to_XYZ)
        //        = inv(camera_to_XYZ)

        // First: camera_to_sRGB * sRGB_to_XYZ = camera_to_XYZ
        double cameraToXyz[9];
        matMul3x3(ccm.data(), kSrgbToXyz, cameraToXyz);

        // Then invert to get XYZ_to_camera
        std::array<double, 9> result;
        if (!matInv3x3(cameraToXyz, result.data()))
        {
            // Fallback to identity if inversion fails
            result = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        }

        return result;
    }

    bool SensorCalibrationStore::loadFromServerJson(const std::string &sensorModel,
                                                    const QJsonObject &json)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        SensorCalibration cal;
        cal.sensorModel = sensorModel;
        cal.cameraModel = json["camera_model"].toString().toStdString();
        cal.blackLevel = static_cast<uint16_t>(json["black_level"].toInt(64));
        cal.whiteLevel = static_cast<uint16_t>(json["white_level"].toInt(1023));
        cal.bitsPerSample = static_cast<uint16_t>(json["bits_per_sample"].toInt(10));

        // CFA pattern
        if (json.contains("cfa_pattern"))
        {
            QJsonArray cfa = json["cfa_pattern"].toArray();
            if (cfa.size() == 4)
            {
                for (int i = 0; i < 4; ++i)
                {
                    cal.cfaPattern[i] = static_cast<uint8_t>(cfa[i].toInt());
                }
            }
        }

        // Color matrix
        if (json.contains("color_matrix_d65"))
        {
            QJsonArray cm = json["color_matrix_d65"].toArray();
            if (cm.size() == 9)
            {
                for (int i = 0; i < 9; ++i)
                {
                    cal.d65.colorMatrix[i] = cm[i].toDouble();
                }
                cal.d65.colorTemp = 6500;
                cal.d65.valid = true;
            }
        }

        calibrations_[sensorModel] = cal;

        LOG_INFO << "Loaded calibration for " << sensorModel << " from server" << std::endl;
        return cal.isValid();
    }

    void SensorCalibrationStore::registerFallback(const std::string &sensorModel,
                                                  const SensorCalibration &cal)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!calibrations_.count(sensorModel))
        {
            calibrations_[sensorModel] = cal;
        }
    }

    std::optional<SensorCalibration> SensorCalibrationStore::getCalibration(
        const std::string &sensorModel) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = calibrations_.find(sensorModel);
        if (it != calibrations_.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<SensorCalibration> SensorCalibrationStore::getCalibrationByWidth(int width) const
    {
        // IMX708 max = 4608, IMX219 max = 3280
        std::string model = (width > 3500) ? "imx708" : "imx219";
        return getCalibration(model);
    }

    bool SensorCalibrationStore::hasCalibration(const std::string &sensorModel) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return calibrations_.count(sensorModel) > 0;
    }

    void SensorCalibrationStore::clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        calibrations_.clear();
    }

} // namespace sanuwave
