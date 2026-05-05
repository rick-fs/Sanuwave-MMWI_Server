#ifndef FRAME_DATA_
#define FRAME_DATA_
#include <stdint.h>
#include <opencv2/opencv.hpp>

namespace sanuwave 
{


    struct FrameMetadata
    {
        int width = 0;
        int height = 0;
        uint64_t timestamp_ns = 0;
        int32_t exposureTime_us = 0;
        bool autoAnalogGain = true;
        float analogGain = 0.0f;
        float lensPosition = 0.0f;
        int32_t hblank = 0;
        int32_t vblank = 0;
        int64_t frameDuration_us = 0;
        double lineTime_us = 0.0;
        double rollingShutter_us = 0.0;        
      
        float digitalGain = 0.0f;
        float redGain = 0.0f;          // AWB red/blue gains (ColourGains)
        float blueGain = 0.0f;
        int32_t colourTemperature = 0; // Estimated CCT in Kelvin
        int32_t sensorBlackLevels[4] = {0, 0, 0, 0}; // Per-channel (R, Gr, Gb, B)
        bool blackLevelsValid = false;
        bool aeEnabled = false;        // Was AE active for this frame?
        bool awbEnabled = false;       // Was AWB active for this frame?
        bool valid = false;
    };

     struct FrameData
    {
        cv::Mat image;
        FrameMetadata metadata;
    };
}

#endif