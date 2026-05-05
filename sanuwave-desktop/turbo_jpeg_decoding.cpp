// ============================================================================
// turbo_jpeg_decoding.cpp
// ============================================================================
#include "turbo_jpeg_decoding.h"
#include "logger.h"

namespace sanuwave
{

TurboJpegDecoder::TurboJpegDecoder()
    : tjDecompressor(nullptr)
{
    tjDecompressor = tjInitDecompress();
    
    if (!tjDecompressor)
    {
        lastError = "Failed to initialize TurboJPEG decompressor";
        LOG_ERROR << lastError << std::endl;
    }
    else
    {
        LOG_INFO << "TurboJPEG decoder initialized successfully" << std::endl;
    }
}

TurboJpegDecoder::~TurboJpegDecoder()
{
    if (tjDecompressor)
    {
        tjDestroy(tjDecompressor);
        tjDecompressor = nullptr;
        LOG_INFO << "TurboJPEG decoder destroyed" << std::endl;
    }
}

TurboJpegDecoder& TurboJpegDecoder::getInstance()
{
    static TurboJpegDecoder instance;
    return instance;
}

QImage TurboJpegDecoder::decode(const QByteArray& jpegData)
{
    QImage image;

    // Validate decoder
    if (!tjDecompressor)
    {
        lastError = "TurboJPEG decompressor not initialized";
        LOG_ERROR << lastError << std::endl;
        return image;
    }

    // Validate input
    if (jpegData.isEmpty())
    {
        lastError = "Cannot decode empty JPEG data";
        LOG_ERROR << lastError << std::endl;
        return image;
    }

    // Thread-safe decoding
    std::lock_guard<std::mutex> lock(decoderMutex);

    // Get image dimensions first
    int width, height, subsamp, colorspace;
    int result = tjDecompressHeader3(
        tjDecompressor,
        reinterpret_cast<const unsigned char*>(jpegData.constData()),
        jpegData.size(),
        &width,
        &height,
        &subsamp,
        &colorspace
    );

    if (result != 0)
    {
        lastError = std::string("Failed to read JPEG header: ") + tjGetErrorStr2(tjDecompressor);
        LOG_ERROR << lastError << std::endl;
        return image;
    }
    LOG_INFO << "JPEG - width: " << width << ", height: " << height
        << ", subsamp: " << subsamp << " (";
    switch (subsamp) {
    case TJSAMP_444: LOG_INFO << "4:4:4 - no subsampling"; break;
    case TJSAMP_422: LOG_INFO << "4:2:2"; break;
    case TJSAMP_420: LOG_INFO << "4:2:0"; break;
    case TJSAMP_GRAY: LOG_INFO << "Grayscale"; break;
    default: LOG_INFO << "Unknown"; break;
    }
    LOG_INFO << ")" << std::endl;



    // Allocate temporary buffer for RGB data
    std::vector<unsigned char> rgbBuffer(width * height * 3);
 
    // Decompress JPEG data to RGB format
    result = tjDecompress2(
        tjDecompressor,
        reinterpret_cast<const unsigned char*>(jpegData.constData()),  // Input JPEG data
        jpegData.size(),                                                // Input size
        rgbBuffer.data(),                                                   // Output buffer
        width,                                                          // Width
        0,                                                              // Pitch (0 = width * bytes per pixel)
        height,                                                         // Height
        TJPF_RGB,                                                       // Pixel format (RGB)
        TJFLAG_FASTDCT                                                  // Fast DCT flag
    );

    if (result != 0)
    {
        lastError = std::string("TurboJPEG decompression failed: ") + tjGetErrorStr2(tjDecompressor);
        LOG_ERROR << lastError << std::endl;
        return QImage();
    }
    
    image = QImage(width, height, QImage::Format_RGB888);

    LOG_TRACE << "=== DECODE DETAILS ===" << std::endl;
    LOG_TRACE << "Width: " << width << ", Height: " << height << std::endl;
    LOG_TRACE << "QImage format: " << image.format() << " (should be 4 for RGB888)" << std::endl;
    LOG_TRACE << "QImage bytesPerLine: " << image.bytesPerLine() << std::endl;
    LOG_TRACE << "Expected bytesPerLine: " << (width * 3) << std::endl;
    LOG_TRACE << "rgbBuffer size: " << rgbBuffer.size() << std::endl;
    

    const unsigned char* src = rgbBuffer.data();
    for (int y = 0; y < height; ++y)
    {
        uchar* destRow = image.scanLine(y);
        const uchar* srcRow = src + y * width * 3;
        memcpy(destRow, srcRow, width * 3);
    }


    LOG_TRACE << "Decoded JPEG (TurboJPEG): " << width << "x" << height 
              << " (" << jpegData.size() << " bytes)" << std::endl;
    
    return image;
}

bool TurboJpegDecoder::getImageInfo(const QByteArray& jpegData,
                                     int& width, int& height,
                                     int& subsamp, int& colorspace)
{
    if (!tjDecompressor)
    {
        lastError = "TurboJPEG decompressor not initialized";
        LOG_ERROR << lastError << std::endl;
        return false;
    }
    
    if (jpegData.isEmpty())
    {
        lastError = "Cannot get info from empty JPEG data";
        LOG_ERROR << lastError << std::endl;
        return false;
    }
    
    std::lock_guard<std::mutex> lock(decoderMutex);
    
    int result = tjDecompressHeader3(
        tjDecompressor,
        reinterpret_cast<const unsigned char*>(jpegData.constData()),
        jpegData.size(),
        &width,
        &height,
        &subsamp,
        &colorspace
    );
    
    if (result != 0)
    {
        lastError = std::string("Failed to read JPEG header: ") + tjGetErrorStr2(tjDecompressor);
        LOG_ERROR << lastError << std::endl;
        return false;
    }
    
    return true;
}

} // namespace sanuwave
