#include "image_decoding.h"
#include "raw_bayer_decoding.h"
#include "turbo_jpeg_decoding.h"
#include "logger.h"

namespace sanuwave
{

    QImage ImageDecoding::decodeRawBayerToImage(const QByteArray& data, RawImageInfo& info)
    {
        LOG_FUNCTION_TIME();
        
        const uint8_t* rawData = reinterpret_cast<const uint8_t*>(data.constData());
        size_t rawSize = static_cast<size_t>(data.size());
        
        std::vector<uint8_t> rgb = RawBayerDecoder::decodeWithHeader(rawData, rawSize, info);
        
        if (rgb.empty())
        {
            LOG_ERROR << "Failed to decode raw Bayer image" << std::endl;
            return QImage();
        }
    
        LOG_TRACE << "Decoded raw Bayer: " << info.width << "x" << info.height 
                << " " << info.bitDepth << "-bit" << std::endl;
        
        // Convert RGB24 to QImage (Format_RGB888 or Format_RGB32)
        QImage image(info.width, info.height, QImage::Format_RGB32);
        
        const uint8_t* src = rgb.data();
        for (int y = 0; y < info.height; ++y)
        {
            uchar* scanLine = image.scanLine(y);
            const uint8_t* srcRow = src + y * info.width * 3;
        
        for (int x = 0; x < info.width; ++x)
        {
            scanLine[x * 4 + 0] = srcRow[x * 3 + 2];  // Blue
            scanLine[x * 4 + 1] = srcRow[x * 3 + 1];  // Green
            scanLine[x * 4 + 2] = srcRow[x * 3 + 0];  // Red
            scanLine[x * 4 + 3] = 0xFF;               // Alpha
        }
    }
    
    return image;
}


QPixmap ImageDecoding::decodeImage(const QByteArray& data)
{
    LOG_FUNCTION_TIME();

    // Check if this is uncompressed RGB
    if (data.startsWith("RGB|"))
    {
        return parseRgbImage(data);
    }
    
     // Check if this is raw Bayer
    if (data.startsWith("RAW|"))
    {
        return QPixmap::fromImage(decodeRawBayerToImage(data));
    }

    // Otherwise try JPEG/PNG
    QPixmap pixmap;
    if (!pixmap.loadFromData(data, "JPEG"))
    {
        if (!pixmap.loadFromData(data, "PNG"))
        {
            LOG_ERROR << "Failed to decode image data" << std::endl;
            return QPixmap();
        }
    }
    
    return pixmap;
}

QImage ImageDecoding::decodeToImage(const QByteArray& data)
{
    //LOG_FUNCTION_TIME();

    // Check if this is uncompressed RGB
    if (data.startsWith("RGB|"))
    {
        return parseRgbToImage(data);
    }

     // Check if this is raw Bayer
    if (data.startsWith("RAW|"))
    {
        return decodeRawBayerToImage(data);
    }
    
    
    // Otherwise try JPEG/PNG
    QImage image;
    if (!image.loadFromData(data, "JPEG"))
    {
        if (!image.loadFromData(data, "PNG"))
        {
            LOG_ERROR << "Failed to decode image data" << std::endl;
            return QImage();
        }
    }
    
    return image;
}

QImage ImageDecoding::decodeJpegToImageWithTurboJpeg(const QByteArray& data)
{
 
    TurboJpegDecoder& decoder = TurboJpegDecoder::getInstance();
    
    if (!decoder.isInitialized())
    {
        LOG_ERROR << "TurboJPEG decoder not initialized" << std::endl;
        return QImage();
    }
    
    QImage image = decoder.decode(data);
    
    if (image.isNull())
    {
        LOG_ERROR << "TurboJPEG decode failed: " << decoder.getLastError() << std::endl;
    }
    
    return image;
}



QImage ImageDecoding::decodeRawBayerToImage(const QByteArray& data)
{
    RawImageInfo info;
    return decodeRawBayerToImage(data, info);
}

QPixmap ImageDecoding::parseRgbImage(const QByteArray& data)
{
    LOG_FUNCTION_TIME();

    // Parse "RGB|width|height|"
    int headerEnd = data.indexOf('|', 0);
    for (int i = 0; i < 2; i++)
    {
        headerEnd = data.indexOf('|', headerEnd + 1);
        if (headerEnd == -1) return QPixmap();
    }
    headerEnd++;
    
    QString header = QString::fromUtf8(data.left(headerEnd));
    QStringList parts = header.split('|');
    
    if (parts.size() < 3 || parts[0] != "RGB")
    {
        LOG_ERROR << "Invalid RGB header format" << std::endl;
        return QPixmap();
    }
    
    int width = parts[1].toInt();
    int height = parts[2].toInt();
    
    QByteArray imageBytes = data.mid(headerEnd);
    
    // Verify data size
    if (imageBytes.size() != width * height * 3)
    {
        LOG_ERROR << "RGB data size mismatch. Expected: " << (width * height * 3) 
                  << ", got: " << imageBytes.size() << std::endl;
        return QPixmap();
    }
    
    LOG_TRACE << "Parsing uncompressed RGB: " << width << "x" << height << std::endl;
    
    // Create QImage from RGB data
    QImage image(width, height, QImage::Format_RGB32);
    LOG_TRACE << "Successfully decoded uncompressed RGB image" << std::endl;
    const uchar* src = reinterpret_cast<const uchar*>(imageBytes.constData());
    for(int y = 0; y < height; ++y)
    {
        uchar* scanLine = image.scanLine(y);
        const uchar* srcRow = src + y * width * 3;

        for (int x = 0; x < width; ++x)
        {
            scanLine[x * 4 + 0] = srcRow[x * 3 + 2];  // Blue
            scanLine[x * 4 + 1] = srcRow[x * 3 + 1];  // Green
            scanLine[x * 4 + 2] = srcRow[x * 3 + 0];  // Red
            scanLine[x * 4 + 3] = 0xFF;             // Alpha
        }
    }

    // Copy the image data since QByteArray will go out of scope
    return QPixmap::fromImage(image.copy(), Qt::ImageConversionFlag::NoFormatConversion);
}

QImage ImageDecoding::parseRgbToImage(const QByteArray& data)
{
    LOG_FUNCTION_TIME();

    // Parse "RGB|width|height|"
    int headerEnd = data.indexOf('|', 0);
    for (int i = 0; i < 2; i++)
    {
        headerEnd = data.indexOf('|', headerEnd + 1);
        if (headerEnd == -1) return QImage();
    }
    headerEnd++;
    
    QString header = QString::fromUtf8(data.left(headerEnd));
    QStringList parts = header.split('|');
    
    if (parts.size() < 3 || parts[0] != "RGB")
    {
        LOG_ERROR << "Invalid RGB header format" << std::endl;
        return QImage();
    }
    
    int width = parts[1].toInt();
    int height = parts[2].toInt();
    
    QByteArray imageBytes = data.mid(headerEnd);
    
    // Verify data size
    if (imageBytes.size() != width * height * 3)
    {
        LOG_ERROR << "RGB data size mismatch. Expected: " << (width * height * 3) 
                  << ", got: " << imageBytes.size() << std::endl;
        return QImage();
    }
    
    LOG_TRACE << "Parsing uncompressed RGB: " << width << "x" << height << std::endl;
    
    // Create QImage from RGB data
    QImage image(width, height, QImage::Format_RGB32);
    const uchar* src = reinterpret_cast<const uchar*>(imageBytes.constData());
    uchar* dest = image.bits();
    int pixels = width * height;
    for (int y = 0; y < height; ++y)
    {
        uchar* scanLine = image.scanLine(y);
        const uchar* srcRow = src + y * width * 3;
        for (int x = 0; x < width; ++x)
        {
            scanLine[x * 4 + 0] = srcRow[x * 3 + 2];  // Blue
            scanLine[x * 4 + 1] = srcRow[x * 3 + 1];  // Green
            scanLine[x * 4 + 2] = srcRow[x * 3 + 0];  // Red
            scanLine[x * 4 + 3] = 0xFF;             // Alpha
        }
    }
    
    LOG_TRACE << "Successfully decoded uncompressed RGB image" << std::endl;

    // Copy the image data since QByteArray will go out of scope
    return image.copy();
}

}
