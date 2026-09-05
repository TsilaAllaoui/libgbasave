#pragma once

#include "gbasave/rom_image.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace gbasave {

// Small reusable matcher for Nintendo SDK machine-code signatures. Signatures
// describe protocol ABI, never a title/game code. Wildcards are explicit byte
// positions used only for relocation-sensitive operands.
struct MaskedPattern {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> wildcard;
};

MaskedPattern exactPattern(const char *hexText);
MaskedPattern wildcardPattern(const char *hexText, std::initializer_list<std::size_t> wildcardOffsets);
std::vector<std::size_t> findPattern(const RomImage &rom, const MaskedPattern &pattern);
std::size_t uniquePattern(const RomImage &rom, const MaskedPattern &pattern, const std::string &name);
std::vector<std::size_t> expectedPatternMatches(
    const RomImage &rom,
    const MaskedPattern &pattern,
    std::size_t expectedCount,
    const std::string &name);
void requirePatternAt(const RomImage &rom, std::size_t offset, const MaskedPattern &pattern, const std::string &name);

} // namespace gbasave
