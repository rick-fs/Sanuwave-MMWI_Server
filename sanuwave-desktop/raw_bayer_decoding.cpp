#include "raw_bayer_decoding.h"
#include <cstring>
#include <algorithm>
#include <sstream>
#include "protocol_constants.h"
#include "logger.h"
namespace sanuwave
{

protocol::BayerPattern RawBayerDecoder::patternFromString(const std::string& str)
{
    if (str == "BGGR") return protocol::BayerPattern::BGGR;
    if (str == "GRBG") return protocol::BayerPattern::GRBG;
    if (str == "GBRG") return protocol::BayerPattern::GBRG;
    return protocol::BayerPattern::RGGB;  // Default
}

std::string RawBayerDecoder::patternToString(protocol::BayerPattern pattern)
{
    switch (pattern)
    {
        case protocol::BayerPattern::BGGR: return "BGGR";
        case protocol::BayerPattern::GRBG: return "GRBG";
        case protocol::BayerPattern::GBRG: return "GBRG";
        case protocol::BayerPattern::RGGB: 
        default: return "RGGB";
    }
}

std::string RawBayerDecoder::createHeader(const RawImageInfo& info)
{
    std::ostringstream ss;
    ss << "RAW|" 
       << info.width << "|" 
       << info.height << "|"
       << info.bitDepth << "|"
       << info.storageBits << "|"
       << info.blackLevel << "|"
       << patternToString(info.pattern) << "|";
    return ss.str();
}

bool RawBayerDecoder::parseHeader(const uint8_t* data, size_t size,
                                   RawImageInfo& info, size_t& headerLength)
{
    // Check minimum size and prefix
    if (size < 20 || memcmp(data, "RAW|", 4) != 0)
    {
        return false;
    }
    
    // Find header end (7 pipe characters total)
    // Format: RAW|width|height|bit_depth|storage_bits|black_level|pattern|
    const char* str = reinterpret_cast<const char*>(data);
    int pipeCount = 0;
    size_t pos = 0;
    
    for (pos = 0; pos < size && pos < 80; ++pos)
    {
        if (str[pos] == '|')
        {
            pipeCount++;
            if (pipeCount == 7)
            {
                pos++;
                break;
            }
        }
    }
    
    if (pipeCount != 7)
    {
        return false;
    }
    
    headerLength = pos;
    
    std::string header(str, pos);
    
    // Parse fields: RAW|width|height|bit_depth|storage_bits|black_level|pattern|
    size_t start = 4;  // Skip "RAW|"
    size_t end = header.find('|', start);
    if (end == std::string::npos) return false;
    info.width = std::stoi(header.substr(start, end - start));
    
    start = end + 1;
    end = header.find('|', start);
    if (end == std::string::npos) return false;
    info.height = std::stoi(header.substr(start, end - start));
    
    start = end + 1;
    end = header.find('|', start);
    if (end == std::string::npos) return false;
    info.bitDepth = std::stoi(header.substr(start, end - start));
    
    start = end + 1;
    end = header.find('|', start);
    if (end == std::string::npos) return false;
    info.storageBits = std::stoi(header.substr(start, end - start));
    
    start = end + 1;
    end = header.find('|', start);
    if (end == std::string::npos) return false;
    info.blackLevel = std::stoi(header.substr(start, end - start));
    
    start = end + 1;
    end = header.find('|', start);
    if (end == std::string::npos) return false;
    info.pattern = patternFromString(header.substr(start, end - start));
    
    return true;
}

bool RawBayerDecoder::decode(const uint8_t* rawData, size_t rawSize,
                              const RawImageInfo& info, uint8_t* rgbOut)
{
    if (!rawData || !rgbOut || info.width <= 0 || info.height <= 0)
    {
        return false;
    }
    
    size_t expectedSize;
    if (info.storageBits <= 8)
    {
        expectedSize = info.width * info.height;
    }
    else
    {
        expectedSize = info.width * info.height * 2;
    }
    
    if (rawSize < expectedSize)
    {
        return false;
    }
    
    if (info.storageBits <= 8)
    {
        demosaicBilinear8(rawData, info.width, info.height, info.pattern, rgbOut);
    }
    else
    {
        demosaicBilinear16(reinterpret_cast<const uint16_t*>(rawData),
                          info.width, info.height, info, rgbOut);
    }
    
    return true;
}

std::vector<uint8_t> RawBayerDecoder::decode(const uint8_t* rawData, size_t rawSize,
                                              const RawImageInfo& info)
{
    std::vector<uint8_t> rgb(info.width * info.height * 3);
    
    if (!decode(rawData, rawSize, info, rgb.data()))
    {
        return std::vector<uint8_t>();
    }
    
    return rgb;
}

std::vector<uint8_t> RawBayerDecoder::decodeWithHeader(const uint8_t* data, size_t size,
                                                        RawImageInfo& info)
{
    size_t headerLen = 0;
    if (!parseHeader(data, size, info, headerLen))
    {
        return std::vector<uint8_t>();
    }
    
    return decode(data + headerLen, size - headerLen, info);
}

// Returns the byte offset of the first pixel (i.e. one past the 7th '|'),
// or -1 if the header is malformed or the payload is too short.
// The RAW| header format is:
//   RAW|cols|rows|sensorBits|storageBits|blackLevel|bayerPattern|<pixels>
//         1    2      3          4           5            6
// There are 7 pipe characters total before the pixel data begins.
 int RawBayerDecoder::findHeaderEnd(const QByteArray& data)
{
    constexpr int kPipesExpected = 7;
    constexpr int kMaxHeaderScan = 256;

    int pipeCount = 0;
    const int limit = qMin(data.size(), kMaxHeaderScan);
    for (int i = 0; i < limit; ++i)
    {
        if (data[i] == '|')
        {
            if (++pipeCount == kPipesExpected)
                return i + 1;
        }
    }
    return -1;
}

QPixmap RawBayerDecoder::previewFromRawPayload(const QByteArray& data, int subsample)
{
    if (!data.startsWith("RAW|"))
        return {};

    // Parse header via both paths and assert they agree
    int headerLenFind = findHeaderEnd(data);

    size_t headerLenParse = 0;
    RawImageInfo parsedInfo;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    bool parseOk = RawBayerDecoder::parseHeader(bytes, static_cast<size_t>(data.size()),
                                                parsedInfo, headerLenParse);

    if (headerLenFind < 0 || !parseOk) {
        LOG_ERROR << "previewFromRawPayload: header parse failed"
                  << " findHeaderEnd=" << headerLenFind
                  << " parseOk=" << parseOk << std::endl;
        return {};
    }

    if (static_cast<size_t>(headerLenFind) != headerLenParse) {
        LOG_ERROR << "previewFromRawPayload: HEADER MISMATCH"
                  << " findHeaderEnd=" << headerLenFind
                  << " parseHeader=" << headerLenParse << std::endl;
        // Fall through using parseHeader result
    }

    // Always use parseHeader result — it doesn't scan into pixel data
    const int headerLen = static_cast<int>(headerLenParse);
    RawImageInfo info = parsedInfo;

    if (info.width <= 0 || info.height <= 0)
        return {};

    int bytesPerPixel = (info.storageBits == 16) ? 2 : 1;
    if (data.size() - headerLen < info.width * info.height * bytesPerPixel)
        return {};

    const uint8_t* src = bytes + headerLen;
    size_t srcSize = static_cast<size_t>(data.size() - headerLen);

    // Full decode then scale down, reusing the existing decode path
    std::vector<uint8_t> rgb = RawBayerDecoder::decode(src, srcSize, info);
    if (rgb.empty())
        return {};

    QImage preview = QImage(rgb.data(), info.width, info.height,
                            info.width * 3, QImage::Format_RGB888)
                         .copy()
                         .scaled(info.width  / subsample,
                                 info.height / subsample,
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
    return QPixmap::fromImage(preview);
}



inline uint8_t RawBayerDecoder::getPixel8(const uint8_t* data, int x, int y,
                                           int width, int height)
{
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    return data[y * width + x];
}

inline uint16_t RawBayerDecoder::getPixel16(const uint16_t* data, int x, int y,
                                             int width, int height)
{
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    return data[y * width + x];
}

void RawBayerDecoder::demosaicBilinear8(const uint8_t* bayer, int width, int height,
                                         protocol::BayerPattern pattern, uint8_t* rgb)
{
    // Determine offsets based on Bayer pattern
    // Pattern defines what color is at (0,0)
    int rRowOff, rColOff;  // Red pixel offset
    int bRowOff, bColOff;  // Blue pixel offset
    
    switch (pattern)
    {
        case protocol::BayerPattern::RGGB:
            rRowOff = 0; rColOff = 0;
            bRowOff = 1; bColOff = 1;
            break;
        case protocol::BayerPattern::BGGR:
            rRowOff = 1; rColOff = 1;
            bRowOff = 0; bColOff = 0;
            break;
        case protocol::BayerPattern::GRBG:
            rRowOff = 0; rColOff = 1;
            bRowOff = 1; bColOff = 0;
            break;
        case protocol::BayerPattern::GBRG:
            rRowOff = 1; rColOff = 0;
            bRowOff = 0; bColOff = 1;
            break;
    }
    
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int r, g, b;
            int row = y % 2;
            int col = x % 2;
            
            // Determine what color this pixel is
            bool isRed = (row == rRowOff && col == rColOff);
            bool isBlue = (row == bRowOff && col == bColOff);
            bool isGreenR = (row == rRowOff && col != rColOff);  // Green in red row
            
            if (isRed)
            {
                // Red pixel: R is known, interpolate G and B
                r = getPixel8(bayer, x, y, width, height);
                g = (getPixel8(bayer, x-1, y, width, height) +
                     getPixel8(bayer, x+1, y, width, height) +
                     getPixel8(bayer, x, y-1, width, height) +
                     getPixel8(bayer, x, y+1, width, height)) / 4;
                b = (getPixel8(bayer, x-1, y-1, width, height) +
                     getPixel8(bayer, x+1, y-1, width, height) +
                     getPixel8(bayer, x-1, y+1, width, height) +
                     getPixel8(bayer, x+1, y+1, width, height)) / 4;
            }
            else if (isBlue)
            {
                // Blue pixel: B is known, interpolate R and G
                b = getPixel8(bayer, x, y, width, height);
                g = (getPixel8(bayer, x-1, y, width, height) +
                     getPixel8(bayer, x+1, y, width, height) +
                     getPixel8(bayer, x, y-1, width, height) +
                     getPixel8(bayer, x, y+1, width, height)) / 4;
                r = (getPixel8(bayer, x-1, y-1, width, height) +
                     getPixel8(bayer, x+1, y-1, width, height) +
                     getPixel8(bayer, x-1, y+1, width, height) +
                     getPixel8(bayer, x+1, y+1, width, height)) / 4;
            }
            else if (isGreenR)
            {
                // Green in red row
                g = getPixel8(bayer, x, y, width, height);
                r = (getPixel8(bayer, x-1, y, width, height) +
                     getPixel8(bayer, x+1, y, width, height)) / 2;
                b = (getPixel8(bayer, x, y-1, width, height) +
                     getPixel8(bayer, x, y+1, width, height)) / 2;
            }
            else  // isGreenB
            {
                // Green in blue row
                g = getPixel8(bayer, x, y, width, height);
                b = (getPixel8(bayer, x-1, y, width, height) +
                     getPixel8(bayer, x+1, y, width, height)) / 2;
                r = (getPixel8(bayer, x, y-1, width, height) +
                     getPixel8(bayer, x, y+1, width, height)) / 2;
            }
            
            int idx = (y * width + x) * 3;
            rgb[idx + 0] = static_cast<uint8_t>(r);
            rgb[idx + 1] = static_cast<uint8_t>(g);
            rgb[idx + 2] = static_cast<uint8_t>(b);
        }
    }
}

void RawBayerDecoder::demosaicBilinear16(const uint16_t* bayer, int width, int height,
                                          const RawImageInfo& info, uint8_t* rgb)
{
    // Determine offsets based on Bayer pattern
    int rRowOff, rColOff;
    int bRowOff, bColOff;
    
    switch (info.pattern)
    {
        case protocol::BayerPattern::RGGB:
            rRowOff = 0; rColOff = 0;
            bRowOff = 1; bColOff = 1;
            break;
        case protocol::BayerPattern::BGGR:
            rRowOff = 1; rColOff = 1;
            bRowOff = 0; bColOff = 0;
            break;
        case protocol::BayerPattern::GRBG:
            rRowOff = 0; rColOff = 1;
            bRowOff = 1; bColOff = 0;
            break;
        case protocol::BayerPattern::GBRG:
            rRowOff = 1; rColOff = 0;
            bRowOff = 0; bColOff = 1;
            break;
    }
    
    // Data format: 10-bit sensor values stored in upper bits of 16-bit words
    // 16-bit value = (sensor_10bit << 6) + offset
    // Range observed: ~3968 to ~4992 (difference of 1024 = 10-bit range)
    
    int shift = info.storageBits - info.bitDepth;  // e.g., 16 - 10 = 6
    int blackLevel = info.blackLevel;
    
    // If blackLevel is 0, use a reasonable default
    if (blackLevel == 0 && shift > 0)
    {
        blackLevel = 64 << shift;  // ~4096 for 10-bit in 16-bit
    }
    
    // Max value in 10-bit space (0-1023)
    int maxValue10bit = (1 << info.bitDepth) - 1;  // 1023 for 10-bit
    
    // Scale factor: convert 10-bit range (0-1023) to 8-bit (0-255)
    float scale = 255.0f / maxValue10bit;  // 0.249
    
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int r, g, b;
            int row = y % 2;
            int col = x % 2;
            
            bool isRed = (row == rRowOff && col == rColOff);
            bool isBlue = (row == bRowOff && col == bColOff);
            bool isGreenR = (row == rRowOff && col != rColOff);
            
            if (isRed)
            {
                r = getPixel16(bayer, x, y, width, height);
                g = (getPixel16(bayer, x-1, y, width, height) +
                     getPixel16(bayer, x+1, y, width, height) +
                     getPixel16(bayer, x, y-1, width, height) +
                     getPixel16(bayer, x, y+1, width, height)) / 4;
                b = (getPixel16(bayer, x-1, y-1, width, height) +
                     getPixel16(bayer, x+1, y-1, width, height) +
                     getPixel16(bayer, x-1, y+1, width, height) +
                     getPixel16(bayer, x+1, y+1, width, height)) / 4;
            }
            else if (isBlue)
            {
                b = getPixel16(bayer, x, y, width, height);
                g = (getPixel16(bayer, x-1, y, width, height) +
                     getPixel16(bayer, x+1, y, width, height) +
                     getPixel16(bayer, x, y-1, width, height) +
                     getPixel16(bayer, x, y+1, width, height)) / 4;
                r = (getPixel16(bayer, x-1, y-1, width, height) +
                     getPixel16(bayer, x+1, y-1, width, height) +
                     getPixel16(bayer, x-1, y+1, width, height) +
                     getPixel16(bayer, x+1, y+1, width, height)) / 4;
            }
            else if (isGreenR)
            {
                g = getPixel16(bayer, x, y, width, height);
                r = (getPixel16(bayer, x-1, y, width, height) +
                     getPixel16(bayer, x+1, y, width, height)) / 2;
                b = (getPixel16(bayer, x, y-1, width, height) +
                     getPixel16(bayer, x, y+1, width, height)) / 2;
            }
            else
            {
                g = getPixel16(bayer, x, y, width, height);
                b = (getPixel16(bayer, x-1, y, width, height) +
                     getPixel16(bayer, x+1, y, width, height)) / 2;
                r = (getPixel16(bayer, x, y-1, width, height) +
                     getPixel16(bayer, x, y+1, width, height)) / 2;
            }
            
            // Subtract black level (in 16-bit space), shift to 10-bit, then scale to 8-bit
            r = std::max(0, r - blackLevel) >> shift;
            g = std::max(0, g - blackLevel) >> shift;
            b = std::max(0, b - blackLevel) >> shift;
            
            int idx = (y * width + x) * 3;
            rgb[idx + 0] = static_cast<uint8_t>(std::min(255, static_cast<int>(r * scale)));
            rgb[idx + 1] = static_cast<uint8_t>(std::min(255, static_cast<int>(g * scale)));
            rgb[idx + 2] = static_cast<uint8_t>(std::min(255, static_cast<int>(b * scale)));
        }
    }
}

} // namespace sanuwave
