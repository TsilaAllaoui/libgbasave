#include "gbasave/embedded/save_memory_patch.h"
#include "embedded_assets.h"

namespace {
using namespace gbasave::embedded_assets;

constexpr uint32_t kGbaRomBase = 0x08000000u;
constexpr uint32_t kMinimumMapperWindow = 0x00080000u;

// sfw_save_stubs.bin ABI frozen from the hardware-proven GBABR runtime.
constexpr uint32_t kEepromReadOff=0x00u, kEepromReadLen=0x20u;
constexpr uint32_t kEepromWriteOff=0x20u, kEepromWriteLen=0x20u;
constexpr uint32_t kFlashReadOff=0x40u, kFlashReadLen=0x20u, kFlashReadDispatch=0x1Cu;
constexpr uint32_t kFlashEraseChipOff=0x60u, kFlashEraseChipLen=0x18u, kFlashEraseChipDispatch=0x14u;
constexpr uint32_t kFlashEraseSectorOff=0x78u, kFlashEraseSectorLen=0x1Cu, kFlashEraseSectorDispatch=0x18u;
constexpr uint32_t kFlashWriteSectorOff=0x94u, kFlashWriteSectorLen=0x20u, kFlashWriteSectorDispatch=0x1Cu;
constexpr uint32_t kFlashWriteByteOff=0xB4u, kFlashWriteByteLen=0x1Cu, kFlashWriteByteDispatch=0x18u;
constexpr uint32_t kFlashIdent512Off=0xD0u, kFlashIdent512Len=0x08u;
constexpr uint32_t kFlashIdent1MOff=0xD8u, kFlashIdent1MLen=0x08u;
constexpr uint32_t kThumbRet0Off=0xE0u, kThumbRet0Len=0x04u;

// fram_flash512_inline.bin ABI.
constexpr uint32_t kF512ReadOff=0x00u, kF512ReadLen=0x20u, kF512ReadWindow=0x1Cu;
constexpr uint32_t kF512EraseChipOff=0x20u, kF512EraseChipLen=0x1Cu, kF512EraseChipWindow=0x18u;
constexpr uint32_t kF512EraseSectorOff=0x3Cu, kF512EraseSectorLen=0x24u, kF512EraseSectorWindow=0x20u;
constexpr uint32_t kF512WriteSectorOff=0x60u, kF512WriteSectorLen=0x2Cu, kF512WriteSectorWindow=0x28u;
constexpr uint32_t kF512WriteByteOff=0x8Cu, kF512WriteByteLen=0x14u, kF512WriteByteWindow=0x10u;

// fram_banked_runtime.bin ABI.
constexpr uint32_t kFramDispatchOff=0x30u;
constexpr uint32_t kFramCfgWindow=0x0Cu;
constexpr uint32_t kFramCfgSelector=0x10u;
constexpr uint32_t kFramCfgSelectorMask=0x14u;
constexpr uint32_t kFramCfgSelectorShift=0x18u;
constexpr uint32_t kFramCfgSelectorFixed=0x1Cu;
constexpr uint32_t kFramCfgSelectorWidth=0x20u;
constexpr uint32_t kFramCfgSectorCount=0x24u;
constexpr uint32_t kFramCfgWindowBytes=0x28u;
constexpr uint32_t kFramCfgBankCount=0x2Cu;

static_assert(kEmbeddedSfwSaveStubsSize == 0xE4u, "unexpected save stub ABI");
static_assert(kEmbeddedFramFlash512InlineSize == 0xA0u, "unexpected FLASH512 FRAM ABI");
static_assert(kEmbeddedFramBankedRuntimeSize == 0x2E0u, "unexpected banked FRAM ABI");

uint32_t align4(uint32_t value) { return (value + 3u) & ~3u; }

void zero_bytes(void *ptr, uint32_t bytes) {
    auto *p=static_cast<uint8_t*>(ptr); while(bytes--) *p++=0u;
}
void copy_bytes(uint8_t *dst,const uint8_t *src,uint32_t bytes) {
    while(bytes--) *dst++=*src++;
}
void write32le(uint8_t *dst,uint32_t value) {
    dst[0]=static_cast<uint8_t>(value); dst[1]=static_cast<uint8_t>(value>>8);
    dst[2]=static_cast<uint8_t>(value>>16); dst[3]=static_cast<uint8_t>(value>>24);
}

uint32_t mapper_window_for_size(uint32_t bytes,uint32_t capacity) {
    uint32_t window=kMinimumMapperWindow;
    if(bytes==0u||capacity==0u) return 0u;
    while(window<bytes&&window<capacity) {
        if(window>0x40000000u) return 0u;
        window<<=1u;
    }
    return window>=bytes&&window<=capacity?window:0u;
}

void overlay_bytes(uint32_t chunkOff,uint8_t *dst,uint32_t chunkBytes,uint32_t patchOff,const uint8_t *src,uint32_t len) {
    if(chunkBytes==0u||len==0u) return;
    const uint32_t chunkEnd=chunkOff+chunkBytes;
    if(patchOff>=chunkEnd||patchOff+len<=chunkOff) return;
    const uint32_t sourceSkip=patchOff<chunkOff?chunkOff-patchOff:0u;
    const uint32_t destOff=patchOff>chunkOff?patchOff-chunkOff:0u;
    uint32_t count=len-sourceSkip;
    if(count>chunkBytes-destOff) count=chunkBytes-destOff;
    copy_bytes(dst+destOff,src+sourceSkip,count);
}

void overlay_word(uint32_t chunkOff,uint8_t *dst,uint32_t chunkBytes,uint32_t patchOff,uint32_t value) {
    uint8_t bytes[4]; write32le(bytes,value); overlay_bytes(chunkOff,dst,chunkBytes,patchOff,bytes,4u);
}

void overlay_stub(uint32_t chunkOff,uint8_t *dst,uint32_t chunkBytes,uint32_t patchOff,
                  uint32_t stubOff,uint32_t stubLen,uint32_t literalOff,uint32_t literal) {
    overlay_bytes(chunkOff,dst,chunkBytes,patchOff,kEmbeddedSfwSaveStubs+stubOff,stubLen);
    if(literalOff!=0xFFFFFFFFu) overlay_word(chunkOff,dst,chunkBytes,patchOff+literalOff,literal);
}

void overlay_flash512(uint32_t chunkOff,uint8_t *dst,uint32_t chunkBytes,uint32_t patchOff,
                      uint32_t stubOff,uint32_t stubLen,uint32_t windowOff,uint32_t windowBase) {
    overlay_bytes(chunkOff,dst,chunkBytes,patchOff,kEmbeddedFramFlash512Inline+stubOff,stubLen);
    overlay_word(chunkOff,dst,chunkBytes,patchOff+windowOff,windowBase);
}

bool has_cap(const GbasaveSaveMemoryTarget &target,uint32_t cap) { return (target.capabilities&cap)!=0u; }

} // namespace

extern "C" {

int gbasave_save_memory_patch_plan_build(
    GbasaveSaveMemoryPatchPlan *out,
    const SfwSavePlan *savePlan,
    const GbasaveSaveMemoryTarget *target,
    uint32_t originalSize,
    uint32_t baseVirtualSize,
    uint32_t romCapacity,
    int trailingFfAvailable,
    uint32_t trailingFfSlot)
{
    if(!out||!savePlan||!target||originalSize==0u||baseVirtualSize<originalSize) return GBASAVE_SAVE_MEMORY_PLAN_INVALID;
    zero_bytes(out,sizeof(*out));
    out->save_type=savePlan->save_type;
    out->original_size=originalSize;
    out->virtual_size=baseVirtualSize;
    out->logical_save_bytes=sfw_save_type_size(savePlan->save_type);
    out->target=*target;
    out->save_plan=*savePlan;
    if(savePlan->save_type==SFW_SAVE_NONE||out->logical_save_bytes==0u) return GBASAVE_SAVE_MEMORY_PLAN_NO_SAVE;
    if(out->logical_save_bytes>target->total_bytes) return GBASAVE_SAVE_MEMORY_PLAN_UNSUPPORTED;

    switch(savePlan->save_type) {
    case SFW_SAVE_SRAM:
        if(!has_cap(*target,GBASAVE_SAVE_MEMORY_CAP_DIRECT_SRAM_GAMEPLAY) ||
           !has_cap(*target,GBASAVE_SAVE_MEMORY_CAP_SRAM_WINDOW) ||
           target->gba_window_base==0u || out->logical_save_bytes>target->window_bytes)
            return GBASAVE_SAVE_MEMORY_PLAN_UNSUPPORTED;
        out->route=GBASAVE_SAVE_MEMORY_ROUTE_DIRECT_SRAM;
        break;
    case SFW_SAVE_EEPROM4K:
    case SFW_SAVE_EEPROM64K:
        if(!has_cap(*target,GBASAVE_SAVE_MEMORY_CAP_EEPROM_TO_RAM) ||
           !has_cap(*target,GBASAVE_SAVE_MEMORY_CAP_SRAM_WINDOW) ||
           target->gba_window_base==0u || out->logical_save_bytes>target->window_bytes)
            return GBASAVE_SAVE_MEMORY_PLAN_UNSUPPORTED;
        out->route=GBASAVE_SAVE_MEMORY_ROUTE_EEPROM; out->overlay_active=1u;
        break;
    case SFW_SAVE_FLASH512K:
        if(!has_cap(*target,GBASAVE_SAVE_MEMORY_CAP_FLASH512_TO_RAM)||target->gba_window_base==0u||target->window_bytes<0x10000u)
            return GBASAVE_SAVE_MEMORY_PLAN_UNSUPPORTED;
        out->route=GBASAVE_SAVE_MEMORY_ROUTE_FLASH512; out->overlay_active=1u;
        break;
    case SFW_SAVE_FLASH1024K:
        if(!has_cap(*target,GBASAVE_SAVE_MEMORY_CAP_FLASH1M_TO_BANKED_RAM) ||
           !has_cap(*target,GBASAVE_SAVE_MEMORY_CAP_BANKED_WINDOW) ||
           target->total_bytes<0x20000u || target->window_bytes<0x10000u || target->bank_count<2u ||
           target->selector_kind==GBASAVE_SAVE_MEMORY_SELECTOR_NONE || target->selector_gba_address==0u)
            return GBASAVE_SAVE_MEMORY_PLAN_UNSUPPORTED;
        out->route=GBASAVE_SAVE_MEMORY_ROUTE_FLASH1M_BANKED; out->overlay_active=1u;
        out->payload_bytes=kEmbeddedFramBankedRuntimeSize;
        break;
    default:
        return GBASAVE_SAVE_MEMORY_PLAN_UNSUPPORTED;
    }

    if(out->payload_bytes!=0u) {
        const uint32_t appendBase=align4(baseVirtualSize);
        const uint32_t appendEnd=align4(appendBase+out->payload_bytes);
        const uint32_t currentWindow=mapper_window_for_size(baseVirtualSize,romCapacity);
        const bool crossesWindow=currentWindow!=0u&&appendEnd>currentWindow;
        if(crossesWindow&&trailingFfAvailable) {
            out->payload_base=trailingFfSlot;
        } else if(appendEnd<=romCapacity) {
            out->payload_base=appendBase; out->virtual_size=appendEnd;
        } else if(baseVirtualSize<=romCapacity&&trailingFfAvailable) {
            out->payload_base=trailingFfSlot;
        } else {
            return GBASAVE_SAVE_MEMORY_PLAN_CAPACITY;
        }
    }
    if(out->virtual_size>romCapacity) return GBASAVE_SAVE_MEMORY_PLAN_CAPACITY;
    out->active=1u;
    return GBASAVE_SAVE_MEMORY_PLAN_OK;
}

void gbasave_save_memory_patch_apply_overlay(const GbasaveSaveMemoryPatchPlan *plan,uint32_t off,uint8_t *dst,uint32_t n)
{
    if(!plan||!plan->active||!dst||n==0u||!plan->overlay_active) return;
    uint32_t dispatcher=0u;
    const auto &target=plan->target;
    if(plan->payload_bytes&&plan->route==GBASAVE_SAVE_MEMORY_ROUTE_FLASH1M_BANKED) {
        const uint32_t po=plan->payload_base;
        overlay_bytes(off,dst,n,po,kEmbeddedFramBankedRuntime,kEmbeddedFramBankedRuntimeSize);
        overlay_word(off,dst,n,po+kFramCfgWindow,target.gba_window_base);
        overlay_word(off,dst,n,po+kFramCfgSelector,target.selector_gba_address);
        overlay_word(off,dst,n,po+kFramCfgSelectorMask,target.selector_mask);
        overlay_word(off,dst,n,po+kFramCfgSelectorShift,target.selector_shift);
        overlay_word(off,dst,n,po+kFramCfgSelectorFixed,target.selector_fixed_value);
        overlay_word(off,dst,n,po+kFramCfgSelectorWidth,target.selector_write_width);
        overlay_word(off,dst,n,po+kFramCfgSectorCount,plan->logical_save_bytes>>12u);
        overlay_word(off,dst,n,po+kFramCfgWindowBytes,target.window_bytes);
        overlay_word(off,dst,n,po+kFramCfgBankCount,target.bank_count);
        dispatcher=kGbaRomBase+po+kFramDispatchOff;
    }

    for(uint32_t i=0u;i<plan->save_plan.op_count;i++) {
        const SfwSaveOp *op=&plan->save_plan.op[i];
        switch(op->kind) {
        case SFW_OP_EEPROM_READ:
            if(plan->route==GBASAVE_SAVE_MEMORY_ROUTE_EEPROM) overlay_stub(off,dst,n,op->offset,kEepromReadOff,kEepromReadLen,0xFFFFFFFFu,0u);
            break;
        case SFW_OP_EEPROM_WRITE:
            if(plan->route==GBASAVE_SAVE_MEMORY_ROUTE_EEPROM) overlay_stub(off,dst,n,op->offset,kEepromWriteOff,kEepromWriteLen,0xFFFFFFFFu,0u);
            break;
        case SFW_OP_FLASH_READ:
            if(plan->route==GBASAVE_SAVE_MEMORY_ROUTE_FLASH512) overlay_flash512(off,dst,n,op->offset,kF512ReadOff,kF512ReadLen,kF512ReadWindow,target.gba_window_base);
            else if(dispatcher) overlay_stub(off,dst,n,op->offset,kFlashReadOff,kFlashReadLen,kFlashReadDispatch,dispatcher);
            break;
        case SFW_OP_FLASH_ERASE_CHIP:
            if(plan->route==GBASAVE_SAVE_MEMORY_ROUTE_FLASH512) overlay_flash512(off,dst,n,op->offset,kF512EraseChipOff,kF512EraseChipLen,kF512EraseChipWindow,target.gba_window_base);
            else if(dispatcher) overlay_stub(off,dst,n,op->offset,kFlashEraseChipOff,kFlashEraseChipLen,kFlashEraseChipDispatch,dispatcher);
            break;
        case SFW_OP_FLASH_ERASE_SECTOR:
            if(plan->route==GBASAVE_SAVE_MEMORY_ROUTE_FLASH512) overlay_flash512(off,dst,n,op->offset,kF512EraseSectorOff,kF512EraseSectorLen,kF512EraseSectorWindow,target.gba_window_base);
            else if(dispatcher) overlay_stub(off,dst,n,op->offset,kFlashEraseSectorOff,kFlashEraseSectorLen,kFlashEraseSectorDispatch,dispatcher);
            break;
        case SFW_OP_FLASH_WRITE_SECTOR:
            if(plan->route==GBASAVE_SAVE_MEMORY_ROUTE_FLASH512) overlay_flash512(off,dst,n,op->offset,kF512WriteSectorOff,kF512WriteSectorLen,kF512WriteSectorWindow,target.gba_window_base);
            else if(dispatcher) overlay_stub(off,dst,n,op->offset,kFlashWriteSectorOff,kFlashWriteSectorLen,kFlashWriteSectorDispatch,dispatcher);
            break;
        case SFW_OP_FLASH_WRITE_BYTE:
            if(plan->route==GBASAVE_SAVE_MEMORY_ROUTE_FLASH512) overlay_flash512(off,dst,n,op->offset,kF512WriteByteOff,kF512WriteByteLen,kF512WriteByteWindow,target.gba_window_base);
            else if(dispatcher) overlay_stub(off,dst,n,op->offset,kFlashWriteByteOff,kFlashWriteByteLen,kFlashWriteByteDispatch,dispatcher);
            break;
        case SFW_OP_FLASH_IDENT:
            if(plan->route==GBASAVE_SAVE_MEMORY_ROUTE_FLASH512) overlay_stub(off,dst,n,op->offset,kFlashIdent512Off,kFlashIdent512Len,0xFFFFFFFFu,0u);
            else if(dispatcher) {
                if(plan->save_type==SFW_SAVE_FLASH1024K) overlay_stub(off,dst,n,op->offset,kFlashIdent1MOff,kFlashIdent1MLen,0xFFFFFFFFu,0u);
                else overlay_stub(off,dst,n,op->offset,kFlashIdent512Off,kFlashIdent512Len,0xFFFFFFFFu,0u);
            }
            break;
        case SFW_OP_FLASH_VERIFY:
        case SFW_OP_RAW_THUMB_RET0:
            overlay_stub(off,dst,n,op->offset,kThumbRet0Off,kThumbRet0Len,0xFFFFFFFFu,0u);
            break;
        case SFW_OP_RAW_BYTES:
            overlay_bytes(off,dst,n,op->offset,op->raw,op->raw_len);
            break;
        default: break;
        }
    }
}

int gbasave_save_memory_patch_trailing_ff_probe_needed(
    const SfwSavePlan *savePlan,uint32_t baseVirtualSize,uint32_t romCapacity)
{
    if(!savePlan||savePlan->save_type!=SFW_SAVE_FLASH1024K||baseVirtualSize==0u||romCapacity==0u) return 0;
    const uint32_t appendBase=align4(baseVirtualSize);
    const uint32_t appendEnd=align4(appendBase+kEmbeddedFramBankedRuntimeSize);
    const uint32_t currentWindow=mapper_window_for_size(baseVirtualSize,romCapacity);
    const bool crossesWindow=currentWindow!=0u&&appendEnd>currentWindow;
    return (crossesWindow||appendEnd>romCapacity)?1:0;
}

const char *gbasave_save_memory_route_name(uint8_t route)
{
    switch(route) {
    case GBASAVE_SAVE_MEMORY_ROUTE_DIRECT_SRAM: return "DIRECT_SRAM_WINDOW";
    case GBASAVE_SAVE_MEMORY_ROUTE_EEPROM: return "EEPROM_TO_SRAM_WINDOW";
    case GBASAVE_SAVE_MEMORY_ROUTE_FLASH512: return "FLASH512_TO_SRAM_WINDOW";
    case GBASAVE_SAVE_MEMORY_ROUTE_FLASH1M_BANKED: return "FLASH1M_TO_BANKED_SRAM_WINDOW";
    default: return "NONE";
    }
}

uint32_t gbasave_save_memory_banked_runtime_size(void) { return kEmbeddedFramBankedRuntimeSize; }

} // extern "C"
