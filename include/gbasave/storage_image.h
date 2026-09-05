#pragma once

#include "gbasave/rom_image.h"
#include "gbasave/storage_layout.h"

#include <string>
#include <vector>

namespace gbasave {

struct StorageImageResult {
    bool exactPreservationApplied{};
    std::string preservationMode;
};

StorageImageResult initialiseStorageImage(
    RomImage &rom,
    const StorageLayout &storage,
    const std::vector<std::uint8_t> &preserveImage);

} // namespace gbasave
