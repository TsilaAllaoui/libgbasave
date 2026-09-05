#pragma once

#include "gbasave/save_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace gbasave {

inline constexpr std::size_t kMaxNorStorageForbiddenRanges = 4u;

struct NorStorageForbiddenRange {
    std::size_t offset{};
    std::size_t bytes{};
};

// Reusable electrical/runtime protocol descriptor. Chip identities and aliases
// live in config/nor/chips; this descriptor is generated only from
// config/nor/protocols. Existing hardware-proven runtime driver enums remain
// frozen while multiple chips may select the same protocol.
struct NorBackendDescriptor {
    NorFlashType type{};
    const char *profileKey{};
    const char *displayName{};
    const char *protocolDescription{};
    std::uint16_t wordProgramCommand{};
    std::uint16_t runtimeProgramSelector{};
    std::size_t eraseBlockBytes{};
    std::size_t programUnitBytes{};
    bool requiresRamExecution{};
    std::size_t romAddressSpaceLimit{};
    std::size_t mutableMainArrayEnd{};
    std::size_t runtimeExecutionBankBytes{};
    bool supportsDirectProtocolEngine{};
    bool supportsBlockErase{};
    std::array<NorStorageForbiddenRange, kMaxNorStorageForbiddenRanges> storageForbiddenRanges{};
    std::size_t storageForbiddenRangeCount{};
};

// User-facing physical chip/cart profile. This stays deliberately small: a new
// chip using a proven protocol normally needs only one JSON file under
// config/nor/chips plus build-time regeneration.
struct NorTargetProfileDescriptor {
    const char *key{};
    const char *displayName{};
    NorFlashType backendType{};
    std::size_t capacityBytes{};
    const char *protocolKey{};
    const char *qualificationState{};
    const char *identificationSummary{};
    const char *qualificationNote{};
};

const NorBackendDescriptor &norBackendDescriptor(NorFlashType type);
const NorTargetProfileDescriptor &parseNorTargetProfile(const std::string &value);
const NorTargetProfileDescriptor &defaultNorTargetProfile(NorFlashType type);
NorFlashType parseNorBackendProfile(const std::string &value); // compatibility helper

const NorBackendDescriptor *norBackendProfilesData();
std::size_t norBackendProfileCount();
const NorTargetProfileDescriptor *norTargetProfilesData();
std::size_t norTargetProfileCount();

// Validates the compiled table itself. Source JSON is validated more strictly by
// tools/gen_nor_profiles.py before this header can be generated.
bool validateCompiledNorProfiles(std::string &message);

} // namespace gbasave
