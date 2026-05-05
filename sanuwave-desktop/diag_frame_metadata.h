#pragma once
#include <cstdint>

namespace sanuwave {
namespace protocol {

struct DiagFrameMetadata {
    // Common fields (all cameras)
    uint32_t actualExposureUs     = 0;
    float    actualAnalogGain     = 0.0f;
    float    actualDigitalGain    = 0.0f;
    float    colourGains[2]       = {0.0f, 0.0f};
    uint32_t colourTemperature    = 0;
    int32_t  sensorBlackLevels[4] = {0, 0, 0, 0};
    bool     blackLevelsValid     = false;
    uint64_t sensorTimestampNs    = 0;
    bool     lensShadingApplied   = false;
    bool     aeEnabled            = false;
    bool     awbEnabled           = false;
    int32_t  hblank               = 0;
    int32_t  vblank               = 0;
    int64_t  frameDurationUs      = 0;

    // Lepton-specific (only valid when camera == "thermal")
    float    fpaTemperatureK  = 0.0f;
    float    auxTemperatureK  = 0.0f;
    uint16_t rawMin           = 0;
    uint16_t rawMax           = 0;
    bool     ffcDesired       = false;
    uint32_t ffcFramesSince   = 0;
    uint8_t  gainMode         = 0;
    bool     agcEnabled       = false;
    bool     radiometryEnabled = false;
};

} // namespace protocol
} // namespace sanuwave