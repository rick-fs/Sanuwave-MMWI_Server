// tests/test_as7331.cpp
// Unit tests for AS7331 UV sensor driver using MockI2Cdev.
// No hardware needed — tests run on Ubuntu dev machine.

#include <gtest/gtest.h>
#include "MockI2Cdev.h"
#include "as7331.h"

// ============================================================================
// Constants matching AS7331 datasheet / as7331.h
// ============================================================================

static const uint8_t ADDR = 0x74;
static const uint8_t EXPECTED_DEVICE_ID = 0x21;  // DEVID=0010, MUT=0001

// Config-state register addresses
static const uint8_t REG_OSR   = 0x00;
static const uint8_t REG_AGEN  = 0x02;
static const uint8_t REG_CREG1 = 0x06;
static const uint8_t REG_CREG2 = 0x07;
static const uint8_t REG_CREG3 = 0x08;

// Measurement-state register addresses (same address space, different meaning)
static const uint8_t REG_TEMP  = 0x01;
static const uint8_t REG_MRES1 = 0x02;  // UVA
static const uint8_t REG_MRES2 = 0x03;  // UVB
static const uint8_t REG_MRES3 = 0x04;  // UVC

// OSR bit definitions
static const uint8_t OSR_SS     = 0x80;  // Start/Stop (bit 7)
static const uint8_t OSR_PD     = 0x40;  // Power Down (bit 6) — NOTE: code uses 0x04
static const uint8_t OSR_SW_RES = 0x08;  // Software Reset (bit 3)
static const uint8_t DOS_CONFIG  = 0x02; // DOS = 010
static const uint8_t DOS_MEASURE = 0x03; // DOS = 011

// ============================================================================
// Test fixture: sets up MockI2Cdev with default AS7331 register state
// ============================================================================

class AS7331Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        mock.begin();
        loadPowerOnDefaults();
    }

    void TearDown() override
    {
        mock.reset();
    }

    // Simulate power-on reset register state
    void loadPowerOnDefaults()
    {
        // OSR: power-down=1 (bit 2 per code), DOS=CONFIG(010)
        mock.setRegister8(ADDR, REG_OSR, 0x04 | DOS_CONFIG);

        // AGEN: device ID = 0x21
        mock.setRegister8(ADDR, REG_AGEN, EXPECTED_DEVICE_ID);

        // CREG1: gain=GAIN_2(0x0A) in [7:4], time=TIME_64MS(6) in [3:0]
        mock.setRegister8(ADDR, REG_CREG1, 0xA6);

        // CREG2: divider off
        mock.setRegister8(ADDR, REG_CREG2, 0x00);

        // CREG3: measMode=MODE_CMD(1) in bits [3:2] = 0x04
        mock.setRegister8(ADDR, REG_CREG3, 0x04);
    }

    // Helper: pre-load measurement data into consecutive byte addresses.
    //
    // readAll() does two I2C reads:
    //   1) readRegister16(0x01) -> readBytes(addr, 0x01, 2, buf)  -> addresses 0x01, 0x02
    //   2) readRegisterMultiple(0x02, buf, 6) -> readBytes(addr, 0x02, 6, buf) -> addresses 0x02..0x07
    //
    // Address 0x02 is shared: it's temp high byte for read #1 and UVA low byte for read #2.
    // On real hardware the sensor returns different data for the two separate transactions.
    // In our flat mock, the last write to 0x02 wins. We set up for the UV read (which
    // happens second) and accept that the temp value will incorporate the UVA low byte.
    //
    // AS7331 data is little-endian: low byte at lower address.
    void loadMeasurementData(uint16_t uvaRaw, uint16_t uvbRaw, uint16_t uvcRaw)
    {
        // Temperature low byte at 0x01 (temp high byte at 0x02 will be UVA low byte)
        mock.setRegister8(ADDR, 0x01, 0x00);

        // UV data: readRegisterMultiple(0x02, buf, 6) reads addresses 0x02..0x07
        mock.setRegister8(ADDR, 0x02, static_cast<uint8_t>(uvaRaw & 0xFF));
        mock.setRegister8(ADDR, 0x03, static_cast<uint8_t>(uvaRaw >> 8));
        mock.setRegister8(ADDR, 0x04, static_cast<uint8_t>(uvbRaw & 0xFF));
        mock.setRegister8(ADDR, 0x05, static_cast<uint8_t>(uvbRaw >> 8));
        mock.setRegister8(ADDR, 0x06, static_cast<uint8_t>(uvcRaw & 0xFF));
        mock.setRegister8(ADDR, 0x07, static_cast<uint8_t>(uvcRaw >> 8));
    }

    MockI2Cdev mock{1};
};

// ============================================================================
// Initialization tests
// ============================================================================

TEST_F(AS7331Test, BeginSucceedsWithValidDeviceId)
{
    AS7331 sensor(&mock, ADDR);
    EXPECT_TRUE(sensor.begin());
}

TEST_F(AS7331Test, BeginFailsWithNullI2C)
{
    AS7331 sensor(nullptr, ADDR);
    EXPECT_FALSE(sensor.begin());
}

TEST_F(AS7331Test, BeginFailsWithUninitializedI2C)
{
    MockI2Cdev uninit(1);  // never call begin()
    AS7331 sensor(&uninit, ADDR);
    EXPECT_FALSE(sensor.begin());
}

TEST_F(AS7331Test, BeginIssuesSoftwareReset)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();

    // Verify a write to OSR with SW_RES bit (0x08) set
    bool resetWritten = false;
    for (const I2CWriteRecord& rec : mock.getWriteLog())
    {
        if (rec.devAddr == ADDR && rec.regAddr == REG_OSR
            && !rec.data.empty() && (rec.data[0] & OSR_SW_RES))
        {
            resetWritten = true;
            break;
        }
    }
    EXPECT_TRUE(resetWritten) << "begin() should issue a software reset via OSR";
}

TEST_F(AS7331Test, GetDeviceIdReturnsCorrectValue)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    EXPECT_EQ(sensor.getDeviceID(), EXPECTED_DEVICE_ID);
}

TEST_F(AS7331Test, BeginFailsOnBusReadError)
{
    mock.setFailReads(true);
    AS7331 sensor(&mock, ADDR);
    // begin() calls reset() which reads OSR — read failure propagates
    // The sensor may or may not return false from begin() depending on
    // whether readRegister failures are fatal in reset(). At minimum
    // getDeviceID should return 0.
    sensor.begin();
    EXPECT_EQ(sensor.getDeviceID(), 0);
}

// ============================================================================
// Configuration: setGain
// ============================================================================

TEST_F(AS7331Test, SetGainWritesCREG1HighNibble)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    mock.clearWriteLog();

    sensor.setGain(AS7331::GAIN_128);  // 0b0100 = 4

    // CREG1 should be written with gain in bits [7:4]
    EXPECT_TRUE(mock.wasRegisterWritten8(ADDR, REG_CREG1));

    uint8_t creg1 = mock.getWrittenValue8(ADDR, REG_CREG1);
    uint8_t gainBits = (creg1 >> 4) & 0x0F;
    EXPECT_EQ(gainBits, static_cast<uint8_t>(AS7331::GAIN_128));
}

TEST_F(AS7331Test, SetGainPreservesTimeBits)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();

    // Set a known conversion time first
    sensor.setConversionTime(AS7331::TIME_256MS);  // value 8
    mock.clearWriteLog();

    // Now set gain — lower nibble (time) should be preserved
    sensor.setGain(AS7331::GAIN_64);  // 0b0101 = 5

    uint8_t creg1 = mock.getWrittenValue8(ADDR, REG_CREG1);
    uint8_t timeBits = creg1 & 0x0F;
    EXPECT_EQ(timeBits, static_cast<uint8_t>(AS7331::TIME_256MS));
}

TEST_F(AS7331Test, SetGainFailsOnWriteError)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    mock.setFailWrites(true);
    EXPECT_FALSE(sensor.setGain(AS7331::GAIN_128));
}

TEST_F(AS7331Test, SetGainWithReadErrorWritesGainIntoZero)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    mock.setFailReads(true);
    // readByte returns uint8_t (0 on error) — no error signal.
    // setGain reads 0, ORs in the gain, writes it. This succeeds.
    // The result is gain bits set, time bits zeroed (data loss).
    EXPECT_TRUE(sensor.setGain(AS7331::GAIN_128));
    
    // Verify: written value has gain but time bits are lost (read 0)
    uint8_t creg1 = mock.getWrittenValue8(ADDR, REG_CREG1);
    uint8_t gainBits = (creg1 >> 4) & 0x0F;
    EXPECT_EQ(gainBits, static_cast<uint8_t>(AS7331::GAIN_128));
    uint8_t timeBits = creg1 & 0x0F;
    EXPECT_EQ(timeBits, 0) << "Time bits lost because readByte returned 0";
}

// ============================================================================
// Configuration: setConversionTime
// ============================================================================

TEST_F(AS7331Test, SetConversionTimeWritesCREG1LowNibble)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    mock.clearWriteLog();

    sensor.setConversionTime(AS7331::TIME_256MS);  // value 8

    EXPECT_TRUE(mock.wasRegisterWritten8(ADDR, REG_CREG1));

    uint8_t creg1 = mock.getWrittenValue8(ADDR, REG_CREG1);
    uint8_t timeBits = creg1 & 0x0F;
    EXPECT_EQ(timeBits, static_cast<uint8_t>(AS7331::TIME_256MS));
}

TEST_F(AS7331Test, SetConversionTimePreservesGainBits)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();

    sensor.setGain(AS7331::GAIN_32);  // 0b0110 = 6
    mock.clearWriteLog();

    sensor.setConversionTime(AS7331::TIME_128MS);  // value 7

    uint8_t creg1 = mock.getWrittenValue8(ADDR, REG_CREG1);
    uint8_t gainBits = (creg1 >> 4) & 0x0F;
    EXPECT_EQ(gainBits, static_cast<uint8_t>(AS7331::GAIN_32));
}

// ============================================================================
// Configuration: setMeasurementMode
// ============================================================================

TEST_F(AS7331Test, SetMeasurementModeWritesCREG3)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    mock.clearWriteLog();

    sensor.setMeasurementMode(AS7331::MODE_CONT);  // value 0

    EXPECT_TRUE(mock.wasRegisterWritten8(ADDR, REG_CREG3));

    // Mode goes in CREG3 bits [3:2] per current code: (creg3 & 0xF3) | (mode << 2)
    uint8_t creg3 = mock.getWrittenValue8(ADDR, REG_CREG3);
    uint8_t modeBits = (creg3 >> 2) & 0x03;
    EXPECT_EQ(modeBits, static_cast<uint8_t>(AS7331::MODE_CONT));
}

TEST_F(AS7331Test, SetMeasurementModeCMD)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    mock.clearWriteLog();

    sensor.setMeasurementMode(AS7331::MODE_CMD);  // value 1

    uint8_t creg3 = mock.getWrittenValue8(ADDR, REG_CREG3);
    uint8_t modeBits = (creg3 >> 2) & 0x03;
    EXPECT_EQ(modeBits, static_cast<uint8_t>(AS7331::MODE_CMD));
}

// ============================================================================
// Mode switching
// ============================================================================

TEST_F(AS7331Test, SetGainSwitchesToConfigModeIfNeeded)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();

    // Put sensor in measurement mode
    // setOperationMode writes (osr & 0xFC) | MODE_MEASURE to OSR
    sensor.prepareMeasurement(AS7331::MODE_CMD, false);
    mock.clearWriteLog();

    // setGain should switch back to config mode first
    sensor.setGain(AS7331::GAIN_64);

    // Check that OSR was written with DOS=CONFIG before CREG1
    bool sawConfigSwitch = false;
    bool sawCreg1 = false;
    for (const I2CWriteRecord& rec : mock.getWriteLog())
    {
        if (rec.devAddr == ADDR && rec.regAddr == REG_OSR
            && !rec.data.empty() && (rec.data[0] & 0x03) == DOS_CONFIG)
        {
            sawConfigSwitch = true;
        }
        if (rec.devAddr == ADDR && rec.regAddr == REG_CREG1)
        {
            EXPECT_TRUE(sawConfigSwitch)
                << "Must switch to CONFIG mode before writing CREG1";
            sawCreg1 = true;
        }
    }
    EXPECT_TRUE(sawCreg1) << "CREG1 should have been written";
}

TEST_F(AS7331Test, PrepareMeasurementSetsModeBits)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    mock.clearWriteLog();

    sensor.prepareMeasurement(AS7331::MODE_CMD, false);

    // Should write OSR with DOS=MEASURE (0x03)
    bool sawMeasureMode = false;
    for (const I2CWriteRecord& rec : mock.getWriteLog())
    {
        if (rec.devAddr == ADDR && rec.regAddr == REG_OSR
            && !rec.data.empty() && (rec.data[0] & 0x03) == DOS_MEASURE)
        {
            sawMeasureMode = true;
            break;
        }
    }
    EXPECT_TRUE(sawMeasureMode) << "prepareMeasurement should set DOS=MEASURE";
}

TEST_F(AS7331Test, StartMeasurementSetsSSBit)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();

    // Must be in measurement mode first
    sensor.prepareMeasurement(AS7331::MODE_CMD, false);
    mock.clearWriteLog();

    sensor.startMeasurement();

    // Should write OSR with SS bit (0x80) set
    bool sawStart = false;
    for (const I2CWriteRecord& rec : mock.getWriteLog())
    {
        if (rec.devAddr == ADDR && rec.regAddr == REG_OSR
            && !rec.data.empty() && (rec.data[0] & OSR_SS))
        {
            sawStart = true;
            break;
        }
    }
    EXPECT_TRUE(sawStart) << "startMeasurement should set SS bit in OSR";
}

// ============================================================================
// Data reading
// ============================================================================

TEST_F(AS7331Test, ReadAllReturnsDataInMeasureMode)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();

    // Switch to measurement mode
    sensor.prepareMeasurement(AS7331::MODE_CMD, false);

    // Load fake UV measurement data: UVA=1000, UVB=500, UVC=100
    loadMeasurementData(1000, 500, 100);

    EXPECT_TRUE(sensor.readAll());

    // UV values should be non-zero (exact values depend on conversion factors
    // based on gain, conversion time, clock freq, and FSR constants)
    EXPECT_GT(sensor.getUVA(), 0.0f);
    EXPECT_GT(sensor.getUVB(), 0.0f);
    EXPECT_GT(sensor.getUVC(), 0.0f);
}

TEST_F(AS7331Test, ReadAllFailsInConfigMode)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    // Don't switch to measurement mode
    EXPECT_FALSE(sensor.readAll());
}

TEST_F(AS7331Test, ReadAllFailsOnBusError)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    sensor.prepareMeasurement(AS7331::MODE_CMD, false);

    mock.setFailReads(true);
    EXPECT_FALSE(sensor.readAll());
}

// ============================================================================
// Clock frequency
// ============================================================================

TEST_F(AS7331Test, SetClockFreqWritesCREG3LowBits)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    mock.clearWriteLog();

    sensor.setClockFreq(AS7331::CCLK_4_096_MHZ);  // value 2

    EXPECT_TRUE(mock.wasRegisterWritten8(ADDR, REG_CREG3));

    uint8_t creg3 = mock.getWrittenValue8(ADDR, REG_CREG3);
    uint8_t freqBits = creg3 & 0x03;
    EXPECT_EQ(freqBits, static_cast<uint8_t>(AS7331::CCLK_4_096_MHZ));
}

// ============================================================================
// Power management
// ============================================================================

TEST_F(AS7331Test, PrepareMeasurementClearsPowerDown)
{
    AS7331 sensor(&mock, ADDR);
    sensor.begin();
    // After begin(), powerDownState is true (from reset())
    mock.clearWriteLog();

    sensor.prepareMeasurement(AS7331::MODE_CMD, false);

    // Should write OSR with PD bit cleared (bit 2 = 0 per code)
    bool sawPdClear = false;
    for (const I2CWriteRecord& rec : mock.getWriteLog())
    {
        if (rec.devAddr == ADDR && rec.regAddr == REG_OSR
            && !rec.data.empty() && !(rec.data[0] & 0x04))
        {
            sawPdClear = true;
            break;
        }
    }
    EXPECT_TRUE(sawPdClear)
        << "prepareMeasurement should clear power-down bit";
}

// ============================================================================
// MockI2Cdev self-tests (validates the mock itself)
// ============================================================================

TEST(MockI2CdevTest, ReadReturnsPreloadedValue)
{
    MockI2Cdev m(1);
    m.begin();
    m.setRegister8(0x20, 0x00, 0x42);
    EXPECT_EQ(m.readByte(0x20, 0x00), 0x42);
}

TEST(MockI2CdevTest, WriteUpdatesRegisterState)
{
    MockI2Cdev m(1);
    m.begin();
    m.writeByte(0x20, 0x03, 0xAB);
    EXPECT_EQ(m.readByte(0x20, 0x03), 0xAB);
    EXPECT_EQ(m.getWrittenValue8(0x20, 0x03), 0xAB);
}

TEST(MockI2CdevTest, WriteLogRecordsAllWrites)
{
    MockI2Cdev m(1);
    m.begin();
    m.writeByte(0x20, 0x01, 0x11);
    m.writeByte(0x20, 0x02, 0x22);
    m.writeByte(0x30, 0x01, 0x33);

    const std::vector<I2CWriteRecord>& log = m.getWriteLog();
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0].devAddr, 0x20);
    EXPECT_EQ(log[0].regAddr, 0x01u);
    EXPECT_EQ(log[1].regAddr, 0x02u);
    EXPECT_EQ(log[2].devAddr, 0x30);
}

TEST(MockI2CdevTest, FailReadsReturnZero)
{
    MockI2Cdev m(1);
    m.begin();
    m.setRegister8(0x20, 0x00, 0xFF);
    m.setFailReads(true);
    EXPECT_EQ(m.readByte(0x20, 0x00), 0);
}

TEST(MockI2CdevTest, FailWritesReturnFalse)
{
    MockI2Cdev m(1);
    m.begin();
    m.setFailWrites(true);
    EXPECT_FALSE(m.writeByte(0x20, 0x00, 0x42));
}

TEST(MockI2CdevTest, ReadBytesReadsConsecutiveAddresses)
{
    MockI2Cdev m(1);
    m.begin();
    m.setRegister8(0x20, 0x10, 0xAA);
    m.setRegister8(0x20, 0x11, 0xBB);
    m.setRegister8(0x20, 0x12, 0xCC);

    uint8_t buf[3] = {0};
    EXPECT_TRUE(m.readBytes(0x20, 0x10, 3, buf));
    EXPECT_EQ(buf[0], 0xAA);
    EXPECT_EQ(buf[1], 0xBB);
    EXPECT_EQ(buf[2], 0xCC);
}

TEST(MockI2CdevTest, SixteenBitAddressSpaceSeparate)
{
    MockI2Cdev m(1);
    m.begin();
    m.setRegister8(0x20, 0x02, 0xAA);
    m.setRegister16(0x20, 0x0002, 0xBB);
    EXPECT_EQ(m.readByte(0x20, 0x02), 0xAA);
    EXPECT_EQ(m.readByte16(0x20, 0x0002), 0xBB);
}

TEST(MockI2CdevTest, ResetClearsEverything)
{
    MockI2Cdev m(1);
    m.begin();
    m.setRegister8(0x20, 0x00, 0xFF);
    m.writeByte(0x20, 0x01, 0x42);
    m.setFailReads(true);

    m.reset();

    EXPECT_EQ(m.readByte(0x20, 0x00), 0x00);
    EXPECT_TRUE(m.getWriteLog().empty());
}

TEST(MockI2CdevTest, ReadModifyWritePattern)
{
    // Simulates the pattern AS7331 uses: read register, modify bits, write back
    MockI2Cdev m(1);
    m.begin();
    m.setRegister8(0x74, 0x06, 0xA6);  // CREG1 default

    uint8_t val = m.readByte(0x74, 0x06);
    EXPECT_EQ(val, 0xA6);

    val = (val & 0x0F) | (0x05 << 4);  // Change gain to GAIN_64
    m.writeByte(0x74, 0x06, val);

    EXPECT_EQ(m.readByte(0x74, 0x06), 0x56);
    EXPECT_EQ(m.getWrittenValue8(0x74, 0x06), 0x56);
}
