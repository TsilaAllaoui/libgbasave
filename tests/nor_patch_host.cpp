#include "gbasave/embedded/nor_patch.h"
#include "direct_rww_assets.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gbasave::generated_direct_rww_assets;
static void wr32(uint8_t*p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static uint32_t branch(uint32_t from,uint32_t to){int64_t d=int64_t(to)-int64_t(from+8u);return 0xEA000000u|(uint32_t(d>>2)&0x00FFFFFFu);}
static void put(std::vector<uint8_t>&v,uint32_t off,const uint8_t*s,uint32_t n){if(off+n>v.size())v.resize(off+n,0xFF);std::copy(s,s+n,v.begin()+off);}
static void put32(std::vector<uint8_t>&v,uint32_t off,uint32_t x){uint8_t b[4];wr32(b,x);put(v,off,b,4);}
static void addop(GbasaveExactRomPlan&p,uint8_t kind,uint32_t off){auto&o=p.save_plan.op[p.save_plan.op_count++]; std::memset(&o,0,sizeof(o));o.kind=kind;o.offset=off;}
static void rawop(GbasaveExactRomPlan&p,uint32_t off){auto&o=p.save_plan.op[p.save_plan.op_count++];std::memset(&o,0,sizeof(o));o.kind=SFW_OP_RAW_BYTES;o.offset=off;o.raw_len=5;for(int i=0;i<5;i++)o.raw[i]=uint8_t(0xA0+i);}
static void stub(std::vector<uint8_t>&v,uint32_t off,size_t so,size_t len,size_t disp,uint32_t dispatcher,bool has){std::vector<uint8_t> t(kSfwSaveStubs+so,kSfwSaveStubs+so+len);if(has)wr32(t.data()+disp,dispatcher);put(v,off,t.data(),t.size());}
static bool same(const char*n,const std::vector<uint8_t>&a,const std::vector<uint8_t>&b){if(a==b)return true;size_t i=0;while(i<a.size()&&i<b.size()&&a[i]==b[i])i++;std::fprintf(stderr,"%s mismatch @0x%zx got=%02X exp=%02X\n",n,i,i<a.size()?a[i]:0,i<b.size()?b[i]:0);return false;}

static bool run(uint8_t saveType,uint8_t layoutRoute,const char*name){
 GbasaveExactRomPlan p{};p.rom_size=0x20000;p.entry_word=0xEA00002Eu;p.save_type=saveType;p.ready=1;p.save_plan.filesize=p.rom_size;p.save_plan.save_type=saveType;
 GbasaveNorSaveLayout l{};l.route=layoutRoute;l.payload_offset=0x24000;l.payload_reserved_bytes=0x2000;l.storage_block_bytes=0x20000;l.output_bytes=0x100000;l.minimum_capacity_bytes=0x100000;
 if(layoutRoute==GBASAVE_NOR_SAVE_LAYOUT_RWW_SRAM){l.storage_count=1;l.storage[0]=0x60000;p.save_plan.irq_count=2;p.save_plan.irq_offset[0]=0x100;p.save_plan.irq_offset[1]=0x1FFC;}
 else if(layoutRoute==GBASAVE_NOR_SAVE_LAYOUT_RWW_EEPROM){l.storage_count=2;l.storage[0]=0x60000;l.storage[1]=0x80000;p.save_plan.irq_count=2;p.save_plan.irq_offset[0]=0x104;p.save_plan.irq_offset[1]=0x1FF8;addop(p,SFW_OP_EEPROM_READ,0x300);addop(p,SFW_OP_EEPROM_WRITE,0x3F0);rawop(p,0x510);}
 else {l.storage_count=6;for(int i=0;i<6;i++)l.storage[i]=0x40000+i*0x10000;addop(p,SFW_OP_FLASH_READ,0x300);addop(p,SFW_OP_FLASH_ERASE_CHIP,0x380);addop(p,SFW_OP_FLASH_ERASE_SECTOR,0x3C0);addop(p,SFW_OP_FLASH_WRITE_SECTOR,0x440);addop(p,SFW_OP_FLASH_WRITE_BYTE,0x4C0);addop(p,SFW_OP_FLASH_IDENT,0x520);addop(p,SFW_OP_FLASH_VERIFY,0x560);addop(p,SFW_OP_RAW_THUMB_RET0,0x590);rawop(p,0x5C0);}
 uint8_t payload[GBASAVE_NOR_PATCH_MAX_PAYLOAD_BYTES]{};GbasaveNorPatchPlan np{};if(gbasave_nor_patch_plan_build(&p,&l,0x1234,payload,sizeof(payload),&np)!=GBASAVE_NOR_PATCH_READY){std::fprintf(stderr,"%s plan failed\n",name);return false;}
 std::vector<uint8_t> exp(l.output_bytes,0xFF), got=exp;
 const uint32_t orig=0x080000C0u; // EA00002E from 08000000 -> 080000C8
 if(np.original_entry_address!=orig){std::fprintf(stderr,"%s original entry %08X\n",name,np.original_entry_address);return false;}
 if(layoutRoute==GBASAVE_NOR_SAVE_LAYOUT_RWW_SRAM){std::vector<uint8_t> q(kSramRwwPayload,kSramRwwPayload+kSramRwwPayloadSize);wr32(q.data()+kSramConfigOriginalEntry,orig);wr32(q.data()+kSramConfigSaveBlock,l.storage[0]);wr32(q.data()+kSramConfigSaveSize,0x8000);wr32(q.data()+kSramConfigRamBackup,0xFFFFFFFF);wr32(q.data()+kSramConfigHotkeyRaw,0x1234);put(exp,l.payload_offset,q.data(),q.size());put32(exp,0,branch(0x08000000,0x08000000+l.payload_offset+kSramEntryOffset));for(unsigned i=0;i<p.save_plan.irq_count;i++)put32(exp,p.save_plan.irq_offset[i],0x03007FF4);}
 else if(layoutRoute==GBASAVE_NOR_SAVE_LAYOUT_RWW_EEPROM){std::vector<uint8_t> q(kEepromDualRwwPayload,kEepromDualRwwPayload+kEepromDualRwwPayloadSize);wr32(q.data()+kEepromConfigOriginalEntry,orig);wr32(q.data()+kEepromConfigSaveBlock0,l.storage[0]);wr32(q.data()+kEepromConfigSaveBlock1,l.storage[1]);wr32(q.data()+kEepromConfigSaveSize,0x2000);wr32(q.data()+kEepromConfigRamBackup,0x6000);put(exp,l.payload_offset,q.data(),q.size());put32(exp,0,branch(0x08000000,0x08000000+l.payload_offset+kEepromEntryOffset));for(unsigned i=0;i<p.save_plan.irq_count;i++)put32(exp,p.save_plan.irq_offset[i],0x03007FF4);stub(exp,0x300,kStubEepromReadOffset,kStubEepromReadLength,0,0,false);uint8_t t[8]={0,0x4B,0x18,0x47,0,0,0,0};wr32(t+4,0x08000000+l.payload_offset+kEepromEepromWriteAuto+1);put(exp,0x3F0,t,8);put(exp,0x510,p.save_plan.op[2].raw,5);}
 else {std::vector<uint8_t> q(kFlashCompactRwwPayload,kFlashCompactRwwPayload+kFlashCompactRwwPayloadSize);for(int i=0;i<6;i++)wr32(q.data()+kFlashCompactConfigBlock0+i*4,l.storage[i]);wr32(q.data()+kFlashCompactConfigSectorCount,32);wr32(q.data()+kFlashCompactConfigLayoutMagic,kFlashCompactLayoutMagicValue);put(exp,l.payload_offset,q.data(),q.size());uint32_t d=0x08000000+l.payload_offset+kFlashCompactDispatchOffset;stub(exp,0x300,kStubFlashReadOffset,kStubFlashReadLength,kStubFlashReadDispatchOffset,d,true);stub(exp,0x380,kStubFlashEraseChipOffset,kStubFlashEraseChipLength,kStubFlashEraseChipDispatchOffset,d,true);stub(exp,0x3C0,kStubFlashEraseSectorOffset,kStubFlashEraseSectorLength,kStubFlashEraseSectorDispatchOffset,d,true);stub(exp,0x440,kStubFlashWriteSectorOffset,kStubFlashWriteSectorLength,kStubFlashWriteSectorDispatchOffset,d,true);stub(exp,0x4C0,kStubFlashWriteByteOffset,kStubFlashWriteByteLength,kStubFlashWriteByteDispatchOffset,d,true);stub(exp,0x520,saveType==SFW_SAVE_FLASH1024K?kStubFlashIdent1mOffset:kStubFlashIdent512Offset,8,0,0,false);stub(exp,0x560,kStubThumbRet0Offset,kStubThumbRet0Length,0,0,false);stub(exp,0x590,kStubThumbRet0Offset,kStubThumbRet0Length,0,0,false);put(exp,0x5C0,p.save_plan.op[8].raw,5);}
 // Exercise awkward chunk boundaries, including one-byte chunks near patches.
 const uint32_t chunks[]={1,3,7,31,257,4093};uint32_t off=0,ci=0;while(off<got.size()){uint32_t n=std::min<uint32_t>(chunks[ci++%6],got.size()-off);if(!gbasave_nor_patch_apply_overlay(&p,&np,payload,off,got.data()+off,n)){std::fprintf(stderr,"%s overlay fail @%x\n",name,off);return false;}off+=n;}
 return same(name,got,exp);
}
int main(){bool ok=true;ok&=run(SFW_SAVE_SRAM,GBASAVE_NOR_SAVE_LAYOUT_RWW_SRAM,"SRAM");ok&=run(SFW_SAVE_EEPROM64K,GBASAVE_NOR_SAVE_LAYOUT_RWW_EEPROM,"EEPROM");ok&=run(SFW_SAVE_FLASH1024K,GBASAVE_NOR_SAVE_LAYOUT_RWW_FLASH_COMPACT,"FLASH1M");if(ok)std::puts("nor_patch_host: PASS");return ok?0:1;}
