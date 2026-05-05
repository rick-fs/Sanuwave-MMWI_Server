// ============================================================================
// jpeg_encoder_factory.cpp
// ============================================================================
#include "jpeg_encoder_factory.h"
#include "jpeg_turbo_encoder.h"
#include "jpeg_opencv_encoder.h"
#include "logger.h"

namespace sanuwave
{

std::unique_ptr<IJpegEncoder> JpegEncoderFactory::createEncoder(JpegEncoderType type)
{
    std::unique_ptr<IJpegEncoder> encoder;
    
    switch (type)
    {
        case JpegEncoderType::TURBO_JPEG:
        {
            try
            {
                encoder = std::make_unique<JpegTurboEncoder>();
                
                if (!encoder->isInitialized())
                {
                    LOG_ERROR << "Failed to initialize TurboJPEG encoder: " 
                              << encoder->getLastError() << std::endl;
                    encoder.reset();
                }
                else
                {
                    LOG_INFO << "Created TurboJPEG encoder" << std::endl;
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR << "Exception creating TurboJPEG encoder: " << e.what() << std::endl;
                encoder.reset();
            }
            break;
        }
        
        case JpegEncoderType::OPENCV:
        {
            try
            {
                encoder = std::make_unique<JpegOpenCVEncoder>();
                
                if (!encoder->isInitialized())
                {
                    LOG_ERROR << "Failed to initialize OpenCV encoder: " 
                              << encoder->getLastError() << std::endl;
                    encoder.reset();
                }
                else
                {
                    LOG_INFO << "Created OpenCV JPEG encoder" << std::endl;
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR << "Exception creating OpenCV encoder: " << e.what() << std::endl;
                encoder.reset();
            }
            break;
        }
        
        default:
            LOG_ERROR << "Unknown encoder type requested" << std::endl;
            break;
    }
    
    return encoder;
}

std::unique_ptr<IJpegEncoder> JpegEncoderFactory::createDefaultEncoder()
{
    // Try TurboJPEG first (faster)
    LOG_INFO << "Attempting to create default encoder (TurboJPEG preferred)" << std::endl;
    
    auto encoder = createEncoder(JpegEncoderType::TURBO_JPEG);
    
    if (encoder && encoder->isInitialized())
    {
        LOG_INFO << "Using TurboJPEG as default encoder" << std::endl;
        return encoder;
    }
    
    // Fall back to OpenCV
    LOG_WARNING << "TurboJPEG not available, falling back to OpenCV encoder" << std::endl;
    encoder = createEncoder(JpegEncoderType::OPENCV);
    
    if (encoder && encoder->isInitialized())
    {
        LOG_INFO << "Using OpenCV as default encoder" << std::endl;
        return encoder;
    }
    
    LOG_ERROR << "Failed to create any JPEG encoder" << std::endl;
    return nullptr;
}

bool JpegEncoderFactory::isEncoderAvailable(JpegEncoderType type)
{
    auto encoder = createEncoder(type);
    return encoder && encoder->isInitialized();
}

std::string JpegEncoderFactory::getEncoderName(JpegEncoderType type)
{
    switch (type)
    {
        case JpegEncoderType::TURBO_JPEG:
            return "TurboJPEG";
        case JpegEncoderType::OPENCV:
            return "OpenCV";
        default:
            return "Unknown";
    }
}

} // namespace sanuwave
