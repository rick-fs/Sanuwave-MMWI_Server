// ============================================================================
// jpeg_turbo_encoder.cpp
// ============================================================================
#include "jpeg_turbo_encoder.h"
#include "logger.h"

namespace sanuwave
{

JpegTurboEncoder::JpegTurboEncoder()
    : tjCompressor(nullptr)
    , tjDecompressor(nullptr)
{
    tjCompressor = tjInitCompress();
    tjDecompressor = tjInitDecompress();
    
    if (!tjCompressor)
    {
        lastError = "Failed to initialize TurboJPEG compressor";
        LOG_ERROR << lastError << std::endl;
    }
    
    if (!tjDecompressor)
    {
        lastError = "Failed to initialize TurboJPEG decompressor";
        LOG_ERROR << lastError << std::endl;
    }
    
    if (tjCompressor && tjDecompressor)
    {
        LOG_INFO << "TurboJPEG encoder/decoder initialized successfully" << std::endl;
    }
}

JpegTurboEncoder::~JpegTurboEncoder()
{
    if (tjCompressor)
    {
        tjDestroy(tjCompressor);
        tjCompressor = nullptr;
    }
    
    if (tjDecompressor)
    {
        tjDestroy(tjDecompressor);
        tjDecompressor = nullptr;
    }
    
    LOG_INFO << "TurboJPEG encoder/decoder destroyed" << std::endl;
}

std::vector<uint8_t> JpegTurboEncoder::encode(cv::Mat& image, int quality)
{
    std::vector<uint8_t> buffer;
    imencode(image, buffer, quality);
    return buffer;
}

bool JpegTurboEncoder::imencode(cv::Mat& frame, std::vector<uint8_t>& buffer, int quality)
{
    buffer.clear();
    
    // Validate encoder
    if (!tjCompressor)
    {
        lastError = "TurboJPEG compressor not initialized";
        LOG_ERROR << lastError << std::endl;
        return false;
    }
    
    // Validate input image
    if (frame.empty())
    {
        lastError = "Cannot encode empty image";
        LOG_ERROR << lastError << std::endl;
        return false;
    }
    
    if (frame.channels() != 3)
    {
        lastError = "Image must be 3-channel (BGR)";
        LOG_ERROR << lastError << std::endl;
        return false;
    }
    
    if (frame.depth() != CV_8U)
    {
        lastError = "Image must be 8-bit";
        LOG_ERROR << lastError << std::endl;
        return false;
    }
    
    // Validate quality
    if (quality < 1 || quality > 100)
    {
        lastError = "Quality must be between 1 and 100";
        LOG_ERROR << lastError << std::endl;
        return false;
    }
    
    // Thread-safe encoding
    std::lock_guard<std::mutex> lock(encoderMutex);
    
    unsigned char* jpegBuf = nullptr;
    unsigned long jpegSize = 0;


    // Encode with TurboJPEG
    // TJPF_BGR: OpenCV default pixel format
    // TJSAMP_420: 4:2:0 chroma subsampling (standard, good compression)
    // TJFLAG_FASTDCT: Use fast DCT algorithm (trades slight quality for speed)
    int result = tjCompress2(
        tjCompressor,
        frame.data,           // Input image data
        frame.cols,           // Width
        0,                    // Pitch (0 = width * bytes per pixel)
        frame.rows,           // Height
        TJPF_RGB,            // Pixel format
        &jpegBuf,            // Output buffer (allocated by TurboJPEG)
        &jpegSize,           // Output size
        TJSAMP_420,          // Chroma subsampling
        quality,             // Quality (1-100)
        TJFLAG_ACCURATEDCT       // Fast DCT flag
    );
    
    if (result != 0)
    {
        lastError = std::string("TurboJPEG compression failed: ") + tjGetErrorStr2(tjCompressor);
        LOG_ERROR << lastError << std::endl;
        
        if (jpegBuf)
        {
            tjFree(jpegBuf);
        }
        return false;
    }
    
    // Copy to vector
    buffer.assign(jpegBuf, jpegBuf + jpegSize);
    
    // Free TurboJPEG-allocated buffer
    tjFree(jpegBuf);
    
    LOG_TRACE << "Encoded JPEG (TurboJPEG): " << frame.cols << "x" << frame.rows 
              << " quality=" << quality << " (" << buffer.size() << " bytes)" << std::endl;
    
    return true;
}

cv::Mat JpegTurboEncoder::decode(const std::vector<uint8_t>& jpegData)
{
    cv::Mat result;
    
    // Validate decoder
    if (!tjDecompressor)
    {
        lastError = "TurboJPEG decompressor not initialized";
        LOG_ERROR << lastError << std::endl;
        return result;
    }
    
    // Validate input data
    if (jpegData.empty())
    {
        lastError = "Cannot decode empty JPEG data";
        LOG_ERROR << lastError << std::endl;
        return result;
    }
    
    // Thread-safe decoding
    std::lock_guard<std::mutex> lock(decoderMutex);
    
    // Get JPEG header information
    int width, height, jpegSubsamp, jpegColorspace;
    int headerResult = tjDecompressHeader3(
        tjDecompressor,
        jpegData.data(),
        jpegData.size(),
        &width,
        &height,
        &jpegSubsamp,
        &jpegColorspace
    );
    
    if (headerResult != 0)
    {
        lastError = std::string("TurboJPEG header decompression failed: ") + tjGetErrorStr2(tjDecompressor);
        LOG_ERROR << lastError << std::endl;
        return result;
    }
    
    // Allocate output buffer
    result = cv::Mat(height, width, CV_8UC3);
    
    // Decompress JPEG to BGR format (OpenCV default)
    int decompressResult = tjDecompress2(
        tjDecompressor,
        jpegData.data(),     // Input JPEG data
        jpegData.size(),     // Input size
        result.data,         // Output buffer
        width,               // Width
        0,                   // Pitch (0 = width * bytes per pixel)
        height,              // Height
        TJPF_BGR,           // Output pixel format (OpenCV default)
        TJFLAG_FASTDCT      // Fast DCT flag
    );
    
    if (decompressResult != 0)
    {
        lastError = std::string("TurboJPEG decompression failed: ") + tjGetErrorStr2(tjDecompressor);
        LOG_ERROR << lastError << std::endl;
        result.release();
        return result;
    }
    
    LOG_DEBUG << "Decoded JPEG (TurboJPEG): " << width << "x" << height 
              << " (" << jpegData.size() << " bytes)" << std::endl;
    
    return result;
}

} // namespace sanuwave
