#include "gbasave/embedded/fram_patch.h"
#include "embedded_assets.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace gbasave::embedded_assets;

constexpr uint32_t kGbaRomBase = 0x08000000u;
constexpr uint32_t kPayloadLen = 0x2E0u;
constexpr uint32_t kDispatchOff = 0x30u;
constexpr uint32_t kCfgWindow = 0x0Cu;
constexpr uint32_t kCfgSelector = 0x10u;
constexpr uint32_t kCfgSelectorMask = 0x14u;
constexpr uint32_t kCfgSelectorShift = 0x18u;
constexpr uint32_t kCfgSelectorFixed = 0x1Cu;
constexpr uint32_t kCfgSelectorWidth = 0x20u;
constexpr uint32_t kCfgSectorCount = 0x24u;
constexpr uint32_t kCfgWindowBytes = 0x28u;
constexpr uint32_t kCfgBankCount = 0x2Cu;

struct ReferencePlan {
    uint8_t active{};
    uint8_t save_type{};
    uint8_t route{};
    uint8_t overlay_active{};
    uint32_t original_size{};
    uint32_t virtual_size{};
    uint32_t payload_base{};
    uint32_t payload_bytes{};
    uint32_t logical_save_bytes{};
    GbasaveFramTarget target{};
    SfwSavePlan save_plan{};
};

uint32_t align4(uint32_t value) { return (value + 3u) & ~3u; }

uint32_t mapper_window_for_size(uint32_t bytes, uint32_t capacity) {
    uint32_t window = 0x00080000u;
    if (!bytes || !capacity) return 0u;
    while (window < bytes && window < capacity) {
        if (window > 0x40000000u) return 0u;
        window <<= 1u;
    }
    return window >= bytes && window <= capacity ? window : 0u;
}

int old_reference_plan_build(ReferencePlan *out, const SfwSavePlan *save_plan,
                             const GbasaveFramTarget *target, uint32_t original_size,
                             uint32_t base_virtual_size, uint32_t rom_capacity,
                             int trailing_ff_available, uint32_t trailing_ff_slot) {
    if (!out || !save_plan || !target || !original_size || base_virtual_size < original_size) return GBASAVE_FRAM_PLAN_INVALID;
    *out = ReferencePlan{};
    out->active = 1u;
    out->save_type = save_plan->save_type;
    out->original_size = original_size;
    out->virtual_size = base_virtual_size;
    out->logical_save_bytes = sfw_save_type_size(save_plan->save_type);
    out->target = *target;
    out->save_plan = *save_plan;

    switch (save_plan->save_type) {
    case SFW_SAVE_NONE:
        *out = ReferencePlan{};
        return GBASAVE_FRAM_PLAN_NO_SAVE;
    case SFW_SAVE_SRAM:
        if ((target->capabilities & GBASAVE_FRAM_CAP_DIRECT_SRAM_GAMEPLAY) == 0u) return GBASAVE_FRAM_PLAN_UNSUPPORTED;
        out->route = GBASAVE_FRAM_ROUTE_DIRECT_SRAM;
        break;
    case SFW_SAVE_EEPROM4K:
    case SFW_SAVE_EEPROM64K:
        if ((target->capabilities & GBASAVE_FRAM_CAP_EEPROM_TO_RAM) == 0u) return GBASAVE_FRAM_PLAN_UNSUPPORTED;
        out->route = GBASAVE_FRAM_ROUTE_EEPROM;
        out->overlay_active = 1u;
        break;
    case SFW_SAVE_FLASH512K:
        if ((target->capabilities & GBASAVE_FRAM_CAP_FLASH512_TO_RAM) == 0u ||
            target->gba_window_base == 0u || target->window_bytes < 0x10000u)
            return GBASAVE_FRAM_PLAN_UNSUPPORTED;
        out->route = GBASAVE_FRAM_ROUTE_FLASH512;
        out->overlay_active = 1u;
        break;
    case SFW_SAVE_FLASH1024K:
        if ((target->capabilities & GBASAVE_FRAM_CAP_FLASH1M_TO_BANKED_RAM) == 0u ||
            target->total_bytes < 0x20000u || target->bank_count < 2u)
            return GBASAVE_FRAM_PLAN_UNSUPPORTED;
        out->route = GBASAVE_FRAM_ROUTE_FLASH1M_BANKED;
        out->overlay_active = 1u;
        break;
    default:
        return GBASAVE_FRAM_PLAN_UNSUPPORTED;
    }
    if (out->logical_save_bytes > target->total_bytes) return GBASAVE_FRAM_PLAN_UNSUPPORTED;

    if (out->route == GBASAVE_FRAM_ROUTE_FLASH1M_BANKED) {
        const uint32_t appended_base = align4(out->virtual_size);
        const uint32_t appended_end = align4(appended_base + kPayloadLen);
        const uint32_t current_window = mapper_window_for_size(out->virtual_size, rom_capacity);
        const bool crosses_window = current_window && appended_end > current_window;
        out->payload_bytes = kPayloadLen;
        if (crosses_window && trailing_ff_available) {
            out->payload_base = trailing_ff_slot;
        } else if (appended_end <= rom_capacity) {
            out->payload_base = appended_base;
            out->virtual_size = appended_end;
        } else if (out->virtual_size <= rom_capacity && trailing_ff_available) {
            out->payload_base = trailing_ff_slot;
        } else {
            return GBASAVE_FRAM_PLAN_CAPACITY;
        }
    }
    if (out->virtual_size > rom_capacity) return GBASAVE_FRAM_PLAN_CAPACITY;
    return GBASAVE_FRAM_PLAN_OK;
}

void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t bytes) {
    while (bytes--) *dst++ = *src++;
}
void write32le(uint8_t *dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
    dst[2] = static_cast<uint8_t>(value >> 16);
    dst[3] = static_cast<uint8_t>(value >> 24);
}
void overlay_one(uint32_t off, uint8_t *dst, uint32_t n, uint32_t patch_off, const uint8_t *src, uint32_t len) {
    if (!n || !len) return;
    const uint32_t a = off, b = off + n;
    if (patch_off >= b || patch_off + len <= a) return;
    const uint32_t s = patch_off < a ? a - patch_off : 0u;
    const uint32_t d = patch_off > a ? patch_off - a : 0u;
    uint32_t c = len - s;
    if (c > n - d) c = n - d;
    copy_bytes(dst + d, src + s, c);
}
void overlay_word(uint32_t off, uint8_t *dst, uint32_t n, uint32_t patch_off, uint32_t value) {
    uint8_t bytes[4]; write32le(bytes, value); overlay_one(off, dst, n, patch_off, bytes, 4u);
}
void overlay_stub(uint32_t off, uint8_t *dst, uint32_t n, uint32_t patch_off,
                  uint32_t stub_off, uint32_t len, uint32_t literal_off, uint32_t literal) {
    overlay_one(off, dst, n, patch_off, kEmbeddedSfwSaveStubs + stub_off, len);
    if (literal_off != 0xFFFFFFFFu) overlay_word(off, dst, n, patch_off + literal_off, literal);
}
void overlay_flash512(uint32_t off, uint8_t *dst, uint32_t n, uint32_t patch_off,
                      uint32_t stub_off, uint32_t len, uint32_t window_off, uint32_t window_base) {
    overlay_one(off, dst, n, patch_off, kEmbeddedFramFlash512Inline + stub_off, len);
    overlay_word(off, dst, n, patch_off + window_off, window_base);
}

void old_reference_overlay(const ReferencePlan &plan, uint32_t off, uint8_t *dst, uint32_t n) {
    if (!plan.active || !n || !plan.overlay_active) return;
    const auto &target = plan.target;
    uint32_t dispatcher = 0u;
    if (plan.payload_bytes && plan.route == GBASAVE_FRAM_ROUTE_FLASH1M_BANKED) {
        const uint32_t po = plan.payload_base;
        overlay_one(off, dst, n, po, kEmbeddedFramBankedRuntime, kPayloadLen);
        overlay_word(off, dst, n, po + kCfgWindow, target.gba_window_base);
        overlay_word(off, dst, n, po + kCfgSelector, target.selector_gba_address);
        overlay_word(off, dst, n, po + kCfgSelectorMask, target.selector_mask);
        overlay_word(off, dst, n, po + kCfgSelectorShift, target.selector_shift);
        overlay_word(off, dst, n, po + kCfgSelectorFixed, target.selector_fixed_value);
        overlay_word(off, dst, n, po + kCfgSelectorWidth, target.selector_write_width);
        overlay_word(off, dst, n, po + kCfgSectorCount, plan.logical_save_bytes >> 12u);
        overlay_word(off, dst, n, po + kCfgWindowBytes, target.window_bytes);
        overlay_word(off, dst, n, po + kCfgBankCount, target.bank_count);
        dispatcher = kGbaRomBase + po + kDispatchOff;
    }

    for (uint32_t i = 0; i < plan.save_plan.op_count; ++i) {
        const SfwSaveOp *op = &plan.save_plan.op[i];
        switch (op->kind) {
        case SFW_OP_EEPROM_READ:
            if (plan.route == GBASAVE_FRAM_ROUTE_EEPROM) overlay_stub(off, dst, n, op->offset, 0x00u, 0x20u, 0xFFFFFFFFu, 0u);
            break;
        case SFW_OP_EEPROM_WRITE:
            if (plan.route == GBASAVE_FRAM_ROUTE_EEPROM) overlay_stub(off, dst, n, op->offset, 0x20u, 0x20u, 0xFFFFFFFFu, 0u);
            break;
        case SFW_OP_FLASH_READ:
            if (plan.route == GBASAVE_FRAM_ROUTE_FLASH512) overlay_flash512(off, dst, n, op->offset, 0x00u, 0x20u, 0x1Cu, target.gba_window_base);
            else if (dispatcher) overlay_stub(off, dst, n, op->offset, 0x40u, 0x20u, 0x1Cu, dispatcher);
            break;
        case SFW_OP_FLASH_ERASE_CHIP:
            if (plan.route == GBASAVE_FRAM_ROUTE_FLASH512) overlay_flash512(off, dst, n, op->offset, 0x20u, 0x1Cu, 0x18u, target.gba_window_base);
            else if (dispatcher) overlay_stub(off, dst, n, op->offset, 0x60u, 0x18u, 0x14u, dispatcher);
            break;
        case SFW_OP_FLASH_ERASE_SECTOR:
            if (plan.route == GBASAVE_FRAM_ROUTE_FLASH512) overlay_flash512(off, dst, n, op->offset, 0x3Cu, 0x24u, 0x20u, target.gba_window_base);
            else if (dispatcher) overlay_stub(off, dst, n, op->offset, 0x78u, 0x1Cu, 0x18u, dispatcher);
            break;
        case SFW_OP_FLASH_WRITE_SECTOR:
            if (plan.route == GBASAVE_FRAM_ROUTE_FLASH512) overlay_flash512(off, dst, n, op->offset, 0x60u, 0x2Cu, 0x28u, target.gba_window_base);
            else if (dispatcher) overlay_stub(off, dst, n, op->offset, 0x94u, 0x20u, 0x1Cu, dispatcher);
            break;
        case SFW_OP_FLASH_WRITE_BYTE:
            if (plan.route == GBASAVE_FRAM_ROUTE_FLASH512) overlay_flash512(off, dst, n, op->offset, 0x8Cu, 0x14u, 0x10u, target.gba_window_base);
            else if (dispatcher) overlay_stub(off, dst, n, op->offset, 0xB4u, 0x1Cu, 0x18u, dispatcher);
            break;
        case SFW_OP_FLASH_IDENT:
            if (plan.route == GBASAVE_FRAM_ROUTE_FLASH512) overlay_stub(off, dst, n, op->offset, 0xD0u, 0x08u, 0xFFFFFFFFu, 0u);
            else if (dispatcher) {
                if (plan.save_type == SFW_SAVE_FLASH1024K) overlay_stub(off, dst, n, op->offset, 0xD8u, 0x08u, 0xFFFFFFFFu, 0u);
                else overlay_stub(off, dst, n, op->offset, 0xD0u, 0x08u, 0xFFFFFFFFu, 0u);
            }
            break;
        case SFW_OP_FLASH_VERIFY:
        case SFW_OP_RAW_THUMB_RET0:
            overlay_stub(off, dst, n, op->offset, 0xE0u, 0x04u, 0xFFFFFFFFu, 0u);
            break;
        case SFW_OP_RAW_BYTES:
            overlay_one(off, dst, n, op->offset, op->raw, op->raw_len);
            break;
        default:
            break;
        }
    }
}

SfwSavePlan make_plan(uint8_t save_type) {
    SfwSavePlan p{};
    sfw_saveplan_init(&p, 0x00800000u);
    p.save_type = save_type;
    auto add = [&](uint8_t kind, uint32_t off) {
        SfwSaveOp &op = p.op[p.op_count++];
        op.kind = kind;
        op.offset = off;
    };
    if (save_type == SFW_SAVE_EEPROM4K || save_type == SFW_SAVE_EEPROM64K) {
        add(SFW_OP_EEPROM_READ, 0x1003u);
        add(SFW_OP_EEPROM_WRITE, 0x1FF1u);
        add(SFW_OP_RAW_THUMB_RET0, 0x2FFEu);
    } else if (save_type == SFW_SAVE_FLASH512K || save_type == SFW_SAVE_FLASH1024K) {
        add(SFW_OP_FLASH_READ, 0x1003u);
        add(SFW_OP_FLASH_ERASE_CHIP, 0x1FF1u);
        add(SFW_OP_FLASH_ERASE_SECTOR, 0x2FFEu);
        add(SFW_OP_FLASH_WRITE_SECTOR, 0x4017u);
        add(SFW_OP_FLASH_WRITE_BYTE, 0x50FDu);
        add(SFW_OP_FLASH_IDENT, 0x6011u);
        add(SFW_OP_FLASH_VERIFY, 0x7002u);
        add(SFW_OP_RAW_THUMB_RET0, 0x80FFu);
    }
    SfwSaveOp &raw = p.op[p.op_count++];
    raw.kind = SFW_OP_RAW_BYTES;
    raw.offset = 0x9123u;
    raw.raw_len = 13u;
    for (uint32_t i = 0; i < raw.raw_len; ++i) raw.raw[i] = static_cast<uint8_t>(0xD0u + i);
    return p;
}

GbasaveFramTarget make_target() {
    GbasaveFramTarget t{};
    t.capabilities = GBASAVE_FRAM_CAP_NONVOLATILE | GBASAVE_FRAM_CAP_BYTE_RW |
        GBASAVE_FRAM_CAP_SRAM_WINDOW | GBASAVE_FRAM_CAP_BANKED_WINDOW |
        GBASAVE_FRAM_CAP_DIRECT_SRAM_GAMEPLAY | GBASAVE_FRAM_CAP_EEPROM_TO_RAM |
        GBASAVE_FRAM_CAP_FLASH512_TO_RAM | GBASAVE_FRAM_CAP_FLASH1M_TO_BANKED_RAM;
    t.total_bytes = 0x20000u;
    t.window_bytes = 0x10000u;
    t.gba_window_base = 0x0E000000u;
    t.selector_gba_address = 0x09000000u;
    t.selector_fixed_value = 0u;
    t.technology = GBASAVE_SAVE_MEMORY_TECH_FRAM;
    t.bank_count = 2u;
    t.selector_kind = GBASAVE_SAVE_MEMORY_SELECTOR_ROM_WRITE_DATA_BITS;
    t.selector_mask = 1u;
    t.selector_shift = 0u;
    t.selector_write_width = 16u;
    return t;
}

bool plans_equal(const ReferencePlan &a, const GbasaveFramPatchPlan &b) {
    return a.active == b.active && a.save_type == b.save_type && a.route == b.route &&
        a.overlay_active == b.overlay_active && a.original_size == b.original_size &&
        a.virtual_size == b.virtual_size && a.payload_base == b.payload_base &&
        a.payload_bytes == b.payload_bytes && a.logical_save_bytes == b.logical_save_bytes &&
        std::memcmp(&a.target, &b.target, sizeof(a.target)) == 0 &&
        std::memcmp(&a.save_plan, &b.save_plan, sizeof(a.save_plan)) == 0;
}

int compare_overlay_case(uint8_t save_type, uint32_t base_virtual_size, uint32_t capacity,
                         int trailing, uint32_t trailing_slot) {
    const auto target = make_target();
    const auto save = make_plan(save_type);
    ReferencePlan ref{};
    GbasaveFramPatchPlan got{};
    const int rr = old_reference_plan_build(&ref, &save, &target, 0x00400000u, base_virtual_size,
                                            capacity, trailing, trailing_slot);
    const int gr = gbasave_fram_patch_plan_build(&got, &save, &target, 0x00400000u, base_virtual_size,
                                                 capacity, trailing, trailing_slot);
    if (rr != gr) {
        std::fprintf(stderr, "plan result mismatch save=%u ref=%d got=%d\n", save_type, rr, gr);
        return 1;
    }
    if (rr != GBASAVE_FRAM_PLAN_OK) return 0;
    if (!plans_equal(ref, got)) {
        std::fprintf(stderr, "plan fields mismatch save=%u\n", save_type);
        return 2;
    }

    std::vector<uint32_t> interesting{0u, 0x0FF0u, 0x1000u, 0x1FE0u, 0x2FE0u, 0x4000u,
                                      0x50E0u, 0x6000u, 0x7000u, 0x80E0u, 0x9110u};
    if (got.payload_bytes) {
        interesting.push_back(got.payload_base > 32u ? got.payload_base - 32u : 0u);
        interesting.push_back(got.payload_base);
        interesting.push_back(got.payload_base + 0x20u);
        interesting.push_back(got.payload_base + got.payload_bytes - 32u);
    }
    const std::array<uint32_t, 12> lengths{{1u, 2u, 3u, 4u, 7u, 15u, 31u, 64u, 127u, 257u, 1024u, 4096u}};
    for (uint32_t off : interesting) {
        for (uint32_t n : lengths) {
            std::vector<uint8_t> expected(n), actual(n);
            for (uint32_t i = 0; i < n; ++i) expected[i] = actual[i] = static_cast<uint8_t>((off + i) * 37u + 11u);
            old_reference_overlay(ref, off, expected.data(), n);
            gbasave_fram_patch_apply_overlay(&got, off, actual.data(), n);
            if (expected != actual) {
                uint32_t first = 0;
                while (first < n && expected[first] == actual[first]) ++first;
                std::fprintf(stderr, "overlay mismatch save=%u off=%08X n=%u first=%u ref=%02X got=%02X\n",
                             save_type, off, n, first,
                             first < n ? expected[first] : 0u, first < n ? actual[first] : 0u);
                return 3;
            }
        }
    }
    return 0;
}

} // namespace

int main() {
    static_assert(kEmbeddedSfwSaveStubsSize == 0xE4u, "legacy stubs size");
    static_assert(kEmbeddedFramFlash512InlineSize == 0xA0u, "legacy F512 size");
    static_assert(kEmbeddedFramBankedRuntimeSize == kPayloadLen, "legacy banked runtime size");

    for (uint8_t save : {uint8_t(SFW_SAVE_SRAM), uint8_t(SFW_SAVE_EEPROM4K), uint8_t(SFW_SAVE_EEPROM64K),
                         uint8_t(SFW_SAVE_FLASH512K)}) {
        if (int rc = compare_overlay_case(save, 0x00400000u, 0x01000000u, 0, 0u)) return rc;
    }
    // FLASH1M placement policies from the old GBABR implementation.
    if (int rc = compare_overlay_case(SFW_SAVE_FLASH1024K, 0x00800000u, 0x01000000u, 1, 0x007FF000u)) return rc;
    if (int rc = compare_overlay_case(SFW_SAVE_FLASH1024K, 0x00800000u, 0x01000000u, 0, 0u)) return rc;
    if (int rc = compare_overlay_case(SFW_SAVE_FLASH1024K, 0x00FFFF00u, 0x01000000u, 1, 0x00FFE000u)) return rc;

    // Capacity refusal parity.
    {
        const auto target = make_target();
        const auto save = make_plan(SFW_SAVE_FLASH1024K);
        ReferencePlan ref{}; GbasaveFramPatchPlan got{};
        int rr = old_reference_plan_build(&ref, &save, &target, 0x00400000u, 0x00FFFF00u, 0x01000000u, 0, 0u);
        int gr = gbasave_fram_patch_plan_build(&got, &save, &target, 0x00400000u, 0x00FFFF00u, 0x01000000u, 0, 0u);
        if (rr != GBASAVE_FRAM_PLAN_CAPACITY || gr != rr) {
            std::fprintf(stderr, "capacity refusal mismatch ref=%d got=%d\n", rr, gr);
            return 4;
        }
    }

    std::puts("PASS embedded FRAM parity: route planning, payload placement and byte overlays match GBABR v2.1 reference semantics");
    return 0;
}
