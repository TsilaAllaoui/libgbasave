#include "gbasave/flash_library_profile.h"

#include <stdexcept>

namespace gbasave {
namespace {

std::vector<std::uint8_t> parseHex(const char *text)
{
    std::vector<std::uint8_t> bytes;
    while (*text) {
        unsigned value = 0;
        for (int nibble = 0; nibble < 2; ++nibble) {
            const char c = *text++;
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<unsigned>(c - 'A' + 10);
            else
                throw std::runtime_error("invalid built-in hex signature");
        }
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    return bytes;
}

FlashRoutineSignature routine(
    FlashRoutineRole role,
    const char *name,
    std::size_t expectedOffset,
    std::size_t stockSize,
    const char *signatureHex,
    const char *evidence)
{
    return FlashRoutineSignature{
        role,
        name,
        expectedOffset,
        stockSize,
        parseHex(signatureHex),
        evidence,
    };
}

} // namespace

const FlashLibraryProfile &flash1mV102Profile()
{
    static const FlashLibraryProfile profile{
        "Nintendo FLASH1M_V102",
        "FLASH1M_V102",
        SaveGeometry{SaveType::Flash1M, 128u * 1024u, 64u * 1024u, 4u * 1024u, 2u, 16u},
        {
            routine(FlashRoutineRole::SwitchBank, "SwitchFlashBank", 0x0E7604, 0x1C, "0006000E054BAA21", "FLASH1M bank switch helper"),
            routine(FlashRoutineRole::ReadId, "ReadFlashId", 0x0E7628, 0x9C, "30B591B0684600F0", "reads JEDEC save-chip ID"),
            routine(FlashRoutineRole::TimerInterrupt, "FlashTimerIntr", 0x0E76C4, 0x28, "00B5074908880028", "flash timeout interrupt callback"),
            routine(FlashRoutineRole::ConfigureTimerInterrupt, "SetFlashTimerIntr", 0x0E76EC, 0x3C, "00B50A1C0006010E", "installs flash timeout callback"),
            routine(FlashRoutineRole::StartTimer, "StartFlashTimer", 0x0E7728, 0xA8, "70B556464D46444670B40006000E1D49", "starts flash timeout machinery"),
            routine(FlashRoutineRole::StopTimer, "StopFlashTimer", 0x0E77D0, 0x44, "0B4B002119800B4A", "stops flash timeout machinery"),
            routine(FlashRoutineRole::ReadByteHelper, "ReadFlash1", 0x0E7814, 0x04, "0078704700B5021C", "tiny byte reader copied to RAM by stock library"),
            routine(FlashRoutineRole::ConfigureReadHelper, "SetReadFlash1", 0x0E7818, 0x40, "00B5021C0549501C", "installs stock RAM read helper"),
            routine(FlashRoutineRole::ReadCore, "ReadFlash_Core", 0x0E7858, 0x24, "10B5041C531E002A", "stock raw flash copy core"),
            routine(FlashRoutineRole::Read, "ReadFlash", 0x0E787C, 0x9C, "F0B5A0B00D1C161C", "public FLASH read API"),
            routine(FlashRoutineRole::VerifyCore, "VerifyFlashSector_Core", 0x0E7918, 0x30, "30B5051C0B1C541E", "stock byte compare core"),
            routine(FlashRoutineRole::VerifySector, "VerifyFlashSector", 0x0E7948, 0x98, "30B5C0B00D1C0304", "public full-sector verify API"),
            routine(FlashRoutineRole::VerifySectorNBytes, "VerifyFlashSectorNBytes", 0x0E79E0, 0x98, "70B5C0B00D1C161C", "public partial verify API"),
            routine(FlashRoutineRole::ProgramSectorAndVerify, "ProgramFlashSectorAndVerify", 0x0E7A78, 0x44, "70B50D1C0004040C", "program-and-verify wrapper"),
            routine(FlashRoutineRole::ProgramSectorAndVerifyNBytes, "ProgramFlashSectorAndVerifyNBytes", 0x0E7ABC, 0x48, "F0B50D1C171C0004", "partial program-and-verify wrapper"),
            routine(FlashRoutineRole::Identify, "IdentifyFlash", 0x0E7B04, 0x94, "10B5074A10880749", "selects and installs setup profile"),
            routine(FlashRoutineRole::WaitForWrite, "WaitForFlashWrite_Common", 0x0E7B98, 0xA0, "F0B54F464646C0B40C1C", "stock status/timeout poller"),
            routine(FlashRoutineRole::EraseChip, "EraseFlashChip_MX", 0x0E7C38, 0x74, "70B590B0154D2988", "whole-chip erase implementation"),
            routine(FlashRoutineRole::EraseSector, "EraseFlashSector_MX", 0x0E7CAC, 0xCC, "F0B590B00004060C", "logical sector erase implementation"),
            routine(FlashRoutineRole::ProgramByte, "ProgramFlashByte_MX", 0x0E7D78, 0x38, "10B50A4CAA222270", "internal byte program helper"),
            routine(FlashRoutineRole::ProgramSector, "ProgramFlashSector_MX", 0x0E7DB0, 0xA4, "F0B590B00F1C0004", "logical sector program implementation"),
        },
    };
    return profile;
}

std::vector<const FlashLibraryProfile *> registeredFlashLibraryProfiles()
{
    return {&flash1mV102Profile()};
}

std::string toString(FlashRoutineRole role)
{
    switch (role) {
    case FlashRoutineRole::SwitchBank: return "SwitchBank";
    case FlashRoutineRole::ReadId: return "ReadId";
    case FlashRoutineRole::TimerInterrupt: return "TimerInterrupt";
    case FlashRoutineRole::ConfigureTimerInterrupt: return "ConfigureTimerInterrupt";
    case FlashRoutineRole::StartTimer: return "StartTimer";
    case FlashRoutineRole::StopTimer: return "StopTimer";
    case FlashRoutineRole::ReadByteHelper: return "ReadByteHelper";
    case FlashRoutineRole::ConfigureReadHelper: return "ConfigureReadHelper";
    case FlashRoutineRole::ReadCore: return "ReadCore";
    case FlashRoutineRole::Read: return "Read";
    case FlashRoutineRole::VerifyCore: return "VerifyCore";
    case FlashRoutineRole::VerifySector: return "VerifySector";
    case FlashRoutineRole::VerifySectorNBytes: return "VerifySectorNBytes";
    case FlashRoutineRole::ProgramSectorAndVerify: return "ProgramSectorAndVerify";
    case FlashRoutineRole::ProgramSectorAndVerifyNBytes: return "ProgramSectorAndVerifyNBytes";
    case FlashRoutineRole::Identify: return "Identify";
    case FlashRoutineRole::WaitForWrite: return "WaitForWrite";
    case FlashRoutineRole::EraseChip: return "EraseChip";
    case FlashRoutineRole::EraseSector: return "EraseSector";
    case FlashRoutineRole::ProgramByte: return "ProgramByte";
    case FlashRoutineRole::ProgramSector: return "ProgramSector";
    }
    return "Unknown";
}

} // namespace gbasave
