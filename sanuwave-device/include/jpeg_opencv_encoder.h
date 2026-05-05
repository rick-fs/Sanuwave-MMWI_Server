// ============================================================================
// jpeg_opencv_encoder.h
// ============================================================================
#ifndef JPEG_OPENCV_ENCODER_H
#define JPEG_OPENCV_ENCODER_H

#include "ijpeg_encoder.h"
#include <mutex>

namespace sanuwave
{

/**
 * @brief OpenCV implementation of JPEG encoder/decoder
 * 
 * Standard JPEG encoding/decoding using OpenCV's built-in functions
 */
class JpegOpenCVEncoder : public IJpegEncoder
{
public:
    JpegOpenCVEncoder();
    ~JpegOpenCVEncoder() override = default;
    
    std::vector<uint8_t> encode(cv::Mat& image, int quality) override;
    bool imencode(cv::Mat& frame, std::vector<uint8_t>& buffer, int quality) override;
    cv::Mat decode(const std::vector<uint8_t>& jpegData) override;
    
    bool isInitialized() const override { return initialized; }
    std::string getLastError() const override { return lastError; }
    
    // Delete copy constructor and assignment operator
    JpegOpenCVEncoder(const JpegOpenCVEncoder&) = delete;
    JpegOpenCVEncoder& operator=(const JpegOpenCVEncoder&) = delete;
    
private:
    bool initialized;
    std::mutex encoderMutex;
    std::string lastError;
};

} // namespace sanuwave

#endif // JPEG_OPENCV_ENCODER_H
