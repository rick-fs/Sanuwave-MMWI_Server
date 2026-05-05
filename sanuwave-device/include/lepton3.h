// include/lepton3.h
#pragma once

#include <string>
#include <cstdint>

// Forward declare SDK types
extern "C" {
#include "LEPTON_Types.h"
}

class Lepton3
{
public:
    // Lepton 3 specifications
    static constexpr int WIDTH = 160;
    static constexpr int HEIGHT = 120;
    static constexpr int PACKET_SIZE = 164;
    static constexpr int PACKET_HEADER_SIZE = 4;
    static constexpr int PIXELS_PER_PACKET = 80;
    static constexpr int PACKETS_PER_SEGMENT = 60;
    static constexpr int NUM_SEGMENTS = 4;
    static constexpr uint16_t DISCARD_PACKET_ID = 0x0FFF;

    Lepton3();
    ~Lepton3();

    // Initialization
    bool begin(int i2cBus, const std::string& spiDevice);
    void end();

    // Frame capture
    bool captureFrame(uint16_t* frameBuffer);

    // CCI commands via SDK
    bool runFFC();
    bool reboot();

    // Status
    bool isInitialized() const { return initialized; }
    std::string getLastError() const { return lastError; }

private:
    // SDK port descriptor
    LEP_CAMERA_PORT_DESC_T portDesc;
    bool portOpen;

    // SPI
    int spiFd;
    uint32_t spiSpeed;
    uint8_t spiMode;
    uint8_t spiBits;

    // State
    bool initialized;
    bool firstCapture;
    std::string lastError;

    // SPI methods
    bool openSPI(const std::string& device);
    void closeSPI();

    void setError(const std::string& error);
};
