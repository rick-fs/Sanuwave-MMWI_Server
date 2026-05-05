#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "protocol_constants.h"
#include "diag_frame_metadata.h"
#include "diag_sensor_info.h"

namespace sanuwave {
namespace protocol {

struct DiagRawFrame {
    // Header fields
    std::string  camera;
    uint8_t      frameIndex    = 0;
    uint8_t      frameCount    = 0;
    uint32_t     width         = 0;
    uint32_t     height        = 0;
    uint32_t     bitsPerPixel  = 0;
    uint32_t     sensorBitDepth = 0;
    BayerPattern bayerPattern  = BayerPattern::BGGR;
    std::string  pixelFormat;
    uint32_t     dataSize      = 0;

    // ROI (0,0,0,0 if full frame)
    uint32_t roiX      = 0;
    uint32_t roiY      = 0;
    uint32_t roiWidth  = 0;
    uint32_t roiHeight = 0;

    // Nested objects
    DiagFrameMetadata  metadata;
    DiagSensorInfoData sensorInfo;

    // Raw pixel data
    std::vector<uint8_t> pixelData;
};

} // namespace protocol
} // namespace sanuwave