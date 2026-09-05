#include "gbasave/gbabr_plan.h"

#include "gbabr_plan_db.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace gbasave {
namespace {

std::uint32_t crc32(const std::vector<std::uint8_t> &bytes)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

SaveType saveTypeFromGbabrId(std::uint8_t id)
{
    switch (id) {
    case 1u: return SaveType::Sram;
    case 2u: return SaveType::Eeprom512;
    case 3u: return SaveType::Eeprom8K;
    case 4u: return SaveType::Flash512;
    case 5u: return SaveType::Flash1M;
    default: return SaveType::Unknown;
    }
}

} // namespace

bool findGbabrExactPlan(const RomImage &rom, GbabrExactPlan &plan)
{
    const std::uint32_t crc = crc32(rom.bytes());
    const auto *begin = std::begin(generated_gbabr_plan::kEntries);
    const auto *end = std::end(generated_gbabr_plan::kEntries);
    auto it = std::lower_bound(begin, end, crc, [](const auto &entry, std::uint32_t value) {
        return entry.crc32 < value;
    });
    for (; it != end && it->crc32 == crc; ++it) {
        if (it->romSize != rom.size())
            continue;

        GbabrExactPlan result;
        result.saveType = saveTypeFromGbabrId(it->saveType);
        if (result.saveType == SaveType::Unknown)
            return false;

        result.operations.reserve(it->opCount);
        for (std::size_t index = 0; index < it->opCount; ++index) {
            const auto &op = generated_gbabr_plan::kOps[it->firstOp + index];
            if (op.offset >= rom.size())
                throw std::runtime_error("GBABR exact plan operation lies outside ROM");
            GbabrPlanOperation converted;
            converted.offset = op.offset;
            converted.kind = op.kind;
            if (op.rawSize != 0u) {
                if (op.rawOffset + op.rawSize > sizeof(generated_gbabr_plan::kRawBytes))
                    throw std::runtime_error("GBABR exact plan raw operation lies outside generated table");
                converted.raw.assign(
                    generated_gbabr_plan::kRawBytes + op.rawOffset,
                    generated_gbabr_plan::kRawBytes + op.rawOffset + op.rawSize);
            }
            result.operations.push_back(std::move(converted));
        }

        result.irqOffsets.reserve(it->irqCount);
        for (std::size_t index = 0; index < it->irqCount; ++index) {
            const auto offset = generated_gbabr_plan::kIrqs[it->firstIrq + index];
            if (offset + 4u > rom.size())
                throw std::runtime_error("GBABR exact plan IRQ literal lies outside ROM");
            result.irqOffsets.push_back(offset);
        }
        plan = std::move(result);
        return true;
    }
    return false;
}

} // namespace gbasave
