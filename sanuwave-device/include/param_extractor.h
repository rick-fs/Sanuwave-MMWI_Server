// include/param_extractor.h
#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace sanuwave
{

/// Helper to extract typed parameters with defaults from a flat string map.
class ParamExtractor
{
public:
    explicit ParamExtractor(const std::map<std::string, std::string>& params)
        : params_(params) {}

    std::string getString(const std::string& key, const std::string& def = "") const;
    int         getInt   (const std::string& key, int   def = 0)     const;
    float       getFloat (const std::string& key, float def = 0.0f)  const;
    bool        getBool  (const std::string& key, bool  def = false)  const;

    bool hasKey(const std::string& key) const;

    /// Returns true if ANY of the listed keys are present.
    bool hasKey(std::initializer_list<const char*> keys) const;

    /// Parse a top-level JSON array value (e.g. "[1,2,3]") into a vector of ints.
    std::vector<int>     getIntList   (const std::string& key) const;

    /// Parse a top-level JSON array value into a vector of uint8_t (clamped 0-255).
    std::vector<uint8_t> getUInt8List (const std::string& key) const;

private:
    const std::map<std::string, std::string>& params_;
};

} // namespace sanuwave
