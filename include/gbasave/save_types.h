#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace gbasave {

enum class SaveType {
    Unknown,
    Flash512,
    Flash1M,
    Sram,
    Eeprom512,
    Eeprom8K,
};

enum class NorFlashType : std::uint16_t {
    // Hardware-proven M6/M6M-compatible Intel status-register programming:
    // target-local 0x40 word-program setup. Keep value 1 for v0.26 compatibility.
    IntelStatusRegister = 1,

    // M36L0R/M36L0T-compatible Intel E8 buffered RWW profile.
    // This is the canonical name from the current NOR protocol schema.
    IntelE8BufferedRww = 2,

    // Historical public name retained as a source/ABI-compatible alias.
    // New code and generated tables should use IntelE8BufferedRww.
    IntelStatusRegisterWord10 = IntelE8BufferedRww,

    // Intel-compatible target-relative status/program protocol used by the
    // dual-die M6MGD137W34D profile. Kept separate from the hardware-proven
    // M6/M6M path so its FlashGBX-qualified 70/40 and erase sequencing can be
    // changed without perturbing the frozen word40 engine.
    IntelRelativeWordProgram = 3,

    // AMD/Fujitsu unlock-cycle word programming (AA/55/A0). The MX26L6420
    // profile deliberately exposes no sector-erase capability.
    AmdUnlockWordProgram = 4,
};

struct SaveGeometry {
    SaveType type{SaveType::Unknown};
    std::size_t totalBytes{};
    std::size_t bankBytes{};
    std::size_t sectorBytes{};
    std::size_t bankCount{};
    std::size_t sectorsPerBank{};
};

std::string toString(SaveType type);
std::string toString(NorFlashType type);

} // namespace gbasave
