#pragma once
#include <cstdint>
#include <string>

namespace sanuwave {
namespace protocol {

struct DiagSensorInfoData {
    std::string name;
    uint32_t    nativeBitDepth   = 0;
    uint32_t    activeAreaWidth  = 0;
    uint32_t    activeAreaHeight = 0;
};

} // namespace protocol
} // namespace sanuwave