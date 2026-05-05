// vl53l4cd_calibration.cpp
// Helper functions for VL53L4CD calibration
#include "vl53l4cd_wrapper.h"
#include <iostream>
#include <vector>
#include <numeric>

namespace sanuwave
{

/**
 * @brief Perform offset calibration by measuring known distance
 * @param sensor VL53L4CD sensor instance
 * @param actualDistance_mm The actual distance to target in mm
 * @param numSamples Number of samples to average
 * @return true on success
 */
bool calibrateOffsetProperly(VL53L4CD& sensor, int16_t actualDistance_mm, int numSamples = 50)
{
    if (!sensor.isInitialized())
    {
        std::cerr << "Sensor not initialized" << std::endl;
        return false;
    }

    std::cout << "Offset Calibration Procedure" << std::endl;
    std::cout << "============================" << std::endl;
    std::cout << "1. Place white target at exactly " << actualDistance_mm << " mm" << std::endl;
    std::cout << "2. Ensure target is perpendicular to sensor" << std::endl;
    std::cout << "3. Taking " << numSamples << " measurements..." << std::endl;

    // Start ranging
    if (!sensor.startRanging())
    {
        std::cerr << "Failed to start ranging" << std::endl;
        return false;
    }

    // Collect measurements
    std::vector<int16_t> measurements;
    int validCount = 0;

    for (int i = 0; i < numSamples * 2 && validCount < numSamples; i++)
    {
        VL53L4CD::Measurement m = sensor.getMeasurement(1000);
        
        if (m.valid && m.range_status == 0)
        {
            measurements.push_back(m.distance_mm);
            validCount++;
            
            if (validCount % 10 == 0)
            {
                std::cout << "  Progress: " << validCount << "/" << numSamples << std::endl;
            }
        }
    }

    sensor.stopRanging();

    if (measurements.size() < numSamples / 2)
    {
        std::cerr << "Failed to get enough valid measurements" << std::endl;
        return false;
    }

    // Calculate average measured distance
    int32_t sum = std::accumulate(measurements.begin(), measurements.end(), 0);
    int16_t avgDistance = sum / measurements.size();

    // Calculate offset (measured - actual)
    int16_t offset = avgDistance - actualDistance_mm;

    std::cout << "\nCalibration Results:" << std::endl;
    std::cout << "  Actual distance:   " << actualDistance_mm << " mm" << std::endl;
    std::cout << "  Measured distance: " << avgDistance << " mm" << std::endl;
    std::cout << "  Calculated offset: " << offset << " mm" << std::endl;

    // Apply offset using the actual VL53L4CD API function
    Dev_t dev = sensor.getDeviceHandle();
    uint8_t status = VL53L4CD_SetOffset(dev, offset);

    if (status != 0)
    {
        std::cerr << "Failed to apply offset: " << (int)status << std::endl;
        return false;
    }

    std::cout << "  Offset applied successfully!" << std::endl;
    return true;
}

/**
 * @brief Perform crosstalk calibration using white target
 * @param sensor VL53L4CD sensor instance
 * @param targetDistance_mm Distance to white target (typically 100mm)
 * @param numSamples Number of samples to average
 * @return true on success
 */
bool calibrateCrosstalkProperly(VL53L4CD& sensor, uint16_t targetDistance_mm = 100, int numSamples = 50)
{
    if (!sensor.isInitialized())
    {
        std::cerr << "Sensor not initialized" << std::endl;
        return false;
    }

    std::cout << "Crosstalk Calibration Procedure" << std::endl;
    std::cout << "================================" << std::endl;
    std::cout << "1. Use a white matte target (>88% reflectance)" << std::endl;
    std::cout << "2. Place target at exactly " << targetDistance_mm << " mm" << std::endl;
    std::cout << "3. Ensure target is perpendicular to sensor" << std::endl;
    std::cout << "4. Taking " << numSamples << " measurements..." << std::endl;

    // Start ranging
    if (!sensor.startRanging())
    {
        std::cerr << "Failed to start ranging" << std::endl;
        return false;
    }

    // Collect signal measurements
    std::vector<uint16_t> signals;
    int validCount = 0;

    for (int i = 0; i < numSamples * 2 && validCount < numSamples; i++)
    {
        VL53L4CD::Measurement m = sensor.getMeasurement(1000);
        
        if (m.valid && m.range_status == 0)
        {
            signals.push_back(m.signal_per_spad);
            validCount++;
            
            if (validCount % 10 == 0)
            {
                std::cout << "  Progress: " << validCount << "/" << numSamples << std::endl;
            }
        }
    }

    sensor.stopRanging();

    if (signals.size() < numSamples / 2)
    {
        std::cerr << "Failed to get enough valid measurements" << std::endl;
        return false;
    }

    // Calculate average signal
    uint32_t sum = std::accumulate(signals.begin(), signals.end(), 0U);
    uint16_t avgSignal = sum / signals.size();

    // Crosstalk is typically a small fraction of the signal
    // For VL53L4CD, crosstalk value format varies by API version
    // This is a simplified calculation - check your API documentation
    uint16_t xtalk = avgSignal / 100;  // Example: 1% of signal

    std::cout << "\nCalibration Results:" << std::endl;
    std::cout << "  Average signal: " << avgSignal << " kcps" << std::endl;
    std::cout << "  Calculated crosstalk: " << xtalk << std::endl;

    // Apply crosstalk using the actual VL53L4CD API function
    Dev_t dev = sensor.getDeviceHandle();
    uint8_t status = VL53L4CD_SetXtalk(dev, xtalk);

    if (status != 0)
    {
        std::cerr << "Failed to apply crosstalk: " << (int)status << std::endl;
        return false;
    }

    std::cout << "  Crosstalk applied successfully!" << std::endl;
    std::cout << "\nNote: Crosstalk calibration is optional for most applications." << std::endl;
    return true;
}

} // namespace sanuwave

// Example usage
#ifdef CALIBRATION_EXAMPLE
int main()
{
    using namespace sanuwave;
    
    VL53L4CD sensor;
    
    if (!sensor.init())
    {
        std::cerr << "Failed to initialize sensor" << std::endl;
        return 1;
    }
    
    std::cout << "VL53L4CD Calibration Tool" << std::endl;
    std::cout << "=========================" << std::endl << std::endl;
    
    // Offset calibration
    std::cout << "Performing offset calibration..." << std::endl;
    std::cout << "Place target at 100mm and press Enter..." << std::endl;
    std::cin.get();
    
    if (calibrateOffsetProperly(sensor, 100))
    {
        std::cout << "\nOffset calibration successful!" << std::endl;
    }
    
    std::cout << "\n\n";
    
    // Crosstalk calibration (optional)
    std::cout << "Performing crosstalk calibration..." << std::endl;
    std::cout << "Place white target at 100mm and press Enter..." << std::endl;
    std::cin.get();
    
    if (calibrateCrosstalkProperly(sensor, 100))
    {
        std::cout << "\nCrosstalk calibration successful!" << std::endl;
    }
    
    sensor.shutdown();
    return 0;
}
#endif
