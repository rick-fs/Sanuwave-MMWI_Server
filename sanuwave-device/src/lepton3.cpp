// src/lepton3.cpp
#include "lepton3.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <iostream>
#include <errno.h>

extern "C" {
#include "LEPTON_SDK.h"
#include "LEPTON_SYS.h"
#include "LEPTON_OEM.h"
}

Lepton3::Lepton3()
    : portOpen(false)
    , spiFd(-1)
    , spiSpeed(20000000)
    , spiMode(SPI_MODE_3)
    , spiBits(8)
    , initialized(false)
    , firstCapture(true)
{
    memset(&portDesc, 0, sizeof(portDesc));
}

Lepton3::~Lepton3()
{
    end();
}

bool Lepton3::begin(int i2cBus, const std::string& spiDevice)
{
    std::cout << "[Lepton3] Initializing on I2C bus " << i2cBus 
              << ", SPI: " << spiDevice << std::endl;

    // Open CCI port using SDK
    LEP_RESULT result = LEP_OpenPort(i2cBus, LEP_CCI_TWI, 400, &portDesc);
    if (result != LEP_OK)
    {
        setError("LEP_OpenPort failed with code " + std::to_string(result));
        return false;
    }
    portOpen = true;

    // Initialize SPI for VoSPI
    if (!openSPI(spiDevice))
    {
        LEP_ClosePort(&portDesc);
        portOpen = false;
        return false;
    }

    // Test frame capture
    uint16_t testFrame[WIDTH * HEIGHT];
    if (!captureFrame(testFrame))
    {
        setError("Failed to capture test frame");
        closeSPI();
        LEP_ClosePort(&portDesc);
        portOpen = false;
        return false;
    }

    initialized = true;
    std::cout << "[Lepton3] Initialized successfully (" << WIDTH << "x" << HEIGHT << ")" << std::endl;

    return true;
}

void Lepton3::end()
{
    if (initialized)
    {
        closeSPI();
        if (portOpen)
        {
            LEP_ClosePort(&portDesc);
            portOpen = false;
        }
        initialized = false;
    }
}

// ============================================================================
// SPI METHODS
// ============================================================================

bool Lepton3::openSPI(const std::string& device)
{
    spiFd = open(device.c_str(), O_RDWR);
    if (spiFd < 0)
    {
        setError("Failed to open SPI device: " + device + " (" + strerror(errno) + ")");
        return false;
    }

    if (ioctl(spiFd, SPI_IOC_WR_MODE, &spiMode) < 0)
    {
        setError("Failed to set SPI mode");
        close(spiFd);
        spiFd = -1;
        return false;
    }

    if (ioctl(spiFd, SPI_IOC_WR_BITS_PER_WORD, &spiBits) < 0)
    {
        setError("Failed to set SPI bits per word");
        close(spiFd);
        spiFd = -1;
        return false;
    }

    if (ioctl(spiFd, SPI_IOC_WR_MAX_SPEED_HZ, &spiSpeed) < 0)
    {
        setError("Failed to set SPI speed");
        close(spiFd);
        spiFd = -1;
        return false;
    }

    return true;
}

void Lepton3::closeSPI()
{
    if (spiFd >= 0)
    {
        close(spiFd);
        spiFd = -1;
    }
}

bool Lepton3::captureFrame(uint16_t* frameBuffer)
{
    if (spiFd < 0)
    {
        setError("SPI not initialized");
        return false;
    }

    // VoSPI sync on first capture only
    if (firstCapture)
    {
        usleep(200000);
        firstCapture = false;
    }

    static const int PACKETS_PER_FRAME = 60;
    static const int PACKET_SIZE_UINT16 = PACKET_SIZE / 2;
    static const int FRAME_SIZE_UINT16 = PACKET_SIZE_UINT16 * PACKETS_PER_FRAME;
    
    uint8_t result[PACKET_SIZE * PACKETS_PER_FRAME];
    uint8_t shelf[4][PACKET_SIZE * PACKETS_PER_FRAME];
    
    bool segmentCaptured[4] = {false, false, false, false};
    int segmentsDone = 0;
    int totalResets = 0;

    while (segmentsDone < NUM_SEGMENTS)
    {
        int resets = 0;
        int segmentNumber = -1;

        for (int j = 0; j < PACKETS_PER_FRAME; j++)
        {
            ssize_t bytesRead = read(spiFd, result + PACKET_SIZE * j, PACKET_SIZE);
            if (bytesRead != PACKET_SIZE)
            {
                j = -1;
                resets++;
                usleep(1000);
                if (resets == 750)
                {
                    setError("Too many read errors");
                    return false;
                }
                continue;
            }

            uint8_t byte0 = result[j * PACKET_SIZE];
            uint8_t byte1 = result[j * PACKET_SIZE + 1];
            
            // Check for discard packet
            if ((byte0 & 0x0F) == 0x0F)
            {
                j = -1;
                resets++;
                usleep(1000);
                if (resets == 750)
                {
                    setError("Too many discard packets");
                    return false;
                }
                continue;
            }
            
            int packetNumber = byte1;
            if (packetNumber != j)
            {
                j = -1;
                resets++;
                usleep(1000);
                if (resets == 750)
                {
                    setError("Too many sync errors");
                    return false;
                }
                continue;
            }

            if (packetNumber == 20)
            {
                segmentNumber = (byte0 >> 4) & 0x0F;
            }
        }

        totalResets += resets;

        if (segmentNumber >= 1 && segmentNumber <= 4)
        {
            int segIdx = segmentNumber - 1;
            if (!segmentCaptured[segIdx])
            {
                memcpy(shelf[segIdx], result, PACKET_SIZE * PACKETS_PER_FRAME);
                segmentCaptured[segIdx] = true;
                segmentsDone++;
            }
        }
        
        if (totalResets > 3000)
        {
            setError("Failed to capture all segments");
            return false;
        }
    }

    // Convert shelf data to frame buffer
    for (int seg = 0; seg < NUM_SEGMENTS; seg++)
    {
        int ofsRow = 30 * seg;
        
        for (int i = 0; i < FRAME_SIZE_UINT16; i++)
        {
            if ((i % PACKET_SIZE_UINT16) < 2)
            {
                continue;
            }
            
            uint16_t value = (static_cast<uint16_t>(shelf[seg][i * 2]) << 8) 
                           | shelf[seg][i * 2 + 1];
            
            int column = (i % PACKET_SIZE_UINT16) - 2 
                       + (WIDTH / 2) * ((i % (PACKET_SIZE_UINT16 * 2)) / PACKET_SIZE_UINT16);
            int row = i / PACKET_SIZE_UINT16 / 2 + ofsRow;
            
            if (row < HEIGHT && column < WIDTH)
            {
                frameBuffer[row * WIDTH + column] = value;
            }
        }
    }

    return true;
}

// ============================================================================
// SDK COMMAND WRAPPERS
// ============================================================================

bool Lepton3::runFFC()
{
    if (!portOpen)
    {
        setError("Port not open");
        return false;
    }

    LEP_RESULT result = LEP_RunSysFFCNormalization(&portDesc);
    if (result != LEP_OK)
    {
        setError("FFC failed with code " + std::to_string(result));
        return false;
    }

    return true;
}

bool Lepton3::reboot()
{
    if (!portOpen)
    {
        setError("Port not open");
        return false;
    }

    LEP_RESULT result = LEP_RunOemReboot(&portDesc);
    if (result != LEP_OK)
    {
        setError("Reboot failed with code " + std::to_string(result));
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    firstCapture = true;

    return true;
}

void Lepton3::setError(const std::string& error)
{
    lastError = error;
    std::cerr << "[Lepton3] ERROR: " << error << std::endl;
}
