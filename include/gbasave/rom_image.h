#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gbasave {

// In-memory GBA ROM image used by the patch engine.
// File-system ownership deliberately belongs to consumers such as GBASaveHandler,
// GBABR, or OpenFlash. Embedded consumers can construct this object from any
// storage/stream transport without pulling desktop file APIs into libgbasave.
class RomImage {
public:
    RomImage() = default;
    explicit RomImage(std::vector<std::uint8_t> bytes) : data_(std::move(bytes)) {}

    static RomImage fromBytes(std::vector<std::uint8_t> bytes)
    {
        return RomImage(std::move(bytes));
    }

    const std::vector<std::uint8_t> &bytes() const { return data_; }
    std::vector<std::uint8_t> &bytes() { return data_; }
    std::size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }

    std::uint16_t u16(std::size_t offset) const;
    std::uint32_t u32(std::size_t offset) const;
    std::string ascii(std::size_t offset, std::size_t length) const;
    std::vector<std::size_t> findAscii(const std::string &text) const;
    std::vector<std::size_t> findBytes(const std::vector<std::uint8_t> &signature) const;

    void resize(std::size_t newSize, std::uint8_t fill = 0xFF);
    void write(std::size_t offset, const void *data, std::size_t length);
    void write16(std::size_t offset, std::uint16_t value);
    void write32(std::size_t offset, std::uint32_t value);

    std::string title() const;
    std::string gameCode() const;
    std::uint8_t revision() const;
    std::uint8_t headerChecksumStored() const;
    std::uint8_t headerChecksumCalculated() const;

private:
    std::vector<std::uint8_t> data_;
};

std::string hex(std::uint64_t value, unsigned width = 0);
std::string bytesHex(const std::vector<std::uint8_t> &data, std::size_t offset, std::size_t length);

} // namespace gbasave
