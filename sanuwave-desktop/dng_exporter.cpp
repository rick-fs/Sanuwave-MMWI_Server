// Copyright 2026 Sanuwave Medical LLC.
//
// Portions of this code were generated with Claude.ai and
// reviewed and edited.

#define TINY_DNG_WRITER_IMPLEMENTATION
#include "tiny_dng_writer.h"

#include "dng_exporter.h"
#include "sensor_calibration.h"
#include "logger.h"
#include <QString>
#include <cstring>

namespace sanuwave
{

// ---------------------------------------------------------------------------
// Fallback color matrices (used only if calibration store has no data)
// ---------------------------------------------------------------------------
static const double kIMX708ColorMatrixFallback[9] = {
     1.5546, -0.5640, -0.1256,
    -0.2489,  1.3432,  0.0259,
    -0.0311, -0.2571,  1.0424
};

static const double kIMX219ColorMatrixFallback[9] = {
     1.6243, -0.6003, -0.1535,
    -0.2297,  1.3622, -0.0156,
     0.0137, -0.3528,  1.1655
};

// ---------------------------------------------------------------------------
void DngExporter::populateFromCalibration(RawImageData& data,
                                           const SensorCalibration& cal)
{
    data.cameraModel = cal.cameraModel;
    data.bitsPerSample = cal.bitsPerSample;
    data.blackLevel = cal.blackLevel;
    data.whiteLevel = cal.whiteLevel;
    
    // CFA pattern
    std::memcpy(data.cfaPattern, cal.cfaPattern.data(), 4);
    
    // Primary color matrix (D65)
    if (cal.d65.valid) {
        std::memcpy(data.colorMatrix1, cal.d65.colorMatrix.data(), 
                    sizeof(data.colorMatrix1));
    }
    
    // Secondary color matrix (incandescent/A) if available
    if (cal.incandescent.valid) {
        data.hasColorMatrix2 = true;
        std::memcpy(data.colorMatrix2, cal.incandescent.colorMatrix.data(),
                    sizeof(data.colorMatrix2));
    }
    
    // As-shot neutral for white balance
    // The ct_curve values are AWB *gains* (multiply R by this to match G)
    // DNG asShotNeutral is the *inverse*: sensor response to neutral gray
    // If gain_R = 0.5, sensor sees 2x as much R as needed, so neutral_R = 1/0.5 = 2
    // But we normalize so G = 1.0
    // 
    // Actually for DNG: asShotNeutral[i] = 1.0 / wb_gain[i], normalized
    // With wb_gains = [R_gain, 1.0, B_gain]:
    //   neutral = [1/R_gain, 1.0, 1/B_gain]
    // Then normalize so max = 1.0
    
    double invR = 1.0 / std::max(0.01, cal.neutralR);
    double invG = 1.0;  // G gain is always 1.0
    double invB = 1.0 / std::max(0.01, cal.neutralB);
    
    // Normalize so largest value is 1.0
    double maxVal = std::max({invR, invG, invB});
    data.asShotNeutral[0] = invR / maxVal;
    data.asShotNeutral[1] = invG / maxVal;
    data.asShotNeutral[2] = invB / maxVal;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void DngExporter::populateSensorDefaults(RawImageData& data,
                                          protocol::BayerPattern pattern,
                                          int width)
{
    data.bitsPerSample = 10;
    data.blackLevel = 64;
    data.whiteLevel = 1023;

    switch (pattern)
    {
        case protocol::BayerPattern::RGGB:
            data.cfaPattern[0] = 0; data.cfaPattern[1] = 1;
            data.cfaPattern[2] = 1; data.cfaPattern[3] = 2;
            break;
        case protocol::BayerPattern::BGGR:
            data.cfaPattern[0] = 2; data.cfaPattern[1] = 1;
            data.cfaPattern[2] = 1; data.cfaPattern[3] = 0;
            break;
        case protocol::BayerPattern::GRBG:
            data.cfaPattern[0] = 1; data.cfaPattern[1] = 0;
            data.cfaPattern[2] = 2; data.cfaPattern[3] = 1;
            break;
        case protocol::BayerPattern::GBRG:
            data.cfaPattern[0] = 1; data.cfaPattern[1] = 2;
            data.cfaPattern[2] = 0; data.cfaPattern[3] = 1;
            break;
    }

    // Determine sensor from cameraModel if already set, otherwise fall back
    // to width heuristic
    bool isIMX708 = false;
    if (!data.cameraModel.empty() && data.cameraModel != "Unknown")
    {
        // Match against known camera identifiers (e.g., "imx708", "imx219")
        std::string model = data.cameraModel;
        std::transform(model.begin(), model.end(), model.begin(), ::tolower);
        isIMX708 = (model.find("imx708") != std::string::npos);
    }
    else
    {
        isIMX708 = (width > 3500);
    }

    if (isIMX708)
    {
        data.cameraModel = "Sony IMX708";
        std::memcpy(data.colorMatrix1, kIMX708ColorMatrixFallback,
                     sizeof(kIMX708ColorMatrixFallback));
    }
    else
    {
        data.cameraModel = "Sony IMX219";
        std::memcpy(data.colorMatrix1, kIMX219ColorMatrixFallback,
                     sizeof(kIMX219ColorMatrixFallback));
    }
}

// ---------------------------------------------------------------------------
RawImageData DngExporter::buildFromCapture(const uint8_t* rawBytes,
                                            size_t rawByteSize,
                                            const RawImageInfo& info)
{
    // Identify sensor by width and delegate
    std::string model = (info.width > 3500) ? "imx708" : "imx219";
    return buildFromCapture(rawBytes, rawByteSize, info, model);
}

// ---------------------------------------------------------------------------
RawImageData DngExporter::buildFromCapture(const uint8_t* rawBytes,
                                            size_t rawByteSize,
                                            const RawImageInfo& info,
                                            const std::string& sensorModel)
{
    RawImageData data;
    data.width = static_cast<uint32_t>(info.width);
    data.height = static_cast<uint32_t>(info.height);

    // Try to get calibration from store
    auto cal = SensorCalibrationStore::instance().getCalibration(sensorModel);
    if (cal.has_value() && cal->isValid()) {
        populateFromCalibration(data, *cal);
        LOG_DEBUG << "DNG: Using calibration store for " << sensorModel << std::endl;
    } else {
        // Fallback to hardcoded defaults
        populateSensorDefaults(data, info.pattern, info.width);
        LOG_DEBUG << "DNG: Using fallback defaults for " << sensorModel << std::endl;
    }

    // Copy raw pixel data
    size_t pixelCount = static_cast<size_t>(info.width) * info.height;
    size_t expectedBytes = pixelCount * 2;  // 16-bit per pixel

    if (rawByteSize < expectedBytes) {
        LOG_ERROR << "DNG: Raw data too small. Expected " << expectedBytes
                  << " bytes, got " << rawByteSize << std::endl;
        return data;  // Returns with empty rawPixels (isValid() == false)
    }

    const uint16_t* src = reinterpret_cast<const uint16_t*>(rawBytes);
    data.rawPixels.resize(pixelCount);

    // Server stores: 16-bit value = (sensor_10bit << shift) + blackLevel
    int shift = info.storageBits - info.bitDepth;
    int blackLevelStorage = info.blackLevel;

    for (size_t i = 0; i < pixelCount; ++i) {
        int val = static_cast<int>(src[i]) - blackLevelStorage;
        val = std::max(0, val) >> shift;
        // Add DNG black level (from calibration or default 64)
        data.rawPixels[i] = static_cast<uint16_t>(
            std::min(static_cast<int>(data.whiteLevel), val + data.blackLevel));
    }

    // Store capture metadata
    data.exposureTime_s = info.exposureUs / 1000000.0;
    data.analogGain = info.analogGain;

    return data;
}

// ---------------------------------------------------------------------------
bool DngExporter::writeDng(const QString& filename,
                           const RawImageData& raw,
                           QString& errorMsg)
{
    if (!raw.isValid()) {
        errorMsg = "No valid RAW data to save";
        return false;
    }

    using namespace tinydngwriter;

    DNGImage dngImage;
    dngImage.SetBigEndian(false);

    // Basic image parameters
    dngImage.SetImageWidth(raw.width);
    dngImage.SetImageLength(raw.height);
    dngImage.SetRowsPerStrip(raw.height);

    // Sample configuration
    dngImage.SetSamplesPerPixel(1);
    unsigned short bps = 16;
    dngImage.SetBitsPerSample(1, &bps);

    // DNG version 1.4
    dngImage.SetDNGVersion(1, 4, 0, 0);
    dngImage.SetUniqueCameraModel(raw.cameraModel);

    // Photometric: Color Filter Array
    dngImage.SetPhotometric(PHOTOMETRIC_CFA);
    dngImage.SetCompression(COMPRESSION_NONE);
    dngImage.SetPlanarConfig(PLANARCONFIG_CONTIG);
    dngImage.SetOrientation(ORIENTATION_TOPLEFT);

    // CFA pattern
    dngImage.SetCFARepeatPatternDim(2, 2);
    dngImage.SetCFAPattern(4, raw.cfaPattern);

    // Black and white levels
    dngImage.SetBlackLevelRepeatDim(1, 1);
    unsigned short blackLevel = raw.blackLevel;
    dngImage.SetBlackLevel(1, &blackLevel);

    double whiteLevel = static_cast<double>(raw.whiteLevel);
    dngImage.SetWhiteLevelRational(1, &whiteLevel);

    // Primary color matrix (D65)
    dngImage.SetColorMatrix1(3, raw.colorMatrix1);
    dngImage.SetCalibrationIlluminant1(21);  // D65

    // Secondary color matrix if available (Standard Light A)
    if (raw.hasColorMatrix2) {
        dngImage.SetColorMatrix2(3, raw.colorMatrix2);
        dngImage.SetCalibrationIlluminant2(17);  // Standard Light A
    }

    // As-shot neutral
    dngImage.SetAsShotNeutral(3, raw.asShotNeutral);

    // Active area
    unsigned int activeArea[4] = {0, 0, raw.height, raw.width};
    dngImage.SetActiveArea(activeArea);

    // Software tag
    dngImage.SetSoftware("Sanuwave Medical Imaging");

    // Image data
    std::vector<unsigned char> imageBytes(raw.rawPixels.size() * sizeof(uint16_t));
    std::memcpy(imageBytes.data(), raw.rawPixels.data(), imageBytes.size());
    dngImage.SetImageData(imageBytes.data(), imageBytes.size());

    // Write file
    DNGWriter writer(false);
    writer.AddImage(&dngImage);

    std::string err;
    bool success = writer.WriteToFile(filename.toStdString().c_str(), &err);

    if (!success) {
        errorMsg = QString::fromStdString(err);
        LOG_ERROR << "DNG write error: " << err << std::endl;
    } else {
        LOG_INFO << "DNG saved: " << filename.toStdString()
                 << " (" << raw.width << "x" << raw.height
                 << ", " << raw.cameraModel << ")"
                 << (raw.hasColorMatrix2 ? " [dual illuminant]" : "")
                 << std::endl;
    }

    return success;
}

} // namespace sanuwave
