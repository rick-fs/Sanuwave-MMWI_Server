// MockI2Cdev.h
// Mock implementation of II2Cdev for unit testing sensor drivers
// without physical I2C hardware.
//
// Usage:
//   MockI2Cdev mock(1);
//   mock.begin();
//   mock.setRegister8(0x74, 0x02, 0x21);   // AGEN register returns 0x21
//
//   AS7331 sensor(&mock, 0x74);             // Accepts II2Cdev*
//   sensor.begin();
//
//   auto writes = mock.getWriteLog();       // Verify what was written

#ifndef MOCK_I2CDEV_H
#define MOCK_I2CDEV_H

#include "II2Cdev.h"
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Record of a single I2C write operation
struct I2CWriteRecord
{
    uint8_t devAddr;
    uint32_t regAddr;    // Holds 8-bit or 16-bit register address
    bool is16bit;        // true if 16-bit register address was used
    std::vector<uint8_t> data;
};

class MockI2Cdev : public II2Cdev
{
public:
    explicit MockI2Cdev(int busNumber = 1)
        : _busNumber(busNumber)
        , _initialized(false)
        , _failReads(false)
        , _failWrites(false)
    {
    }

    ~MockI2Cdev() override = default;

    // ========================================================================
    // II2Cdev interface implementation
    // ========================================================================

    bool begin() override
    {
        _initialized = true;
        return true;
    }

    void end() override
    {
        _initialized = false;
    }

    bool isInitialized() const override { return _initialized; }

    // --- 8-bit register address reads ---

    uint8_t readByte(uint8_t devAddr, uint8_t regAddr) override
    {
        if (_failReads) return 0;
        uint32_t key = makeKey8(devAddr, regAddr);
        std::lock_guard<std::mutex> lock(_mutex);
        std::map<uint32_t, std::vector<uint8_t>>::iterator it = _registers.find(key);
        if (it != _registers.end() && !it->second.empty())
            return it->second[0];
        return 0;
    }

    bool readBytes(uint8_t devAddr, uint8_t regAddr, uint8_t count, uint8_t* dest) override
    {
        if (_failReads) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        for (uint8_t i = 0; i < count; ++i)
        {
            uint32_t key = makeKey8(devAddr, regAddr + i);
            std::map<uint32_t, std::vector<uint8_t>>::iterator it = _registers.find(key);
            dest[i] = (it != _registers.end() && !it->second.empty()) ? it->second[0] : 0;
        }
        return true;
    }

    // --- 16-bit register address reads ---

    uint8_t readByte16(uint8_t devAddr, uint16_t regAddr) override
    {
        if (_failReads) return 0;
        uint32_t key = makeKey16(devAddr, regAddr);
        std::lock_guard<std::mutex> lock(_mutex);
        std::map<uint32_t, std::vector<uint8_t>>::iterator it = _registers.find(key);
        if (it != _registers.end() && !it->second.empty())
            return it->second[0];
        return 0;
    }

    bool readBytes16(uint8_t devAddr, uint16_t regAddr, uint8_t count, uint8_t* dest) override
    {
        if (_failReads) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        for (uint8_t i = 0; i < count; ++i)
        {
            uint32_t key = makeKey16(devAddr, regAddr + i);
            std::map<uint32_t, std::vector<uint8_t>>::iterator it = _registers.find(key);
            dest[i] = (it != _registers.end() && !it->second.empty()) ? it->second[0] : 0;
        }
        return true;
    }

    // --- 8-bit register address writes ---

    bool writeByte(uint8_t devAddr, uint8_t regAddr, uint8_t data) override
    {
        if (_failWrites) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        uint32_t key = makeKey8(devAddr, regAddr);
        _registers[key] = { data };
        _writeLog.push_back({ devAddr, regAddr, false, { data } });
        return true;
    }

    bool writeBytes(uint8_t devAddr, uint8_t regAddr, uint8_t count, const uint8_t* src) override
    {
        if (_failWrites) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        std::vector<uint8_t> dataVec(src, src + count);
        for (uint8_t i = 0; i < count; ++i)
        {
            uint32_t key = makeKey8(devAddr, regAddr + i);
            _registers[key] = { src[i] };
        }
        _writeLog.push_back({ devAddr, regAddr, false, dataVec });
        return true;
    }

    // --- 16-bit register address writes ---

    bool writeByte16(uint8_t devAddr, uint16_t regAddr, uint8_t data) override
    {
        if (_failWrites) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        uint32_t key = makeKey16(devAddr, regAddr);
        _registers[key] = { data };
        _writeLog.push_back({ devAddr, regAddr, true, { data } });
        return true;
    }

    bool writeBytes16(uint8_t devAddr, uint16_t regAddr, uint16_t count, const uint8_t* src) override
    {
        if (_failWrites) return false;
        std::lock_guard<std::mutex> lock(_mutex);
        std::vector<uint8_t> dataVec(src, src + count);
        for (uint16_t i = 0; i < count; ++i)
        {
            uint32_t key = makeKey16(devAddr, regAddr + i);
            _registers[key] = { src[i] };
        }
        _writeLog.push_back({ devAddr, regAddr, true, dataVec });
        return true;
    }

    // --- Scan (no-op) ---

    void scan() override { }

    // ========================================================================
    // Mock control API (for test setup and verification)
    // ========================================================================

    // Pre-load a register value (8-bit address)
    void setRegister8(uint8_t devAddr, uint8_t regAddr, uint8_t value)
    {
        uint32_t key = makeKey8(devAddr, regAddr);
        _registers[key] = { value };
    }

    // Pre-load a register value (16-bit address)
    void setRegister16(uint8_t devAddr, uint16_t regAddr, uint8_t value)
    {
        uint32_t key = makeKey16(devAddr, regAddr);
        _registers[key] = { value };
    }

    // Pre-load multiple consecutive registers (8-bit base address)
    void setRegisters8(uint8_t devAddr, uint8_t startReg, const std::vector<uint8_t>& values)
    {
        for (size_t i = 0; i < values.size(); ++i)
        {
            uint32_t key = makeKey8(devAddr, startReg + static_cast<uint8_t>(i));
            _registers[key] = { values[i] };
        }
    }

    // Pre-load multiple consecutive registers (16-bit base address)
    void setRegisters16(uint8_t devAddr, uint16_t startReg, const std::vector<uint8_t>& values)
    {
        for (size_t i = 0; i < values.size(); ++i)
        {
            uint32_t key = makeKey16(devAddr, startReg + static_cast<uint16_t>(i));
            _registers[key] = { values[i] };
        }
    }

    // Read back current register state (8-bit address)
    uint8_t getWrittenValue8(uint8_t devAddr, uint8_t regAddr) const
    {
        uint32_t key = makeKey8(devAddr, regAddr);
        std::map<uint32_t, std::vector<uint8_t>>::const_iterator it = _registers.find(key);
        if (it != _registers.end() && !it->second.empty())
            return it->second[0];
        return 0;
    }

    // Read back current register state (16-bit address)
    uint8_t getWrittenValue16(uint8_t devAddr, uint16_t regAddr) const
    {
        uint32_t key = makeKey16(devAddr, regAddr);
        std::map<uint32_t, std::vector<uint8_t>>::const_iterator it = _registers.find(key);
        if (it != _registers.end() && !it->second.empty())
            return it->second[0];
        return 0;
    }

    // Get full write log for verification
    const std::vector<I2CWriteRecord>& getWriteLog() const { return _writeLog; }

    // Clear write log (keeps register state)
    void clearWriteLog() { _writeLog.clear(); }

    // Clear all register state and write log
    void reset()
    {
        _registers.clear();
        _writeLog.clear();
        _failReads = false;
        _failWrites = false;
    }

    // Force all reads to fail (simulates bus error)
    void setFailReads(bool fail) { _failReads = fail; }

    // Force all writes to fail (simulates bus error)
    void setFailWrites(bool fail) { _failWrites = fail; }

    // Check if a specific register was written to
    bool wasRegisterWritten8(uint8_t devAddr, uint8_t regAddr) const
    {
        for (const I2CWriteRecord& rec : _writeLog)
        {
            if (rec.devAddr == devAddr && !rec.is16bit && rec.regAddr == regAddr)
                return true;
        }
        return false;
    }

    bool wasRegisterWritten16(uint8_t devAddr, uint16_t regAddr) const
    {
        for (const I2CWriteRecord& rec : _writeLog)
        {
            if (rec.devAddr == devAddr && rec.is16bit && rec.regAddr == regAddr)
                return true;
        }
        return false;
    }

    int getBusNumber() const { return _busNumber; }

private:
    static uint32_t makeKey8(uint8_t devAddr, uint8_t regAddr)
    {
        return (static_cast<uint32_t>(devAddr) << 8) | regAddr;
    }

    static uint32_t makeKey16(uint8_t devAddr, uint16_t regAddr)
    {
        return (1u << 24) | (static_cast<uint32_t>(devAddr) << 16) | regAddr;
    }

    int _busNumber;
    bool _initialized;
    bool _failReads;
    bool _failWrites;
    std::mutex _mutex;
    std::map<uint32_t, std::vector<uint8_t>> _registers;
    std::vector<I2CWriteRecord> _writeLog;
};

#endif // MOCK_I2CDEV_H
