#pragma once

#include "gbasave/runtime_config.h"
#include "gbasave/save_types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gbasave {

struct RuntimeImage {
    std::vector<std::uint8_t> bytes;
    std::size_t configOffset{};
    std::uint32_t linkedBaseAddress{};
    std::uint32_t currentBaseAddress{};
    std::vector<std::size_t> absolute32RelocationOffsets;
    std::map<std::string, std::size_t> symbolOffsets;
};

RuntimeImage loadEmbeddedRuntimeImage(NorFlashType norFlashType);
void patchRuntimeConfig(RuntimeImage &image, const RuntimeConfig &config);
void patchRuntimeSymbolWord(RuntimeImage &image, const std::string &symbol, std::uint32_t value);
void relocateRuntimeImage(RuntimeImage &image, std::uint32_t newBaseAddress);
std::uint32_t runtimeSymbolAddress(const RuntimeImage &image, const std::string &symbol);

} // namespace gbasave
