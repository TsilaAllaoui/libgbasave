#pragma once

#include "gbasave/rom_image.h"
#include "gbasave/save_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gbasave {

// Nintendo SDK save-library families. Marker discovery accepts any structurally
// valid version suffix; support is decided by ABI validation, not a version whitelist.
enum class SaveLibraryFamily {
    Flash1M,
    Flash512,
    Eeprom,
    Sram,
};

struct SaveMarkerMatch {
    std::string marker;
    std::size_t offset{};
};

std::vector<SaveMarkerMatch> findSaveFamilyMarkers(const RomImage &rom, SaveLibraryFamily family);
SaveMarkerMatch findUniqueSaveMarker(const RomImage &rom, SaveLibraryFamily family);
bool hasSaveFamilyMarker(const RomImage &rom, SaveLibraryFamily family);
SaveType autoDetectSaveType(const RomImage &rom);

} // namespace gbasave
