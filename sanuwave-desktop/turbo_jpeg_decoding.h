// ============================================================================
// turbo_jpeg_decoding.h
// ============================================================================
#ifndef TURBOJPEG_DECODING_H
#define TURBOJPEG_DECODING_H

#include <vector>
#include <mutex>
#include <turbojpeg.h>
#include <QImage>
#include <QByteArray>

namespace sanuwave
{

/**
 * @brief Singleton class for TurboJPEG decoding
 * 
 * Manages a persistent TurboJPEG decompressor instance for efficient
 * JPEG decoding during streaming operations.
 */
class TurboJpegDecoder
{
public:
    /**
     * @brief Get the singleton instance
     */
    static TurboJpegDecoder& getInstance();
    
    /**
     * @brief Decode JPEG data to QImage
     * 
     * @param jpegData Input JPEG data as QByteArray
     * @return Decoded image as QImage, or null QImage on error
     */
    QImage decode(const QByteArray& jpegData);
    
    /**
     * @brief Get image dimensions without full decompression
     * 
     * @param jpegData Input JPEG data
     * @param width Output: image width
     * @param height Output: image height
     * @param subsamp Output: chroma subsampling
     * @param colorspace Output: colorspace
     * @return true on success, false on error
     */
    bool getImageInfo(const QByteArray& jpegData, 
                      int& width, int& height, 
                      int& subsamp, int& colorspace);
    
    /**
     * @brief Check if decoder is initialized
     */
    bool isInitialized() const { return tjDecompressor != nullptr; }
    
    /**
     * @brief Get last error message
     */
    std::string getLastError() const { return lastError; }
    
    // Delete copy constructor and assignment operator (singleton)
    TurboJpegDecoder(const TurboJpegDecoder&) = delete;
    TurboJpegDecoder& operator=(const TurboJpegDecoder&) = delete;
    
private:
    TurboJpegDecoder();
    ~TurboJpegDecoder();
    
    tjhandle tjDecompressor;
    std::mutex decoderMutex;  // Thread safety for decoding operations
    std::string lastError;
};

} // namespace sanuwave

#endif // TURBOJPEG_DECODING_H
