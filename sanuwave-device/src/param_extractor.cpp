// src/param_extractor.cpp
#include "param_extractor.h"
#include <algorithm>
#include <sstream>

namespace sanuwave
{

std::string ParamExtractor::getString(const std::string& key, const std::string& def) const
{
    auto it = params_.find(key);
    return (it != params_.end()) ? it->second : def;
}

int ParamExtractor::getInt(const std::string& key, int def) const
{
    auto it = params_.find(key);
    if (it == params_.end() || it->second.empty())
        return def;
    try   { return std::stoi(it->second); }
    catch (...) { return def; }
}

float ParamExtractor::getFloat(const std::string& key, float def) const
{
    auto it = params_.find(key);
    if (it == params_.end() || it->second.empty())
        return def;
    try   { return std::stof(it->second); }
    catch (...) { return def; }
}

bool ParamExtractor::getBool(const std::string& key, bool def) const
{
    auto it = params_.find(key);
    if (it == params_.end())
        return def;
    const auto& v = it->second;
    return v == "true" || v == "1" || v == "yes";
}

bool ParamExtractor::hasKey(const std::string& key) const
{
    return params_.find(key) != params_.end();
}

bool ParamExtractor::hasKey(std::initializer_list<const char*> keys) const
{
    for (const char* k : keys)
        if (params_.find(k) != params_.end())
            return true;
    return false;
}

// Parse "[1, 2, 3]" stored as a raw string value into a vector of ints.
static std::vector<int> parseIntArray(const std::string& raw)
{
    std::vector<int> result;
    auto start = raw.find('[');
    auto end   = raw.rfind(']');
    if (start == std::string::npos || end == std::string::npos || end <= start)
        return result;

    std::string inner = raw.substr(start + 1, end - start - 1);
    std::stringstream ss(inner);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        // strip whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        if (token.empty()) continue;
        try   { result.push_back(std::stoi(token)); }
        catch (...) {}
    }
    return result;
}

std::vector<int> ParamExtractor::getIntList(const std::string& key) const
{
    auto it = params_.find(key);
    if (it == params_.end()) return {};
    return parseIntArray(it->second);
}

std::vector<uint8_t> ParamExtractor::getUInt8List(const std::string& key) const
{
    auto ints = getIntList(key);
    std::vector<uint8_t> result;
    result.reserve(ints.size());
    for (int v : ints)
        result.push_back(static_cast<uint8_t>(std::clamp(v, 0, 255)));
    return result;
}

} // namespace sanuwave
