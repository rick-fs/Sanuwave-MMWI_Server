#ifndef RAW_BAYER_DECODER_H
#define RAW_BAYER_DECODER_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "protocol_constants.h"
#include <QPixmap>
namespace sanuwave
{



struct RawImageInfo
{
    int width = 0;
    int height = 0;
    int bitDepth = 10;       // Sensor bit depth (actual data precision)
    int storageBits = 16;    // Container bit depth (8 or 16)
    int blackLevel = 0;      // Black level offset (0 = auto-detect disabled)
    protocol::BayerPattern pattern = protocol::BayerPattern::RGGB;
    
    // Optional metadata from capture
    int exposureUs = 0;
    float analogGain = 1.0f;
};

class RawBayerDecoder
{
public:
    // Parse header from raw data, returns true if valid
    // Format: "RAW|width|height|bit_depth|storage_bits|black_level|pattern|"
    static bool parseHeader(const uint8_t* data, size_t size, 
                            RawImageInfo& info, size_t& headerLength);
    
    // Create header string for encoding
    static std::string createHeader(const RawImageInfo& info);
    
    // Decode raw Bayer to RGB24 (8-bit per channel)
    // Output buffer must be width * height * 3 bytes
    static bool decode(const uint8_t* rawData, size_t rawSize,
                       const RawImageInfo& info,
                       uint8_t* rgbOut);
    
    // Convenience: allocates output buffer
    static std::vector<uint8_t> decode(const uint8_t* rawData, size_t rawSize,
                                       const RawImageInfo& info);
    
    // Decode with header parsing
    static std::vector<uint8_t> decodeWithHeader(const uint8_t* data, size_t size,
                                                  RawImageInfo& info);
    
    // Helper to convert string to BayerPattern
    static protocol::BayerPattern patternFromString(const std::string& str);
    
    // Helper to convert BayerPattern to string
    static std::string patternToString(protocol::BayerPattern pattern);

    static int findHeaderEnd(const QByteArray &data);

    static QPixmap previewFromRawPayload(const QByteArray &data, int subsample = 4);

private:
    // Demosaic implementations
    static void demosaicBilinear8(const uint8_t* bayer, int width, int height,
                                   protocol::BayerPattern pattern, uint8_t* rgb);
    
    static void demosaicBilinear16(const uint16_t* bayer, int width, int height,
                                    const RawImageInfo& info, uint8_t* rgb);
    
    // Get pixel with bounds checking
    static inline uint8_t getPixel8(const uint8_t* data, int x, int y, 
                                     int width, int height);
    static inline uint16_t getPixel16(const uint16_t* data, int x, int y,
                                       int width, int height);
};

} // namespace sanuwave

#endif // RAW_BAYER_DECODER_H
