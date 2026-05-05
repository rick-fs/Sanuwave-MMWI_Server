// ============================================================================
// jpeg_turbo_encoder.h
// ============================================================================
#ifndef JPEG_TURBO_ENCODER_H
#define JPEG_TURBO_ENCODER_H

#include "ijpeg_encoder.h"
#include <mutex>
#include <turbojpeg.h>

namespace sanuwave
{

/**
 * @brief TurboJPEG implementation of JPEG encoder/decoder
 * 
 * High-performance JPEG encoding/decoding using libjpeg-turbo
 */
class JpegTurboEncoder : public IJpegEncoder
{
public:
    JpegTurboEncoder();
    ~JpegTurboEncoder() override;
    
    std::vector<uint8_t> encode(cv::Mat& image, int quality) override;
    bool imencode(cv::Mat& frame, std::vector<uint8_t>& buffer, int quality) override;
    cv::Mat decode(const std::vector<uint8_t>& jpegData) override;
    
    bool isInitialized() const override { return tjCompressor != nullptr && tjDecompressor != nullptr; }
    std::string getLastError() const override { return lastError; }
    
    // Delete copy constructor and assignment operator
    JpegTurboEncoder(const JpegTurboEncoder&) = delete;
    JpegTurboEncoder& operator=(const JpegTurboEncoder&) = delete;
    
private:
    tjhandle tjCompressor;
    tjhandle tjDecompressor;
    std::mutex encoderMutex;
    std::mutex decoderMutex;
    std::string lastError;
};

} // namespace sanuwave

#endif // JPEG_TURBO_ENCODER_H
