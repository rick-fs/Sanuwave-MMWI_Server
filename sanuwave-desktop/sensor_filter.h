// client/src/sensor_filter.h
// Copyright 2026 Sanuwave Medical LLC.
#ifndef SENSOR_FILTER_H_
#define SENSOR_FILTER_H_

#include "median_wirth.h"
#include <vector>
#include <stdexcept>

namespace Sanuwave {

// Sliding-window median filter for scalar sensor readings.
// Fixed capacity set at construction; reusable across sensor types via T.
// Non-copyable — each filter owns its own sample window.

template <typename T>
class SensorFilter {
public:
    explicit SensorFilter(int capacity)
        : capacity(capacity)
        , buffer(capacity)
    {
        if (capacity <= 0)
            throw std::invalid_argument("SensorFilter: capacity must be > 0");
    }

    SensorFilter(const SensorFilter&)            = delete;
    SensorFilter& operator=(const SensorFilter&) = delete;

    void reset()
    {
        head  = 0;
        count = 0;
    }

    // Push a new sample; returns the median of the current window.
    T push(T value)
    {
        buffer[head % capacity] = value;
        ++head;
        if (count < capacity) ++count;
        return Sanuwave::MedianWirth<T>::median(buffer.data(), count);
    }

    // How many samples are currently in the window.
    int size() const { return count; }

    // True once the window is fully populated.
    bool settled() const { return count == capacity; }

private:
    int            capacity;
    std::vector<T> buffer;
    int            head  = 0;
    int            count = 0;
};

} // namespace Sanuwave
#endif
