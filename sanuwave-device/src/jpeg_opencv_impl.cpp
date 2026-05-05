// ============================================================================
// jpeg_opencv_encoder.cpp
// ============================================================================
#include "jpeg_opencv_encoder.h"
#include "logger.h"

namespace sanuwave
{

JpegOpenCVEncoder::JpegOpenCVEncoder()
    : initialized(true)
{
    LOG_INFO << "OpenCV JPEG encoder/decoder initialized successfully" << std::endl;
}

std::vector<uint8_t> JpegOpenCVEncoder::encode(cv::Mat& image, int quality)
{
    std::vector<uint8_t> buffer;
    imencode(image, buffer, quality);
    return buffer;
}

bool JpegOpenCVEncoder::imencode(cv::Mat& frame, std::vector<uint8_t>& buffer, int quality)
{
    buffer.clear();
    
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
    
    try
    {
        // Set JPEG encoding parameters
        std::vector<int> params;
        params.push_back(cv::IMWRITE_JPEG_QUALITY);
        params.push_back(quality);
        
        // Encode to JPEG
        bool success = cv::imencode(".jpg", frame, buffer, params);
        
        if (!success || buffer.empty())
        {
            lastError = "OpenCV JPEG encoding failed";
            LOG_ERROR << lastError << std::endl;
            buffer.clear();
            return false;
        }
        
        LOG_DEBUG << "Encoded JPEG (OpenCV): " << frame.cols << "x" << frame.rows 
                  << " quality=" << quality << " (" << buffer.size() << " bytes)" << std::endl;
        
        return true;
    }
    catch (const cv::Exception& e)
    {
        lastError = std::string("OpenCV exception during encoding: ") + e.what();
        LOG_ERROR << lastError << std::endl;
        buffer.clear();
        return false;
    }
}

cv::Mat JpegOpenCVEncoder::decode(const std::vector<uint8_t>& jpegData)
{
    cv::Mat result;
    
    // Validate input data
    if (jpegData.empty())
    {
        lastError = "Cannot decode empty JPEG data";
        LOG_ERROR << lastError << std::endl;
        return result;
    }
    
    // Thread-safe decoding
    std::lock_guard<std::mutex> lock(encoderMutex);
    
    try
    {
        // Decode JPEG data
        result = cv::imdecode(jpegData, cv::IMREAD_COLOR);
        
        if (result.empty())
        {
            lastError = "OpenCV JPEG decoding failed";
            LOG_ERROR << lastError << std::endl;
            return result;
        }
        
        LOG_DEBUG << "Decoded JPEG (OpenCV): " << result.cols << "x" << result.rows 
                  << " (" << jpegData.size() << " bytes)" << std::endl;
    }
    catch (const cv::Exception& e)
    {
        lastError = std::string("OpenCV exception during decoding: ") + e.what();
        LOG_ERROR << lastError << std::endl;
        result.release();
    }
    
    return result;
}

} // namespace sanuwave
