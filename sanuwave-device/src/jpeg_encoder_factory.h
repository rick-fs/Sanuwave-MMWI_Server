// ============================================================================
// jpeg_encoder_factory.h
// ============================================================================
#ifndef JPEG_ENCODER_FACTORY_H
#define JPEG_ENCODER_FACTORY_H

#include "ijpeg_encoder.h"
#include <memory>

namespace sanuwave
{

/**
 * @brief Enumeration of available JPEG encoder types
 */
enum class JpegEncoderType
{
    TURBO_JPEG,     ///< High-performance TurboJPEG encoder
    OPENCV          ///< Standard OpenCV encoder
};

/**
 * @brief Factory class for creating JPEG encoder instances
 */
class JpegEncoderFactory
{
public:
    /**
     * @brief Create a JPEG encoder instance
     * 
     * @param type The type of encoder to create
     * @return Unique pointer to the encoder instance, or nullptr on failure
     */
    static std::unique_ptr<IJpegEncoder> createEncoder(JpegEncoderType type);
    
    /**
     * @brief Create the default JPEG encoder (TurboJPEG if available, OpenCV otherwise)
     * 
     * @return Unique pointer to the encoder instance, or nullptr on failure
     */
    static std::unique_ptr<IJpegEncoder> createDefaultEncoder();
    
    /**
     * @brief Check if a specific encoder type is available
     * 
     * @param type The encoder type to check
     * @return true if the encoder is available, false otherwise
     */
    static bool isEncoderAvailable(JpegEncoderType type);
    
    /**
     * @brief Get a string description of an encoder type
     * 
     * @param type The encoder type
     * @return String description
     */
    static std::string getEncoderName(JpegEncoderType type);
    
private:
    // Private constructor - factory class should not be instantiated
    JpegEncoderFactory() = default;
};

} // namespace sanuwave

#endif // JPEG_ENCODER_FACTORY_H
