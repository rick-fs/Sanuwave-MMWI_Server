// ============================================================================
// ijpeg_encoder.h
// ============================================================================
#ifndef IJPEG_ENCODER_H
#define IJPEG_ENCODER_H

#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

namespace sanuwave
{

/**
 * @brief Abstract interface for JPEG encoding/decoding
 */
class IJpegEncoder
{
public:
    virtual ~IJpegEncoder() = default;
    
    /**
     * @brief Encode an OpenCV Mat to JPEG
     * 
     * @param image Input image (must be 8-bit, 3-channel BGR)
     * @param quality JPEG quality (1-100)
     * @return Encoded JPEG data, or empty vector on error
     */
    virtual std::vector<uint8_t> encode(cv::Mat& image, int quality) = 0;
    
    /**
     * @brief Encode an OpenCV Mat to JPEG (lower-level interface)
     * 
     * @param frame Input image (must be 8-bit, 3-channel BGR)
     * @param buffer Output buffer for encoded JPEG data
     * @param quality JPEG quality (1-100)
     * @return true on success, false on error
     */
    virtual bool imencode(cv::Mat& frame, std::vector<uint8_t>& buffer, int quality) = 0;
    
    /**
     * @brief Decode JPEG data to OpenCV Mat
     * 
     * @param jpegData JPEG encoded data
     * @return Decoded image, or empty Mat on error
     */
    virtual cv::Mat decode(const std::vector<uint8_t>& jpegData) = 0;
    
    /**
     * @brief Check if encoder is initialized
     */
    virtual bool isInitialized() const = 0;
    
    /**
     * @brief Get last error message
     */
    virtual std::string getLastError() const = 0;
};

} // namespace sanuwave

#endif // JPEG_ENCODER_H
