#ifndef DNG_EXPORTER_H
#define DNG_EXPORTER_H
// Copyright 2026 Sanuwave Medical LLC.
//
// Portions of this code were generated with Claude.ai and
// reviewed and edited.

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "raw_bayer_decoding.h"
#include <QString>

namespace sanuwave
{

// Forward declaration
struct SensorCalibration;

/// Metadata needed for DNG export, populated from RawImageInfo + calibration
struct RawImageData
{
    std::vector<uint16_t> rawPixels;  // Original Bayer data (16-bit per pixel)
    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t bitsPerSample = 10;

    // CFA pattern values: 0=Red, 1=Green, 2=Blue
    // RGGB = {0,1,1,2}, BGGR = {2,1,1,0}, GRBG = {1,0,2,1}, GBRG = {1,2,0,1}
    uint8_t cfaPattern[4] = {0, 1, 1, 2};  // Default RGGB

    std::string cameraModel = "Unknown";
    uint16_t blackLevel = 64;
    uint16_t whiteLevel = 1023;

    // Primary color matrix (XYZ to camera, D65 illuminant) — 3×3 stored row-major
    double colorMatrix1[9] = {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };
    
    // Optional second color matrix (Standard Light A / ~2850K)
    bool hasColorMatrix2 = false;
    double colorMatrix2[9] = {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };

    // Capture metadata (for EXIF)
    double exposureTime_s = 0.0;
    double analogGain = 1.0;
    
    // As-shot white balance (neutral point)
    double asShotNeutral[3] = {1.0, 1.0, 1.0};

    // Optional noise profile [slope, intercept] per channel
    bool hasNoiseProfile = false;
    double noiseProfile[6] = {0, 0, 0, 0, 0, 0};  // R, G, B pairs

    // Optional lens shading correction (per-channel gain tables)
    std::optional<std::vector<float>> lensShading;
    int lensShadingWidth = 0;
    int lensShadingHeight = 0;

    // Optional chromatic aberration correction
    struct ChromaticAberration {
        double redGreenCoeffs[4] = {0, 0, 0, 0};
        double blueGreenCoeffs[4] = {0, 0, 0, 0};
    };
    std::optional<ChromaticAberration> chromatic;

    bool isValid() const { return !rawPixels.empty() && width > 0 && height > 0; }
    void clear() { 
        rawPixels.clear(); 
        width = height = 0; 
        hasColorMatrix2 = false;
        hasNoiseProfile = false;
        lensShading.reset();
        chromatic.reset();
    }
};

class DngExporter
{
public:
    /// Write RawImageData to a DNG file. Returns true on success.
    /// On failure, errorMsg contains the reason.
    static bool writeDng(const QString& filename,
                         const RawImageData& raw,
                         QString& errorMsg);

    /// Populate a RawImageData struct from raw bytes + parsed header info.
    /// Uses SensorCalibrationStore for sensor metadata.
    /// rawBytes points to the pixel data AFTER the header.
    /// rawByteSize is the size of pixel data only.
    static RawImageData buildFromCapture(const uint8_t* rawBytes,
                                         size_t rawByteSize,
                                         const RawImageInfo& info);
    
    /// Overload that accepts explicit sensor model (preferred when known)
    static RawImageData buildFromCapture(const uint8_t* rawBytes,
                                         size_t rawByteSize,
                                         const RawImageInfo& info,
                                         const std::string& sensorModel);
    /// Fallback: fill from hardcoded defaults based on pattern/width
    static void populateSensorDefaults(RawImageData& data,
                                        protocol::BayerPattern pattern,
                                        int width);

private:
    /// Fill in sensor-specific metadata from calibration store
    static void populateFromCalibration(RawImageData& data,
                                         const SensorCalibration& cal);
    
   
};
} // namespace sanuwave

#endif // DNG_EXPORTER_H
