// tests/test_i2c_bus.cpp
// Unit tests for I2CBus singleton and its recursive_mutex locking behavior.
// No hardware needed — tests run on Ubuntu dev machine.

#include <gtest/gtest.h>
#include "i2c_bus.h"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <mutex>

using namespace sanuwave;

// ============================================================================
// Basic singleton behavior
// ============================================================================

TEST(I2CBusTest, SingletonReturnsSameInstance)
{
    I2CBus& bus1 = I2CBus::getInstance(1);
    I2CBus& bus2 = I2CBus::getInstance(1);
    EXPECT_EQ(&bus1, &bus2);
}

// ============================================================================
// Lock acquisition and release
// ============================================================================

TEST(I2CBusTest, LockBusReturnsValidLock)
{
    I2CBus& bus = I2CBus::getInstance(1);
    std::unique_lock<std::recursive_mutex> lock = bus.lockBus();
    EXPECT_TRUE(lock.owns_lock());
}

TEST(I2CBusTest, LockReleasesOnScopeExit)
{
    I2CBus& bus = I2CBus::getInstance(1);
    
    {
        std::unique_lock<std::recursive_mutex> lock = bus.lockBus();
        EXPECT_TRUE(lock.owns_lock());
    }
    // Lock should be released — verify by acquiring again without deadlock
    std::unique_lock<std::recursive_mutex> lock2 = bus.lockBus();
    EXPECT_TRUE(lock2.owns_lock());
}

// ============================================================================
// Recursive locking (critical for our architecture)
// ============================================================================

TEST(I2CBusTest, RecursiveLockDoesNotDeadlock)
{
    I2CBus& bus = I2CBus::getInstance(1);
    
    // Simulate: wrapper holds sequence lock, calls I2Cdev method which also locks
    std::unique_lock<std::recursive_mutex> outerLock = bus.lockBus();
    std::unique_lock<std::recursive_mutex> innerLock = bus.lockBus();
    
    EXPECT_TRUE(outerLock.owns_lock());
    EXPECT_TRUE(innerLock.owns_lock());
}

TEST(I2CBusTest, TripleRecursiveLock)
{
    I2CBus& bus = I2CBus::getInstance(1);
    
    // Simulate: wrapper → sensor driver → I2Cdev, all locking
    std::unique_lock<std::recursive_mutex> lock1 = bus.lockBus();
    std::unique_lock<std::recursive_mutex> lock2 = bus.lockBus();
    std::unique_lock<std::recursive_mutex> lock3 = bus.lockBus();
    
    EXPECT_TRUE(lock1.owns_lock());
    EXPECT_TRUE(lock2.owns_lock());
    EXPECT_TRUE(lock3.owns_lock());
}

// ============================================================================
// Thread safety — mutual exclusion
// ============================================================================

TEST(I2CBusTest, MutualExclusionBetweenThreads)
{
    I2CBus& bus = I2CBus::getInstance(1);
    
    // Shared state: if two threads are in the critical section simultaneously,
    // the counter will show it.
    std::atomic<int> concurrentCount{0};
    std::atomic<bool> violationDetected{false};
    
    constexpr int numThreads = 4;
    constexpr int iterationsPerThread = 100;
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < numThreads; t++)
    {
        threads.emplace_back([&bus, &concurrentCount, &violationDetected]() {
            for (int i = 0; i < iterationsPerThread; i++)
            {
                std::unique_lock<std::recursive_mutex> lock = bus.lockBus();
                
                int count = concurrentCount.fetch_add(1) + 1;
                if (count > 1)
                {
                    violationDetected.store(true);
                }
                
                // Simulate some I2C work
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                
                concurrentCount.fetch_sub(1);
            }
        });
    }
    
    for (std::thread& t : threads)
    {
        t.join();
    }
    
    EXPECT_FALSE(violationDetected.load()) 
        << "Multiple threads were in the critical section simultaneously";
    EXPECT_EQ(concurrentCount.load(), 0);
}

// ============================================================================
// Simulated sensor interleaving pattern
// ============================================================================

TEST(I2CBusTest, SequenceLockPreventsInterleaving)
{
    I2CBus& bus = I2CBus::getInstance(1);
    
    // Log of operations: each entry is (thread_id, step)
    // If sequences interleave, we'll see thread IDs mixed within a sequence
    std::mutex logMutex;
    std::vector<std::pair<int, std::string>> opLog;
    
    auto simulateSensor = [&](int sensorId, const std::string& name) {
        for (int i = 0; i < 10; i++)
        {
            // Hold sequence lock for multi-step operation
            std::unique_lock<std::recursive_mutex> lock = bus.lockBus();
            
            {
                std::lock_guard<std::mutex> lg(logMutex);
                opLog.push_back({sensorId, name + "_start"});
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            
            {
                std::lock_guard<std::mutex> lg(logMutex);
                opLog.push_back({sensorId, name + "_end"});
            }
        }
    };
    
    std::thread t1(simulateSensor, 1, "uv_read");
    std::thread t2(simulateSensor, 2, "distance_read");
    std::thread t3(simulateSensor, 3, "als_read");
    
    t1.join();
    t2.join();
    t3.join();
    
    // Verify: every _start must be immediately followed by matching _end
    // from the same sensor (no interleaving)
    for (size_t i = 0; i + 1 < opLog.size(); i += 2)
    {
        EXPECT_EQ(opLog[i].first, opLog[i + 1].first)
            << "Interleaving detected at log position " << i
            << ": " << opLog[i].second << " (sensor " << opLog[i].first << ")"
            << " followed by " << opLog[i + 1].second << " (sensor " << opLog[i + 1].first << ")";
    }
}