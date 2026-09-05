#include "gbasave/m36_compat_patcher.h"

#include "gbasave/hole_finder.h"
#include "gbasave/nor_backends.h"
#include "gbasave/storage_layout.h"
#include "gbabr_plan_db.h"
#include "m36_assets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

namespace gbasave {
namespace {

constexpr std::uint32_t kGbaRomBase = 0x08000000u;
constexpr std::size_t GBS2_ORIG=0x10, GBS2_BLOCK=0x14, GBS2_SIZE=0x18, GBS2_BACKUP=0x1C, GBS2_HOTKEY=0x20, GBS2_ENTRY=0x134;
constexpr std::size_t COMPACT_B0=0x0C, COMPACT_SECTORS=0x24, COMPACT_MAGIC=0x28, COMPACT_DISPATCH=0x34;
constexpr std::uint32_t COMPACT_LAYOUT_MAGIC=0x314D4336u;


const NorBackendDescriptor &directBackend()
{
    return norBackendDescriptor(NorFlashType::IntelStatusRegisterWord10);
}

struct StubDesc { std::size_t offset; std::size_t length; std::size_t literal; bool hasLiteral; };
constexpr StubDesc S_EE_READ{0x00,0x20,0,false};
constexpr StubDesc S_FR{0x40,0x20,0x1C,true};
constexpr StubDesc S_FEC{0x60,0x18,0x14,true};
constexpr StubDesc S_FES{0x78,0x1C,0x18,true};
constexpr StubDesc S_FWS{0x94,0x20,0x1C,true};
constexpr StubDesc S_FWB{0xB4,0x1C,0x18,true};
constexpr StubDesc S_ID512{0xD0,8,0,false};
constexpr StubDesc S_ID1M{0xD8,8,0,false};
constexpr StubDesc S_RET0{0xE0,4,0,false};

std::size_t alignUp(std::size_t v, std::size_t a) { return (v+a-1u)&~(a-1u); }
std::uint32_t crc32(const std::vector<std::uint8_t>& b) {
    std::uint32_t c=0xFFFFFFFFu;
    for (auto x:b) { c^=x; for (unsigned i=0;i<8;i++) c=(c>>1u)^(0xEDB88320u&(0u-(c&1u))); }
    return ~c;
}

std::uint8_t gbabrSaveTypeId(SaveType type) {
    // Shared GBABR v6 DAT/JSON IDs are an independent stable wire format and
    // intentionally do not equal GBASaveHandler's C++ enum ordinals.
    switch(type) {
    case SaveType::Sram: return 1u;
    case SaveType::Eeprom512: return 2u;
    case SaveType::Eeprom8K: return 3u;
    case SaveType::Flash512: return 4u;
    case SaveType::Flash1M: return 5u;
    default: return 0u;
    }
}

const generated_gbabr_plan::Entry *planFor(const RomImage &rom) {
    const auto c=crc32(rom.bytes());
    const auto *b=std::begin(generated_gbabr_plan::kEntries), *e=std::end(generated_gbabr_plan::kEntries);
    auto it=std::lower_bound(b,e,c,[](const auto &x,std::uint32_t v){return x.crc32<v;});
    for(;it!=e&&it->crc32==c;++it) if(it->romSize==rom.size()) return it;
    return nullptr;
}

std::uint32_t decodeArmBranchTarget(std::uint32_t instruction) {
    if ((instruction & 0xFF000000u) != 0xEA000000u) throw std::runtime_error("M36 proven route requires ARM B reset entry");
    std::int32_t d=static_cast<std::int32_t>((instruction&0x00FFFFFFu)<<8u)>>6u;
    return static_cast<std::uint32_t>(0x08000008u+d);
}
std::uint32_t armBranch(std::uint32_t from,std::uint32_t to) {
    const std::int64_t d=static_cast<std::int64_t>(to)-static_cast<std::int64_t>(from+8u);
    if((d&3)!=0||d<-0x02000000ll||d>0x01FFFFFCl) throw std::runtime_error("M36 payload reset branch out of range");
    return 0xEA000000u|(static_cast<std::uint32_t>(d>>2u)&0x00FFFFFFu);
}
void write32(std::vector<std::uint8_t>& b,std::size_t o,std::uint32_t v) {
    if(o+4>b.size()) throw std::runtime_error("M36 asset config outside payload");
    b[o]=v; b[o+1]=v>>8; b[o+2]=v>>16; b[o+3]=v>>24;
}
void ensureRom(RomImage &rom,std::size_t end,std::size_t cap) {
    if(end>cap) throw std::runtime_error("M36 hardware-proven route exceeds configured ROM address space");
    if(rom.size()<end) rom.resize(end,0xFFu);
}
void patchStub(RomImage &rom,std::size_t target,const StubDesc &d,std::uint32_t dispatch=0u) {
    if(target+d.length>rom.size()) throw std::runtime_error("GBABR operation patch outside ROM");
    std::vector<std::uint8_t> tmp(generated_m36_assets::kSfwSaveStubs+d.offset,
                                  generated_m36_assets::kSfwSaveStubs+d.offset+d.length);
    if(d.hasLiteral) write32(tmp,d.literal,dispatch);
    rom.write(target,tmp.data(),tmp.size());
}

std::vector<std::size_t> safeSectors(const RomHoleCatalog &holes) {
    const auto &caps = directBackend();
    std::set<std::size_t> uniq;
    for(const auto &r:holes.databaseRegions) {
        if(!r.programOnlyFfSafe()) continue;
        std::size_t p=alignUp(r.range.offset,caps.eraseBlockBytes);
        while(p<=r.range.end()&&caps.eraseBlockBytes<=r.range.end()-p){
            // M36L0R/T main array ends at physical 0x00FDFFFF. The final
            // 0x20000 bytes are four 0x8000 parameter sectors and must never
            // be treated as one normal 128 KiB erase unit by these assets.
            if (p + caps.eraseBlockBytes <= caps.mutableMainArrayEnd)
                uniq.insert(p);
            p+=caps.eraseBlockBytes;
        }
    }
    return {uniq.begin(),uniq.end()};
}

struct M36Layout {
    std::size_t payload{};
    std::vector<std::size_t> storage;
    std::size_t output{};
    std::size_t originalSize{};
    bool payloadInternal{};
};

M36Layout layoutM36(const RomImage &rom,const RomHoleCatalog &holes,std::size_t storageCount,std::size_t cap) {
    const auto &caps = directBackend();
    auto sectors=safeSectors(holes);
    std::size_t payload=0; bool internal=false;
    // Hardware-proven R37A policy: when physical capacity remains, put the
    // compatibility runtime in a fresh dedicated 1 MiB RWW bank after the
    // source ROM. Internal SAFE_CODE+FF placement is only the full-capacity
    // fallback (for example a 16 MiB source on a 16 MiB M36). This is driven
    // solely by NOR capacity and structural hole metadata, never by title.
    const std::size_t appendedBank=alignUp(rom.size(),caps.runtimeExecutionBankBytes);
    if(appendedBank+caps.runtimeExecutionBankBytes<=cap && appendedBank+caps.runtimeExecutionBankBytes<=caps.mutableMainArrayEnd) {
        payload=appendedBank; internal=false;
    } else {
        const auto it=std::find_if(sectors.begin(),sectors.end(),[&caps](std::size_t s){
            const std::size_t bank=s&~(caps.runtimeExecutionBankBytes-1u);
            return bank+caps.runtimeExecutionBankBytes<=caps.mutableMainArrayEnd;
        });
        if(it==sectors.end())
            throw std::runtime_error("M36 payload has no dedicated main-array RWW bank or complete safe sector");
        payload=*it; internal=true;
    }
    const std::size_t payloadBank=payload&~(caps.runtimeExecutionBankBytes-1u);
    std::vector<std::size_t> storage;
    // Match the hardware-proven planner's generic priority: when we could
    // append a fresh RWW bank, keep mutable save storage out of the original
    // image too. Internal structural holes are a full-capacity fallback only.
    if(internal) {
        for(auto s:sectors) {
            if(s==payload || (s&~(caps.runtimeExecutionBankBytes-1u))==payloadBank) continue;
            storage.push_back(s); if(storage.size()==storageCount) break;
        }
    }
    std::size_t tail=alignUp(rom.size(),caps.eraseBlockBytes);
    if(!internal) tail=payload+caps.runtimeExecutionBankBytes;
    while(storage.size()<storageCount) {
        if(tail+caps.eraseBlockBytes>cap || tail+caps.eraseBlockBytes>caps.mutableMainArrayEnd) break;
        if((tail&~(caps.runtimeExecutionBankBytes-1u))!=payloadBank && std::find(storage.begin(),storage.end(),tail)==storage.end()) storage.push_back(tail);
        tail+=caps.eraseBlockBytes;
    }
    if(storage.size()!=storageCount) throw std::runtime_error("M36 save storage cannot be placed outside payload RWW bank");
    std::size_t out=std::max(rom.size(),payload+caps.eraseBlockBytes);
    for(auto s:storage) out=std::max(out,s+caps.eraseBlockBytes);
    return {payload,storage,out,rom.size(),internal};
}

M36Layout layoutM36Sram(const RomImage &rom,const RomHoleCatalog &holes,std::size_t cap) {
    const auto &caps = directBackend();
    auto sectors=safeSectors(holes);
    std::size_t payload=0; bool internal=false;
    const std::size_t appendedBank=alignUp(rom.size(),caps.runtimeExecutionBankBytes);
    if(appendedBank+caps.runtimeExecutionBankBytes<=cap && appendedBank+caps.runtimeExecutionBankBytes<=caps.mutableMainArrayEnd) {
        payload=appendedBank; internal=false;
    } else {
        const auto it=std::find_if(sectors.begin(),sectors.end(),[&caps](std::size_t s){
            const std::size_t bank=s&~(caps.runtimeExecutionBankBytes-1u);
            return bank+caps.runtimeExecutionBankBytes<=caps.mutableMainArrayEnd;
        });
        if(it==sectors.end())
            throw std::runtime_error("M36 SRAM payload has no dedicated main-array RWW bank or complete safe sector");
        payload=*it; internal=true;
    }
    const std::size_t payloadBank=payload&~(caps.runtimeExecutionBankBytes-1u);

    // The hardware-proven GBS2/GBJ4 DUAL3 asset receives only block0 and its
    // original planner reserved block0..block0+0x3FFFF. Preserve that exact
    // physical contract: two consecutive 128 KiB sectors, never arbitrary
    // discontiguous sectors, and never in the payload's 1 MiB execution bank.
    std::set<std::size_t> safe(sectors.begin(),sectors.end());
    std::size_t block=static_cast<std::size_t>(-1);
    if(internal) {
        for(auto s:sectors) {
            if((s&~(caps.runtimeExecutionBankBytes-1u))==payloadBank) continue;
            if(safe.count(s+caps.eraseBlockBytes)!=0u && ((s+caps.eraseBlockBytes)&~(caps.runtimeExecutionBankBytes-1u))!=payloadBank) {
                block=s; break;
            }
        }
    }
    if(block==static_cast<std::size_t>(-1)) {
        std::size_t tail=alignUp(rom.size(),caps.eraseBlockBytes);
        if(!internal) tail=payload+caps.runtimeExecutionBankBytes;
        while(tail+2u*caps.eraseBlockBytes<=cap && tail+2u*caps.eraseBlockBytes<=caps.mutableMainArrayEnd) {
            if((tail&~(caps.runtimeExecutionBankBytes-1u))!=payloadBank &&
               ((tail+caps.eraseBlockBytes)&~(caps.runtimeExecutionBankBytes-1u))!=payloadBank) { block=tail; break; }
            tail+=caps.eraseBlockBytes;
        }
    }
    if(block==static_cast<std::size_t>(-1))
        throw std::runtime_error("M36 SRAM needs one contiguous 0x40000 retention window outside payload RWW bank");
    std::vector<std::size_t> storage{block,block+caps.eraseBlockBytes};
    std::size_t out=std::max({rom.size(),payload+caps.eraseBlockBytes,block+2u*caps.eraseBlockBytes});
    return {payload,storage,out,rom.size(),internal};
}

StorageLayout reportLayout(const M36Layout &m, StorageLayoutKind kind) {
    const auto &caps = directBackend();
    StorageLayout l; l.kind=kind; l.romAddressSpaceBytes=caps.romAddressSpaceLimit; l.physicalEraseBlockBytes=caps.eraseBlockBytes; l.outputSizeBytes=m.output;
    for(std::size_t i=0;i<m.storage.size();++i) l.blocks.push_back({i,m.storage[i],caps.eraseBlockBytes,
        m.storage[i] < m.originalSize ? StoragePlacementSource::InternalFfHole : StoragePlacementSource::AppendedTail,
        m.storage[i] < m.originalSize ? HoleDiscoverySource::GbabrDatabase : HoleDiscoverySource::None});
    return l;
}

void applyRawOp(RomImage &rom,const generated_gbabr_plan::Op &o) {
    if(o.kind==10u) { patchStub(rom,o.offset,S_RET0); return; }
    if(o.kind==11u) {
        if(o.rawOffset+o.rawSize>sizeof(generated_gbabr_plan::kRawBytes)) throw std::runtime_error("GBABR raw operation outside generated table");
        rom.write(o.offset,generated_gbabr_plan::kRawBytes+o.rawOffset,o.rawSize); return;
    }
}

} // namespace

bool canUseM36HardwareProvenRoute(const RomImage &rom,const SaveLibraryMatch &saveLibrary) {
    const auto *p=planFor(rom); return p && p->saveType==gbabrSaveTypeId(saveLibrary.type);
}

PatchReport patchM36HardwareProvenRoute(RomImage &rom,const SaveLibraryMatch &saveLibrary,const PatchOptions &options) {
    if (!options.storagePreserveImage.empty())
        throw std::runtime_error("M36 hardware-proven protocol route does not yet support --preserve-storage-from; refusing silent save overwrite");
    const RomImage original=rom; const auto *p=planFor(original);
    const auto &caps = directBackend();
    const std::size_t m36Capacity = std::min(options.romAddressSpaceBytes, caps.romAddressSpaceLimit);
    if (original.size() > m36Capacity)
        throw std::runtime_error("M36 source ROM exceeds 16 MiB physical NOR capacity");
    if(!p) throw std::runtime_error("M36 proven route requires exact shared GBABR v6 plan");
    if(p->saveType!=gbabrSaveTypeId(saveLibrary.type)) throw std::runtime_error("M36 shared-plan save type disagrees with validated library scan");
    const auto holes=discoverRomHoles(original);
    PatchReport report; report.saveType=saveLibrary.type; report.gbabrDatabaseMatched=holes.gbabrDatabaseMatched; report.gbabrDatabaseVersion=holes.gbabrDatabaseVersion; report.gbabrErasedHoleCount=holes.databaseErasedHoles.size();
    const auto originalEntry=decodeArmBranchTarget(original.u32(0u));

    if(saveLibrary.type==SaveType::Sram) {
        auto m=layoutM36Sram(original,holes,m36Capacity); ensureRom(rom,m.output,m36Capacity);
        std::vector<std::uint8_t> pay(std::begin(generated_m36_assets::kM36SramPayload),std::end(generated_m36_assets::kM36SramPayload));
        write32(pay,GBS2_ORIG,originalEntry); write32(pay,GBS2_BLOCK,static_cast<std::uint32_t>(m.storage[0])); write32(pay,GBS2_SIZE,0x8000u); write32(pay,GBS2_BACKUP,0xFFFFFFFFu); write32(pay,GBS2_HOTKEY,0xF9u);
        rom.write(m.payload,pay.data(),pay.size()); rom.write32(0u,armBranch(kGbaRomBase,kGbaRomBase+static_cast<std::uint32_t>(m.payload+GBS2_ENTRY)));
        const std::array<std::uint8_t,4> chain={0xF4,0x7F,0x00,0x03};
        for(std::size_t i=0;i<p->irqCount;++i) { auto off=generated_gbabr_plan::kIrqs[p->firstIrq+i]; if(original.u32(off)!=0x03007FFCu) throw std::runtime_error("M36 SRAM IRQ op no longer matches exact ROM"); rom.write(off,chain.data(),chain.size()); }
        report.storage=reportLayout(m,StorageLayoutKind::FixedSramMirror); report.runtimeOffset=m.payload; report.runtimeSize=pay.size(); report.runtimePlacementSource=m.payloadInternal?RuntimePlacementSource::InternalSafeHole:RuntimePlacementSource::AppendedTail; report.sramHotkeyOnly=true; report.sramUsesGameOwnedShadowSnapshot=false; report.sramShadowAddress=0x0E000000u; report.routines.push_back({"M36 R37A SRAM GBS2/GBJ4 payload",m.payload,kGbaRomBase+static_cast<std::uint32_t>(m.payload+GBS2_ENTRY)}); return report;
    }

    if(saveLibrary.type==SaveType::Flash512 || saveLibrary.type==SaveType::Flash1M) {
        auto m=layoutM36(original,holes,6u,m36Capacity); ensureRom(rom,m.output,m36Capacity);
        std::vector<std::uint8_t> pay(std::begin(generated_m36_assets::kM36CompactPayload),std::end(generated_m36_assets::kM36CompactPayload));
        for(std::size_t i=0;i<6;++i)
            write32(pay,COMPACT_B0+4u*i,static_cast<std::uint32_t>(m.storage[i]));
        if (saveLibrary.geometry.sectorBytes == 0u ||
            saveLibrary.geometry.totalBytes == 0u ||
            (saveLibrary.geometry.totalBytes % saveLibrary.geometry.sectorBytes) != 0u)
            throw std::runtime_error("M36 FLASH geometry has invalid sector count");
        const std::size_t logicalSectorCount =
            saveLibrary.geometry.totalBytes / saveLibrary.geometry.sectorBytes;
        if (logicalSectorCount == 0u || logicalSectorCount > 32u)
            throw std::runtime_error("M36 compact FLASH engine supports 1..32 logical sectors");
        write32(pay,COMPACT_SECTORS,static_cast<std::uint32_t>(logicalSectorCount));
        write32(pay,COMPACT_MAGIC,COMPACT_LAYOUT_MAGIC);
        rom.write(m.payload,pay.data(),pay.size()); const std::uint32_t dispatch=kGbaRomBase+static_cast<std::uint32_t>(m.payload+COMPACT_DISPATCH);
        for(std::size_t i=0;i<p->opCount;++i){const auto&o=generated_gbabr_plan::kOps[p->firstOp+i]; switch(o.kind){case 3:patchStub(rom,o.offset,S_FR,dispatch);break;case 4:patchStub(rom,o.offset,S_FEC,dispatch);break;case 5:patchStub(rom,o.offset,S_FES,dispatch);break;case 6:patchStub(rom,o.offset,S_FWS,dispatch);break;case 7:patchStub(rom,o.offset,S_FWB,dispatch);break;case 8:patchStub(rom,o.offset,saveLibrary.type==SaveType::Flash1M?S_ID1M:S_ID512);break;case 9:case 10:patchStub(rom,o.offset,S_RET0);break;default:applyRawOp(rom,o);break;}}
        report.storage=reportLayout(m,StorageLayoutKind::FlashVersionedSlots); report.runtimeOffset=m.payload; report.runtimeSize=pay.size(); report.runtimePlacementSource=m.payloadInternal?RuntimePlacementSource::InternalSafeHole:RuntimePlacementSource::AppendedTail; report.flashVersionsPerSector=0u; report.flashSpillBlockCount=0u; report.routines.push_back({"M36 R13G FLASH compact dispatcher",m.payload,dispatch}); return report;
    }

    // EEPROM geometries use the shared record-lane runtime selected by the
    // caller. This asset path is intentionally limited to SRAM and FLASH.
    throw std::runtime_error("M36 compact asset route is only valid for SRAM/FLASH protocols");
}

} // namespace gbasave
