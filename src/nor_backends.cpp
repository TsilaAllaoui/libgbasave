#include "gbasave/nor_backends.h"
#include "nor_profile_db.h"

#include <sstream>
#include <stdexcept>

namespace gbasave {

const NorBackendDescriptor &norBackendDescriptor(NorFlashType type)
{
    for (const auto &backend : kGeneratedNorBackends) {
        if (backend.type == type)
            return backend;
    }
    throw std::runtime_error("unknown NOR backend");
}

const NorTargetProfileDescriptor &parseNorTargetProfile(const std::string &value)
{
    for (const auto &alias : kGeneratedNorTargetAliases) {
        if (value == alias.alias)
            return kGeneratedNorTargets.at(alias.targetIndex);
    }
    throw std::runtime_error("unknown NOR target profile: " + value);
}

const NorTargetProfileDescriptor &defaultNorTargetProfile(NorFlashType type)
{
    for (std::size_t index = 0; index < kGeneratedNorBackends.size(); ++index) {
        if (kGeneratedNorBackends[index].type == type)
            return kGeneratedNorTargets.at(kGeneratedNorDefaultTargetByBackend.at(index));
    }
    throw std::runtime_error("unknown NOR backend default target");
}

NorFlashType parseNorBackendProfile(const std::string &value)
{
    return parseNorTargetProfile(value).backendType;
}

const NorBackendDescriptor *norBackendProfilesData()
{
    return kGeneratedNorBackends.data();
}

std::size_t norBackendProfileCount()
{
    return kGeneratedNorBackends.size();
}

const NorTargetProfileDescriptor *norTargetProfilesData()
{
    return kGeneratedNorTargets.data();
}

std::size_t norTargetProfileCount()
{
    return kGeneratedNorTargets.size();
}

bool validateCompiledNorProfiles(std::string &message)
{
    std::ostringstream errors;
    bool ok = true;

    for (std::size_t i = 0; i < kGeneratedNorBackends.size(); ++i) {
        const auto &backend = kGeneratedNorBackends[i];
        if (!backend.profileKey || !*backend.profileKey || !backend.displayName || !*backend.displayName) {
            errors << "backend[" << i << "]: missing key/display name\n";
            ok = false;
        }
        if (backend.programUnitBytes == 0u || backend.romAddressSpaceLimit == 0u ||
            backend.mutableMainArrayEnd > backend.romAddressSpaceLimit) {
            errors << backend.profileKey << ": invalid program/address geometry\n";
            ok = false;
        }
        if (backend.supportsBlockErase != (backend.eraseBlockBytes != 0u)) {
            errors << backend.profileKey << ": erase capability/size mismatch\n";
            ok = false;
        }
        if (backend.storageForbiddenRangeCount > backend.storageForbiddenRanges.size()) {
            errors << backend.profileKey << ": forbidden range count overflow\n";
            ok = false;
        }
    }

    for (std::size_t i = 0; i < kGeneratedNorTargets.size(); ++i) {
        const auto &target = kGeneratedNorTargets[i];
        try {
            const auto &backend = norBackendDescriptor(target.backendType);
            if (target.capacityBytes == 0u || target.capacityBytes > backend.romAddressSpaceLimit) {
                errors << target.key << ": capacity exceeds protocol limit\n";
                ok = false;
            }
            if (!target.protocolKey || std::string(target.protocolKey) != backend.profileKey) {
                errors << target.key << ": protocol key/backend mismatch\n";
                ok = false;
            }
            for (std::size_t r = 0; r < backend.storageForbiddenRangeCount; ++r) {
                const auto &range = backend.storageForbiddenRanges[r];
                if (range.offset + range.bytes > target.capacityBytes) {
                    errors << target.key << ": forbidden range exceeds target capacity\n";
                    ok = false;
                }
            }
        } catch (const std::exception &) {
            errors << target.key << ": target references unknown backend\n";
            ok = false;
        }
    }

    message = ok ? "compiled NOR profile table is internally consistent" : errors.str();
    return ok;
}

} // namespace gbasave
