// ============================================================================
// turbojpeg_encoder.cpp
// ============================================================================
#include "turbo_jpeg_encoder.h"
#include "logger.h"
#include <cstring>

namespace sanuwave
{

TurboJpegEncoder::TurboJpegEncoder()
    : tjCompressor(nullptr)
{
    tjCompressor = tjInitCompress();
    
    if (!tjCompressor)
    {
        lastError = "Failed to initialize TurboJPEG compressor";
        LOG_ERROR << lastError << std::endl;
    }
    else
    {
        LOG_INFO << "TurboJPEG encoder initialized successfully" << std::endl;
    }
}

TurboJpegEncoder::~TurboJpegEncoder()
{
    if (tjCompressor)
    {
        tjDestroy(tjCompressor);
        tjCompressor = nullptr;
        LOG_INFO << "TurboJPEG encoder destroyed" << std::endl;
    }
}

TurboJpegEncoder& TurboJpegEncoder::getInstance()
{
    static TurboJpegEncoder instance;
    return instance;
}

std::vector<uint8_t> TurboJpegEncoder::encode(const cv::Mat& image, int quality, int pixelFormat)
{
    std::vector<uint8_t> buffer;
    
    // Validate encoder
    if (!tjCompressor)
    {
        lastError = "TurboJPEG compressor not initialized";
        LOG_ERROR << lastError << std::endl;
        return buffer;
    }
    
    // Validate input image
    if (image.empty())
    {
        lastError = "Cannot encode empty image";
        LOG_ERROR << lastError << std::endl;
        return buffer;
    }
    
    if (image.channels() != 3)
    {
        lastError = "Image must be 3-channel (RGB/BGR)";
        LOG_ERROR << lastError << std::endl;
        return buffer;
    }
    
    if (image.depth() != CV_8U)
    {
        lastError = "Image must be 8-bit";
        LOG_ERROR << lastError << std::endl;
        return buffer;
    }
    
    // Validate quality
    if (quality < 1 || quality > 100)
    {
        lastError = "Quality must be between 1 and 100";
        LOG_ERROR << lastError << std::endl;
        return buffer;
    }
    
    // Thread-safe encoding
    std::lock_guard<std::mutex> lock(encoderMutex);
    
    unsigned char* jpegBuf = nullptr;
    unsigned long jpegSize = 0;
    int pitch = image.step[0]; 
    // Encode with TurboJPEG
    // TJSAMP_420: 4:2:0 chroma subsampling (standard, good compression)
    // TJFLAG_FASTDCT: Use fast DCT algorithm (trades slight quality for speed)
    int result = tjCompress2(
        tjCompressor,
        image.data,           // Input image data
        image.cols,           // Width
        pitch,                // pitch of image.
        image.rows,           // Height
        pixelFormat,          // Pixel format (TJPF_BGR or TJPF_RGB)
        &jpegBuf,            // Output buffer (allocated by TurboJPEG)
        &jpegSize,           // Output size
        TJSAMP_420,          // Chroma subsampling
        quality,             // Quality (1-100)
        TJFLAG_FASTDCT       // Fast DCT flag
    );
    
    if (result != 0)
    {
        lastError = std::string("TurboJPEG compression failed: ") + tjGetErrorStr2(tjCompressor);
        LOG_ERROR << lastError << std::endl;
        
        if (jpegBuf)
        {
            tjFree(jpegBuf);
        }
        return buffer;
    }
    
    // Copy to vector
    buffer.assign(jpegBuf, jpegBuf + jpegSize);
    
    // Free TurboJPEG-allocated buffer
    tjFree(jpegBuf);
    
    LOG_TRACE << "Encoded JPEG (TurboJPEG): " << image.cols << "x" << image.rows 
              << " quality=" << quality << " (" << buffer.size() << " bytes)" << std::endl;
    
    return buffer;
}

} // namespace sanuwave
