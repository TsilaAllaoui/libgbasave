#include "gbasave/save_marker.h"

#include <algorithm>
#include <stdexcept>

namespace gbasave {
namespace {

bool isAsciiDigit(std::uint8_t value)
{
    return value >= static_cast<std::uint8_t>('0') && value <= static_cast<std::uint8_t>('9');
}

std::vector<SaveMarkerMatch> findVersionedMarkers(const RomImage &rom, const std::string &prefix)
{
    std::vector<SaveMarkerMatch> matches;
    for (const auto offset : rom.findAscii(prefix)) {
        // A GBA ROM is binary, not a tokenized text file. The byte immediately
        // before an SDK marker may be an instruction/data byte that happens to be
        // ASCII, so it is not valid negative evidence. The version digits and NUL
        // terminator below are the marker boundary proof.
        const std::size_t versionOffset = offset + prefix.size();
        if (versionOffset + 3u > rom.size())
            continue;
        if (!isAsciiDigit(rom.bytes()[versionOffset]) ||
            !isAsciiDigit(rom.bytes()[versionOffset + 1u]) ||
            !isAsciiDigit(rom.bytes()[versionOffset + 2u]))
            continue;
        const std::size_t end = versionOffset + 3u;
        if (end >= rom.size() || rom.bytes()[end] != 0u)
            continue;
        matches.push_back({
            std::string(reinterpret_cast<const char *>(rom.bytes().data() + offset), prefix.size() + 3u),
            offset,
        });
    }
    return matches;
}

SaveMarkerMatch uniqueFamilyMarker(const std::vector<SaveMarkerMatch> &matches, const char *familyName)
{
    if (matches.empty())
        return {};
    if (matches.size() != 1u)
        throw std::runtime_error(std::string("multiple ") + familyName + " save-library markers found");
    return matches.front();
}

} // namespace

std::vector<SaveMarkerMatch> findSaveFamilyMarkers(const RomImage &rom, SaveLibraryFamily family)
{
    switch (family) {
    case SaveLibraryFamily::Flash1M:
        return findVersionedMarkers(rom, "FLASH1M_V");
    case SaveLibraryFamily::Flash512: {
        auto matches = findVersionedMarkers(rom, "FLASH512_V");
        auto alternate = findVersionedMarkers(rom, "FLASH_V");
        matches.insert(matches.end(), alternate.begin(), alternate.end());
        return matches;
    }
    case SaveLibraryFamily::Eeprom:
        return findVersionedMarkers(rom, "EEPROM_V");
    case SaveLibraryFamily::Sram: {
        auto matches = findVersionedMarkers(rom, "SRAM_V");
        auto fast = findVersionedMarkers(rom, "SRAM_F_V");
        matches.insert(matches.end(), fast.begin(), fast.end());
        return matches;
    }
    }
    return {};
}

SaveMarkerMatch findUniqueSaveMarker(const RomImage &rom, SaveLibraryFamily family)
{
    switch (family) {
    case SaveLibraryFamily::Flash1M:
        return uniqueFamilyMarker(findVersionedMarkers(rom, "FLASH1M_V"), "FLASH1M");
    case SaveLibraryFamily::Flash512: {
        auto matches = findSaveFamilyMarkers(rom, family);
        if (matches.empty()) {
            const auto legacy = rom.findAscii("FLASH512");
            if (legacy.size() == 1u)
                return {"FLASH512", legacy.front()};
        }
        return uniqueFamilyMarker(matches, "FLASH512/FLASH");
    }
    case SaveLibraryFamily::Eeprom:
        return uniqueFamilyMarker(findVersionedMarkers(rom, "EEPROM_V"), "EEPROM");
    case SaveLibraryFamily::Sram:
        return uniqueFamilyMarker(findSaveFamilyMarkers(rom, family), "SRAM");
    }
    return {};
}

bool hasSaveFamilyMarker(const RomImage &rom, SaveLibraryFamily family)
{
    if (!findSaveFamilyMarkers(rom, family).empty())
        return true;
    return family == SaveLibraryFamily::Flash512 && rom.findAscii("FLASH512").size() == 1u;
}

SaveType autoDetectSaveType(const RomImage &rom)
{
    std::vector<SaveType> candidates;
    if (hasSaveFamilyMarker(rom, SaveLibraryFamily::Flash1M)) candidates.push_back(SaveType::Flash1M);
    if (hasSaveFamilyMarker(rom, SaveLibraryFamily::Flash512)) candidates.push_back(SaveType::Flash512);
    if (hasSaveFamilyMarker(rom, SaveLibraryFamily::Eeprom)) candidates.push_back(SaveType::Eeprom8K);
    if (hasSaveFamilyMarker(rom, SaveLibraryFamily::Sram)) candidates.push_back(SaveType::Sram);
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (candidates.empty())
        throw std::runtime_error("no supported save-library marker found");
    if (candidates.size() != 1u)
        throw std::runtime_error("multiple save-library families found; use --save-type explicitly");
    return candidates.front();
}

} // namespace gbasave
