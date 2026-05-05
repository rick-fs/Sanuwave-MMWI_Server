// ============================================================================
// turbojpeg_encoder.h
// ============================================================================
#ifndef TURBOJPEG_ENCODER_H
#define TURBOJPEG_ENCODER_H

#include <vector>
#include <mutex>
#include <turbojpeg.h>
#include <opencv2/opencv.hpp>

namespace sanuwave
{

/**
 * @brief Singleton class for TurboJPEG encoding
 * 
 * Manages a persistent TurboJPEG compressor instance for efficient
 * JPEG encoding during streaming operations.
 */
class TurboJpegEncoder
{
public:
    /**
     * @brief Get the singleton instance
     */
    static TurboJpegEncoder& getInstance();
    
    /**
     * @brief Encode an OpenCV Mat to JPEG
     * 
     * @param image Input image (must be 8-bit, 3-channel BGR/RGB)
     * @param quality JPEG quality (1-100)
     * @param pixelFormat Pixel format (TJPF_BGR for OpenCV default, TJPF_RGB if converted)
     * @return Encoded JPEG data, or empty vector on error
     */
    std::vector<uint8_t> encode(const cv::Mat& image, int quality, int pixelFormat = TJPF_BGR);
    
    /**
     * @brief Check if encoder is initialized
     */
    bool isInitialized() const { return tjCompressor != nullptr; }
    
    /**
     * @brief Get last error message
     */
    std::string getLastError() const { return lastError; }
    
    // Delete copy constructor and assignment operator (singleton)
    TurboJpegEncoder(const TurboJpegEncoder&) = delete;
    TurboJpegEncoder& operator=(const TurboJpegEncoder&) = delete;
    
private:
    TurboJpegEncoder();
    ~TurboJpegEncoder();
    
    tjhandle tjCompressor;
    std::mutex encoderMutex;  // Thread safety for encoding operations
    std::string lastError;
};

} // namespace sanuwave

#endif // TURBOJPEG_ENCODER_H
