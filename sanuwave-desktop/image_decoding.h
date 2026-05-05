#ifndef IMAGE_DECODING_
#define IMAGE_DECODING_
#include <QPixmap>
#include <QByteArray>
#include <QString>
#include <QImage>
#include "raw_bayer_decoding.h"

namespace sanuwave
{

class ImageDecoding
{
public:
    // Main decoding function - automatically detects format
    static QPixmap decodeImage(const QByteArray& data);
    
    // Decode to QImage instead of QPixmap
    static QImage decodeToImage(const QByteArray& data);
    
    // Decode JPEG to QImage using TurboJPEG (faster than Qt's decoder)
    static QImage decodeJpegToImageWithTurboJpeg(const QByteArray& data);

    static QImage decodeRawBayerToImage(const QByteArray& data);
    
    // Decode raw Bayer and return metadata
    static QImage decodeRawBayerToImage(const QByteArray& data, RawImageInfo& info);
    
protected:
    // Parse uncompressed RGB with header
    static QPixmap parseRgbImage(const QByteArray& data);
    
    // Parse uncompressed RGB to QImage
    static QImage parseRgbToImage(const QByteArray& data);
};

} // namespace sanuwave

#endif // IMAGE_DECODING_
