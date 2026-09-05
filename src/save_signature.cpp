#include "gbasave/save_signature.h"

#include <sstream>
#include <stdexcept>

namespace gbasave {
namespace {

std::vector<std::uint8_t> parseHex(const char *text)
{
    std::vector<std::uint8_t> result;
    while (*text) {
        unsigned value = 0;
        for (int nibble = 0; nibble < 2; ++nibble) {
            const char c = *text++;
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else throw std::runtime_error("invalid built-in save signature");
        }
        result.push_back(static_cast<std::uint8_t>(value));
    }
    return result;
}

} // namespace

MaskedPattern exactPattern(const char *hexText)
{
    MaskedPattern pattern;
    pattern.bytes = parseHex(hexText);
    pattern.wildcard.assign(pattern.bytes.size(), 0u);
    return pattern;
}

MaskedPattern wildcardPattern(const char *hexText, std::initializer_list<std::size_t> wildcardOffsets)
{
    auto pattern = exactPattern(hexText);
    for (const auto offset : wildcardOffsets) {
        if (offset >= pattern.wildcard.size())
            throw std::runtime_error("invalid built-in wildcard offset");
        pattern.wildcard[offset] = 1u;
    }
    return pattern;
}

std::vector<std::size_t> findPattern(const RomImage &rom, const MaskedPattern &pattern)
{
    std::vector<std::size_t> matches;
    if (pattern.bytes.empty() || pattern.bytes.size() > rom.size())
        return matches;
    for (std::size_t offset = 0; offset + pattern.bytes.size() <= rom.size(); ++offset) {
        bool matchesHere = true;
        for (std::size_t index = 0; index < pattern.bytes.size(); ++index) {
            if (!pattern.wildcard[index] && rom.bytes()[offset + index] != pattern.bytes[index]) {
                matchesHere = false;
                break;
            }
        }
        if (matchesHere)
            matches.push_back(offset);
    }
    return matches;
}

std::size_t uniquePattern(const RomImage &rom, const MaskedPattern &pattern, const std::string &name)
{
    const auto matches = findPattern(rom, pattern);
    if (matches.size() != 1u) {
        std::ostringstream message;
        message << name << " signature expected exactly once, found " << matches.size();
        throw std::runtime_error(message.str());
    }
    return matches.front();
}

std::vector<std::size_t> expectedPatternMatches(
    const RomImage &rom,
    const MaskedPattern &pattern,
    std::size_t expectedCount,
    const std::string &name)
{
    const auto matches = findPattern(rom, pattern);
    if (matches.size() != expectedCount) {
        std::ostringstream message;
        message << name << " signature expected " << expectedCount << " copies, found " << matches.size();
        throw std::runtime_error(message.str());
    }
    return matches;
}

void requirePatternAt(const RomImage &rom, std::size_t offset, const MaskedPattern &pattern, const std::string &name)
{
    if (offset + pattern.bytes.size() > rom.size())
        throw std::runtime_error(name + " exact-plan primitive lies outside ROM");
    for (std::size_t i = 0; i < pattern.bytes.size(); ++i) {
        if (!pattern.wildcard[i] && rom.bytes()[offset + i] != pattern.bytes[i])
            throw std::runtime_error(name + " exact-plan primitive signature changed");
    }
}

} // namespace gbasave
