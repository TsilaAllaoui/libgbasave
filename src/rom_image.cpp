#include "gbasave/rom_image.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace gbasave {

std::uint16_t RomImage::u16(std::size_t offset) const
{
    if (offset + 2 > data_.size())
        throw std::out_of_range("u16 outside ROM");
    return static_cast<std::uint16_t>(data_[offset]) |
           (static_cast<std::uint16_t>(data_[offset + 1]) << 8);
}

std::uint32_t RomImage::u32(std::size_t offset) const
{
    if (offset + 4 > data_.size())
        throw std::out_of_range("u32 outside ROM");
    return static_cast<std::uint32_t>(data_[offset]) |
           (static_cast<std::uint32_t>(data_[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data_[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data_[offset + 3]) << 24);
}

std::string RomImage::ascii(std::size_t offset, std::size_t length) const
{
    if (offset + length > data_.size())
        throw std::out_of_range("ascii outside ROM");

    std::string text(reinterpret_cast<const char *>(data_.data() + offset), length);
    while (!text.empty() && (text.back() == '\0' || text.back() == ' '))
        text.pop_back();
    return text;
}

std::vector<std::size_t> RomImage::findAscii(const std::string &text) const
{
    return findBytes(std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::vector<std::size_t> RomImage::findBytes(const std::vector<std::uint8_t> &signature) const
{
    std::vector<std::size_t> matches;
    if (signature.empty() || signature.size() > data_.size())
        return matches;

    auto cursor = data_.begin();
    while (cursor != data_.end()) {
        cursor = std::search(cursor, data_.end(), signature.begin(), signature.end());
        if (cursor == data_.end())
            break;
        matches.push_back(static_cast<std::size_t>(cursor - data_.begin()));
        ++cursor;
    }
    return matches;
}

void RomImage::resize(std::size_t newSize, std::uint8_t fill)
{
    data_.resize(newSize, fill);
}

void RomImage::write(std::size_t offset, const void *data, std::size_t length)
{
    if (offset + length > data_.size())
        throw std::out_of_range("write outside ROM");
    std::memcpy(data_.data() + offset, data, length);
}

void RomImage::write16(std::size_t offset, std::uint16_t value)
{
    const std::uint8_t bytes[2] = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8),
    };
    write(offset, bytes, sizeof(bytes));
}

void RomImage::write32(std::size_t offset, std::uint32_t value)
{
    const std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value >> 16),
        static_cast<std::uint8_t>(value >> 24),
    };
    write(offset, bytes, sizeof(bytes));
}

std::string RomImage::title() const { return size() >= 0xAC ? ascii(0xA0, 12) : std::string{}; }
std::string RomImage::gameCode() const { return size() >= 0xB0 ? ascii(0xAC, 4) : std::string{}; }
std::uint8_t RomImage::revision() const { return size() > 0xBC ? data_[0xBC] : 0; }
std::uint8_t RomImage::headerChecksumStored() const { return size() > 0xBD ? data_[0xBD] : 0; }

std::uint8_t RomImage::headerChecksumCalculated() const
{
    if (size() <= 0xBC)
        return 0;
    std::uint8_t checksum = 0;
    for (std::size_t offset = 0xA0; offset <= 0xBC; ++offset)
        checksum = static_cast<std::uint8_t>(checksum - data_[offset]);
    return static_cast<std::uint8_t>(checksum - 0x19);
}

std::string hex(std::uint64_t value, unsigned width)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setfill('0');
    if (width)
        stream << std::setw(static_cast<int>(width));
    stream << value;
    return stream.str();
}

std::string bytesHex(const std::vector<std::uint8_t> &data, std::size_t offset, std::size_t length)
{
    if (offset > data.size())
        return {};
    length = std::min(length, data.size() - offset);

    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < length; ++i)
        stream << std::setw(2) << static_cast<unsigned>(data[offset + i]);
    return stream.str();
}

} // namespace gbasave
