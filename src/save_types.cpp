#include "gbasave/save_types.h"

namespace gbasave {

std::string toString(SaveType type)
{
    switch (type) {
    case SaveType::Flash512: return "FLASH512";
    case SaveType::Flash1M: return "FLASH1M";
    case SaveType::Sram: return "SRAM";
    case SaveType::Eeprom512: return "EEPROM512";
    case SaveType::Eeprom8K: return "EEPROM8K";
    default: return "UNKNOWN";
    }
}

std::string toString(NorFlashType type)
{
    switch (type) {
    case NorFlashType::IntelStatusRegister: return "Intel status-register / 0x40 word program";
    case NorFlashType::IntelStatusRegisterWord10: return "M36 status-register / proven E8 buffered program";
    case NorFlashType::IntelRelativeWordProgram: return "Intel target-relative status / 0x40 word program";
    case NorFlashType::AmdUnlockWordProgram: return "AMD unlock-cycle / 0xA0 word program";
    }
    return "Unknown NOR";
}

} // namespace gbasave
