#pragma once

#include "gbasave/save_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gbasave {

enum class FlashRoutineRole {
    SwitchBank,
    ReadId,
    TimerInterrupt,
    ConfigureTimerInterrupt,
    StartTimer,
    StopTimer,
    ReadByteHelper,
    ConfigureReadHelper,
    ReadCore,
    Read,
    VerifyCore,
    VerifySector,
    VerifySectorNBytes,
    ProgramSectorAndVerify,
    ProgramSectorAndVerifyNBytes,
    Identify,
    WaitForWrite,
    EraseChip,
    EraseSector,
    ProgramByte,
    ProgramSector,
};

struct FlashRoutineSignature {
    FlashRoutineRole role{};
    std::string name;
    std::size_t expectedOffset{};
    std::size_t stockSize{};
    std::vector<std::uint8_t> signature;
    std::string evidence;
};

struct FlashLibraryProfile {
    std::string name;
    std::string marker;
    SaveGeometry geometry;
    std::vector<FlashRoutineSignature> routines;
};

const FlashLibraryProfile &flash1mV102Profile();
std::vector<const FlashLibraryProfile *> registeredFlashLibraryProfiles();
std::string toString(FlashRoutineRole role);

} // namespace gbasave
