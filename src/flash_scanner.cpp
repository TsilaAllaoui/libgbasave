#include "gbasave/flash_scanner.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace gbasave {
namespace {

constexpr std::uint32_t kGbaRomBase = 0x08000000u;

std::size_t align4(std::size_t value)
{
    return (value + 3u) & ~std::size_t(3u);
}

bool isPlausibleRomPointer(std::uint32_t pointer, std::size_t romSize)
{
    const auto address = pointer & ~1u;
    return address >= kGbaRomBase && static_cast<std::size_t>(address - kGbaRomBase) < romSize;
}

std::size_t gbaAddressToRomOffset(std::uint32_t pointer, std::size_t romSize)
{
    const auto address = pointer & ~1u;
    if (!isPlausibleRomPointer(address, romSize))
        throw std::runtime_error("pointer outside GBA ROM window: " + hex(pointer, 8));
    return static_cast<std::size_t>(address - kGbaRomBase);
}

std::vector<std::size_t> findDirectThumbBlCallsites(const RomImage &rom, std::size_t targetOffset)
{
    std::vector<std::size_t> callsites;
    for (std::size_t offset = 0; offset + 4 <= rom.size(); offset += 2) {
        const auto firstHalf = rom.u16(offset);
        const auto secondHalf = rom.u16(offset + 2);
        if ((firstHalf & 0xF800u) != 0xF000u || (secondHalf & 0xF800u) != 0xF800u)
            continue;

        std::int32_t high = static_cast<std::int32_t>(firstHalf & 0x07FFu);
        if (high & 0x400)
            high -= 0x800;
        const std::int32_t delta = (high << 12) | (static_cast<std::int32_t>(secondHalf & 0x07FFu) << 1);
        const std::int64_t target = static_cast<std::int64_t>(offset) + 4 + delta;
        if (target >= 0 && static_cast<std::size_t>(target) == targetOffset)
            callsites.push_back(offset);
    }
    return callsites;
}

std::string chipName(std::uint16_t id)
{
    switch (id) {
    case 0x09C2: return "Macronix MX29L010";
    case 0x1362: return "Sanyo LE26FV10N1TS";
    case 0x0000: return "Default/fallback profile";
    default: return "Unknown profile";
    }
}

bool asciiDigit(std::uint8_t value)
{
    return value >= static_cast<std::uint8_t>('0') && value <= static_cast<std::uint8_t>('9');
}

std::vector<std::pair<std::size_t, std::string>> findFlash1mMarkers(const RomImage &rom)
{
    constexpr const char *kPrefix = "FLASH1M_V";
    constexpr std::size_t kPrefixBytes = 9u;
    std::vector<std::pair<std::size_t, std::string>> result;
    for (const auto offset : rom.findAscii(kPrefix)) {
        const std::size_t versionOffset = offset + kPrefixBytes;
        if (versionOffset + 3u >= rom.size())
            continue;
        if (!asciiDigit(rom.bytes()[versionOffset]) ||
            !asciiDigit(rom.bytes()[versionOffset + 1u]) ||
            !asciiDigit(rom.bytes()[versionOffset + 2u]) ||
            rom.bytes()[versionOffset + 3u] != 0u)
            continue;
        result.push_back({offset,
            std::string(reinterpret_cast<const char *>(rom.bytes().data() + offset), kPrefixBytes + 3u)});
    }
    return result;
}

std::string jsonEscape(const std::string &text)
{
    std::ostringstream output;
    for (char c : text) {
        switch (c) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default: output << c; break;
        }
    }
    return output.str();
}

} // namespace

const FunctionInfo &FlashMap::function(FlashRoutineRole role) const
{
    const auto it = std::find_if(functions.begin(), functions.end(), [role](const FunctionInfo &function) {
        return function.role == role;
    });
    if (it == functions.end())
        throw std::runtime_error("FLASH routine is not present in the map: " + toString(role));
    return *it;
}

FlashMap FlashScanner::scan(const RomImage &rom) const
{
    // FLASH1M SDK revisions share one stock control-flow family. Detect the
    // versioned marker generically, then prove compatibility from setup-table
    // geometry and the complete routine-signature map before patching.
    const auto markerMatches = findFlash1mMarkers(rom);
    if (markerMatches.empty())
        throw std::runtime_error("no supported stock FLASH1M library was detected");
    if (markerMatches.size() != 1u)
        throw std::runtime_error("multiple FLASH1M save-library markers detected");

    const FlashLibraryProfile *detectedLibrary = &flash1mV102Profile();
    const std::size_t detectedMarkerOffset = markerMatches.front().first;
    const std::string detectedMarker = markerMatches.front().second;

    const auto &library = *detectedLibrary;
    FlashMap result;
    result.libraryProfile = detectedLibrary;
    result.detectedMarker = detectedMarker;
    result.markerOffset = detectedMarkerOffset;
    result.setupTableOffset = align4(result.markerOffset + result.detectedMarker.size() + 1u);

    for (std::size_t index = 0; index < 3; ++index) {
        const auto setupPointer = rom.u32(result.setupTableOffset + index * 4u);
        if (!isPlausibleRomPointer(setupPointer, rom.size()))
            throw std::runtime_error("invalid FLASH setup-table pointer");

        const auto setupOffset = gbaAddressToRomOffset(setupPointer, rom.size());
        SetupProfile setup;
        setup.offset = setupOffset;

        const auto &geometry = library.geometry;
        // Nintendo FLASH1M setup records exist in two closely related layouts.
        // Newer SDKs (including V103) store a maxTime pointer at +0x14 and the
        // FlashType inline from +0x18. Older validated builds used the legacy
        // compact geometry offsets below. Detect the layout from geometry, not
        // from the marker version string.
        const bool inlineFlashType =
            setupOffset + 0x2Eu <= rom.size() &&
            rom.u32(setupOffset + 0x18u) == geometry.totalBytes &&
            rom.u32(setupOffset + 0x1Cu) == geometry.sectorBytes &&
            rom.bytes()[setupOffset + 0x20u] == 12u &&
            rom.u16(setupOffset + 0x22u) == geometry.bankCount * geometry.sectorsPerBank;

        if (inlineFlashType) {
            // V103-style record starts with ProgramFlashByte then the four
            // physical/setup callbacks used by our stock-wrapper validation.
            setup.programFlashSector = rom.u32(setupOffset + 0x04u);
            setup.eraseFlashChip = rom.u32(setupOffset + 0x08u);
            setup.eraseFlashSector = rom.u32(setupOffset + 0x0Cu);
            setup.waitForFlashWrite = rom.u32(setupOffset + 0x10u);
            setup.maxTime = rom.u32(setupOffset + 0x14u);
            setup.romSize = rom.u32(setupOffset + 0x18u);
            setup.sectorSize = rom.u32(setupOffset + 0x1Cu);
            setup.sectorShift = rom.bytes()[setupOffset + 0x20u];
            setup.sectorCount = rom.u16(setupOffset + 0x22u);
            setup.flashId = rom.u16(setupOffset + 0x2Cu);
        } else {
            setup.programFlashSector = rom.u32(setupOffset + 0x00u);
            setup.eraseFlashChip = rom.u32(setupOffset + 0x04u);
            setup.eraseFlashSector = rom.u32(setupOffset + 0x08u);
            setup.waitForFlashWrite = rom.u32(setupOffset + 0x0Cu);
            setup.maxTime = rom.u32(setupOffset + 0x10u);
            setup.romSize = rom.u32(setupOffset + 0x14u);
            setup.sectorSize = rom.u32(setupOffset + 0x18u);
            setup.sectorShift = rom.u16(setupOffset + 0x1Cu);
            setup.sectorCount = rom.u16(setupOffset + 0x1Eu);
            setup.flashId = rom.u16(setupOffset + 0x28u);
        }
        setup.chipName = chipName(setup.flashId);

        if (setup.romSize != geometry.totalBytes ||
            setup.sectorSize != geometry.sectorBytes ||
            setup.sectorCount != geometry.bankCount * geometry.sectorsPerBank ||
            setup.sectorShift != 12) {
            throw std::runtime_error("FLASH setup profile geometry does not match detected FLASH1M routine family");
        }

        result.setupProfiles.push_back(setup);
    }

    result.allSignaturesUnique = true;
    for (const auto &knownRoutine : library.routines) {
        const auto matches = rom.findBytes(knownRoutine.signature);
        if (matches.size() != 1)
            result.allSignaturesUnique = false;
        if (matches.empty())
            throw std::runtime_error("missing stock FLASH routine signature: " + knownRoutine.name);

        FunctionInfo function;
        function.role = knownRoutine.role;
        function.name = knownRoutine.name;
        function.offset = matches.front();
        function.size = knownRoutine.stockSize;
        function.signature = knownRoutine.signature;
        function.evidence = knownRoutine.evidence;
        function.directCallsites = findDirectThumbBlCallsites(rom, function.offset);
        result.functions.push_back(std::move(function));
    }

    for (const auto &setup : result.setupProfiles) {
        const auto expect = [&](std::uint32_t pointer, FlashRoutineRole role) {
            const auto actualOffset = gbaAddressToRomOffset(pointer, rom.size());
            const auto expectedOffset = result.function(role).offset;
            if (actualOffset != expectedOffset)
                throw std::runtime_error("setup profile mismatch for " + toString(role));
        };

        expect(setup.programFlashSector, FlashRoutineRole::ProgramSector);
        expect(setup.eraseFlashChip, FlashRoutineRole::EraseChip);
        expect(setup.eraseFlashSector, FlashRoutineRole::EraseSector);
        expect(setup.waitForFlashWrite, FlashRoutineRole::WaitForWrite);
    }

    return result;
}

std::string FlashScanner::toText(const RomImage &rom, const FlashMap &map, const std::string &sha256)
{
    std::ostringstream output;
    output << "GBASaveHandler stock FLASH map\n";
    output << "Library: " << map.libraryProfile->name << "\n";
    output << "ROM title: " << rom.title() << "\n";
    output << "Game code: " << rom.gameCode() << "\n";
    output << "Revision: " << unsigned(rom.revision()) << "\n";
    output << "Size: " << rom.size() << " bytes\n";
    output << "SHA256: " << sha256 << "\n";
    output << "Header checksum: stored=" << hex(rom.headerChecksumStored(), 2)
           << " calculated=" << hex(rom.headerChecksumCalculated(), 2)
           << (rom.headerChecksumStored() == rom.headerChecksumCalculated() ? " PASS" : " FAIL") << "\n";
    output << "FLASH marker: " << map.detectedMarker << " @ " << hex(map.markerOffset, 6) << "\n";
    output << "Setup table @ " << hex(map.setupTableOffset, 6) << "\n\n";

    output << "Setup profiles:\n";
    for (const auto &profile : map.setupProfiles) {
        output << "  " << profile.chipName << " @ " << hex(profile.offset, 6)
               << " id=" << hex(profile.flashId, 4)
               << " geometry=" << profile.sectorCount << "x" << profile.sectorSize
               << " (shift " << profile.sectorShift << ")\n";
        output << "    ProgramFlashSector=" << hex(profile.programFlashSector, 8)
               << " EraseFlashChip=" << hex(profile.eraseFlashChip, 8)
               << " EraseFlashSector=" << hex(profile.eraseFlashSector, 8)
               << " WaitForFlashWrite=" << hex(profile.waitForFlashWrite, 8) << "\n";
    }

    output << "\nStock FLASH routines (" << map.functions.size() << "):\n";
    for (const auto &function : map.functions) {
        output << "  " << std::left << std::setw(34) << function.name << std::right
               << " role=" << std::setw(28) << toString(function.role)
               << " ROM=" << hex(function.offset, 6)
               << " GBA=" << hex(kGbaRomBase + function.offset + 1u, 8)
               << " size=" << hex(function.size)
               << " xrefs=" << function.directCallsites.size() << "\n";
        output << "    signature: " << bytesHex(rom.bytes(), function.offset, function.signature.size()) << "\n";
        output << "    evidence: " << function.evidence << "\n";
    }

    output << "\nSignature uniqueness: " << (map.allSignaturesUnique ? "PASS" : "WARNING") << "\n";
    return output.str();
}

std::string FlashScanner::toJson(const RomImage &rom, const FlashMap &map, const std::string &sha256)
{
    std::ostringstream output;
    output << "{\n";
    output << "  \"project\": \"GBASaveHandler\",\n";
    output << "  \"library\": \"" << jsonEscape(map.libraryProfile->name) << "\",\n";
    output << "  \"rom\": {\"title\": \"" << jsonEscape(rom.title())
           << "\", \"game_code\": \"" << jsonEscape(rom.gameCode())
           << "\", \"revision\": " << unsigned(rom.revision())
           << ", \"size\": " << rom.size()
           << ", \"sha256\": \"" << sha256 << "\"},\n";
    output << "  \"marker_offset\": " << map.markerOffset << ",\n";
    output << "  \"setup_table_offset\": " << map.setupTableOffset << ",\n";
    output << "  \"functions\": [\n";
    for (std::size_t index = 0; index < map.functions.size(); ++index) {
        const auto &function = map.functions[index];
        output << "    {\"name\": \"" << jsonEscape(function.name)
               << "\", \"role\": \"" << jsonEscape(toString(function.role))
               << "\", \"offset\": " << function.offset
               << ", \"gba_address\": \"" << hex(kGbaRomBase + function.offset + 1u, 8)
               << "\", \"size\": " << function.size << "}";
        if (index + 1 != map.functions.size())
            output << ',';
        output << '\n';
    }
    output << "  ],\n";
    output << "  \"signature_uniqueness\": " << (map.allSignaturesUnique ? "true" : "false") << "\n";
    output << "}\n";
    return output.str();
}

} // namespace gbasave
