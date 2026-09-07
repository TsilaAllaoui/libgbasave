extern "C" {

    typedef unsigned char u8;

    typedef unsigned int u32;

    typedef volatile unsigned char vu8;

    typedef struct {

        u32 block[6];

        u32 sector_count;

        u32 layout_magic;

    }
    CompactCfg;

    extern const CompactCfg *m36c_get_config(void);

    extern u32 m36c_run_worker(u32 op,u32 rom_off);

#ifdef M36C_HOST_TEST
    extern u8 m36c_host_rom[];

    extern u8 m36c_host_sram[];

#define ROM8(off) (m36c_host_rom[(off)])
#define SRAM8(off) (m36c_host_sram[(off)])
#else
#define ROM8(off) (*(volatile const u8*)(0x08000000u+(off)))
#define SRAM8(off) (*(vu8*)(0x0E000000u+(off)))
#endif
#define DIAG_BASE 0x7FC0u
#define DIAG_MAGIC 0x4744334Du
#define FLAG_NOOP 1u
#define FLAG_PROGRAM_ONLY 2u
#define FLAG_ERASED 4u
#define FLAG_COMPACT 8u
#define HEADER_MAGIC 0x31483143u /* C1H1 */
#define HEADER_FMT   0x36303031u /* 1006 */
#define RECORD_MAGIC 0x31434552u /* REC1 */
#define RECORD_FMT   0x31474F4Cu /* LOG1 */
#define COMMIT_MAGIC 0x454E4F44u /* DONE */
#define SPENT_MAGIC  0x544E5053u /* SPNT */
#define META_REC_BASE 0x100u
#define META_REC_SIZE 32u
#define DATA_BASE 0x1000u
#define SLOT_SIZE 0x1000u
#define SLOTS_PER_BLOCK 31u
#define BLOCKS_PER_GROUP 3u
#define RECORD_DATA 0u
#define RECORD_ERASE 1u
#define WORK_PROGRAM4K 2u
#define WORK_PROGRAM32 3u
#define WORK_ERASE_BLOCK 4u

    typedef struct {
        u32 valid,gen,group;
    }
    Active;

    typedef struct {
        u32 found,flags,seq,rom_off;
    }
    Latest;

    typedef struct {
        u32 ok,block_index,slot,rom_off;
    }
    FreeSlot;

    static u32 rd32_rom(u32 off){
        return (u32)ROM8(off)|((u32)ROM8(off+1u)<<8)|((u32)ROM8(off+2u)<<16)|((u32)ROM8(off+3u)<<24);
    }

    static u32 rd32_sram(u32 off){
        return (u32)SRAM8(off)|((u32)SRAM8(off+1u)<<8)|((u32)SRAM8(off+2u)<<16)|((u32)SRAM8(off+3u)<<24);
    }

    static void wr32_sram(u32 off,u32 v){
        SRAM8(off)=(u8)v;
        SRAM8(off+1u)=(u8)(v>>8);
        SRAM8(off+2u)=(u8)(v>>16);
        SRAM8(off+3u)=(u8)(v>>24);
    }

    static void fill_sram32_ff(void){
        u32 i;
        for(i=0;i<32u;i++)SRAM8(i)=0xFFu;
    }

    static void diag32(u32 off,u32 v){
        wr32_sram(DIAG_BASE+off,v);
    }

    static void diag_begin(u32 op){
        diag32(0,DIAG_MAGIC);
        diag32(4,op);
        diag32(8,0);
        diag32(12,1);
        diag32(16,0);
        diag32(20,0);
        diag32(24,0);
        diag32(28,0);
        diag32(32,0);
    }

    static void diag_result(u32 rc,u32 flags){
        diag32(20,rc);
        diag32(28,flags);
    }

    static int bytes_ff_rom(u32 off,u32 n){
        while(n--){
            if(ROM8(off++)!=0xFFu)return 0;
        }
        return 1;
    }

    static int bytes_equal_sram_rom(u32 soff,u32 roff,u32 n){
        u32 i;
        for(i=0;i<n;i++)if(SRAM8(soff+i)!=ROM8(roff+i))return 0;
        return 1;
    }

    static int sram_all_ff(u32 off,u32 n){
        u32 i;
        for(i=0;i<n;i++)if(SRAM8(off+i)!=0xFFu)return 0;
        return 1;
    }

    static void copy_to_sram(const u8*src,u32 n){
        u32 i;
        for(i=0;i<n;i++)SRAM8(i)=src[i];
    }

    static void copy_rom_to_sram(u32 roff,u32 n){
        u32 i;
        for(i=0;i<n;i++)SRAM8(i)=ROM8(roff+i);
    }

    static void copy_rom_to_dst(u32 roff,u8*dst,u32 n){
        u32 i;
        for(i=0;i<n;i++)dst[i]=ROM8(roff+i);
    }

    static void fill_dst_ff(u8*dst,u32 n){
        u32 i;
        for(i=0;i<n;i++)dst[i]=0xFFu;
    }

    static int header_valid(const CompactCfg*c,u32 group,u32 lane,u32*gen){
        u32 b=c->block[group*3u+lane],g,gl;

        if(rd32_rom(b)!=HEADER_MAGIC)return 0;
        g=rd32_rom(b+4u);
        if(g==0u||g==0xFFFFFFFFu)return 0;
        if(rd32_rom(b+8u)!=group)return 0;
        if(rd32_rom(b+12u)!=lane)return 0;
        if(rd32_rom(b+16u)!=~g)return 0;
        gl=(group<<8)|lane;
        if(rd32_rom(b+20u)!=~gl)return 0;
        if(rd32_rom(b+24u)!=HEADER_FMT)return 0;
        if(rd32_rom(b+28u)!=COMMIT_MAGIC)return 0;
        *gen=g;
        return 1;
    }

    static Active find_active(const CompactCfg*c){
        Active a={
            0,0,0
        };
        u32 g,gen0,gen1,gen2;
        for(g=0;g<2u;g++){
            if(header_valid(c,g,0,&gen0)&&header_valid(c,g,1,&gen1)&&header_valid(c,g,2,&gen2)&&gen0==gen1&&gen0==gen2){
                if(!a.valid||gen0>a.gen){
                    a.valid=1;
                    a.gen=gen0;
                    a.group=g;
                }
            }
        }
        return a;
    }

    static int record_valid(u32 desc,u32 expected_slot,u32*seq,u32*sector,u32*flags){
        u32 s,f,q;
        if(rd32_rom(desc)!=RECORD_MAGIC)return 0;
        q=rd32_rom(desc+4u);
        s=rd32_rom(desc+8u);
        f=rd32_rom(desc+12u);
        if(q==0u||q==0xFFFFFFFFu||s>=32u||f>RECORD_ERASE)return 0;
        if(rd32_rom(desc+16u)!=~q)return 0;
        if(rd32_rom(desc+20u)!=~(s|(f<<8)))return 0;
        if(rd32_rom(desc+24u)!=(RECORD_FMT^expected_slot))return 0;
        if(rd32_rom(desc+28u)!=COMMIT_MAGIC)return 0;
        *seq=q;
        *sector=s;
        *flags=f;
        return 1;
    }

    static Latest find_latest_group(const CompactCfg*c,u32 group,u32 wanted){
        Latest r={
            0,0,0,0
        };
        u32 bi,slot,b,desc,seq,sec,flags;
        for(bi=0;bi<3u;bi++){
            b=c->block[group*3u+bi];
            for(slot=0;slot<SLOTS_PER_BLOCK;slot++){
                desc=b+META_REC_BASE+slot*META_REC_SIZE;
                if(record_valid(desc,slot,&seq,&sec,&flags)&&sec==wanted&&(!r.found||seq>r.seq)){
                    r.found=1;
                    r.flags=flags;
                    r.seq=seq;
                    r.rom_off=b+DATA_BASE+slot*SLOT_SIZE;
                }
            }
        }
        return r;
    }

    static u32 max_seq_group(const CompactCfg*c,u32 group){
        u32 best=0,bi,slot,b,desc,seq,sec,flags;
        for(bi=0;bi<3u;bi++){
            b=c->block[group*3u+bi];
            for(slot=0;slot<SLOTS_PER_BLOCK;slot++){
                desc=b+META_REC_BASE+slot*META_REC_SIZE;
                if(record_valid(desc,slot,&seq,&sec,&flags)&&seq>best)best=seq;
            }
        }
        return best;
    }

    static u32 used_slots_group(const CompactCfg*c,u32 group){
        u32 used=0,bi,slot,b,desc;
        for(bi=0;bi<3u;bi++){
            b=c->block[group*3u+bi];
            for(slot=0;slot<SLOTS_PER_BLOCK;slot++){
                desc=b+META_REC_BASE+slot*META_REC_SIZE;
                if(rd32_rom(desc)!=0xFFFFFFFFu)used++;
                else if(!bytes_ff_rom(b+DATA_BASE+slot*SLOT_SIZE,SLOT_SIZE))used++;
            }
        }
        return used;
    }

    static int build_header(u32 gen,u32 group,u32 lane,u32 committed){
        fill_sram32_ff();
        wr32_sram(0,HEADER_MAGIC);
        wr32_sram(4,gen);
        wr32_sram(8,group);
        wr32_sram(12,lane);
        wr32_sram(16,~gen);
        wr32_sram(20,~((group<<8)|lane));
        wr32_sram(24,HEADER_FMT);
        if(committed)wr32_sram(28,COMMIT_MAGIC);
        return 1;
    }

    static int write_header(const CompactCfg*c,u32 group,u32 lane,u32 gen,u32 committed){
        u32 b=c->block[group*3u+lane];
        build_header(gen,group,lane,committed);
        return m36c_run_worker(WORK_PROGRAM32,b)==0u;
    }

    static int erase_group(const CompactCfg*c,u32 group){
        u32 i,b;
        for(i=0;i<3u;i++){
            b=c->block[group*3u+i];
            if(bytes_ff_rom(b,0x20000u))continue;
            if(m36c_run_worker(WORK_ERASE_BLOCK,b)!=0u)return 0;
        }
        return 1;
    }

    static int start_group(const CompactCfg*c,u32 group,u32 gen){
        u32 i;
        if(!erase_group(c,group))return 0;
        for(i=0;i<3u;i++)if(!write_header(c,group,i,gen,0u))return 0;
        return 1;
    }

    static int commit_group(const CompactCfg*c,u32 group,u32 gen){
        u32 i;
        for(i=0;i<3u;i++)if(!write_header(c,group,i,gen,1u))return 0;
        return 1;
    }

    static void build_spent(void){
        fill_sram32_ff();
        wr32_sram(0,SPENT_MAGIC);
        wr32_sram(28,COMMIT_MAGIC);
    }

    static int mark_spent(u32 desc){
        build_spent();
        return m36c_run_worker(WORK_PROGRAM32,desc)==0u;
    }

    static FreeSlot find_free_group(const CompactCfg*c,u32 group){
        FreeSlot f={
            0,0,0,0
        };
        u32 bi,slot,b,desc,data;
        for(bi=0;bi<3u;bi++){
            b=c->block[group*3u+bi];
            for(slot=0;slot<SLOTS_PER_BLOCK;slot++){
                desc=b+META_REC_BASE+slot*META_REC_SIZE;
                if(rd32_rom(desc)!=0xFFFFFFFFu)continue;
                data=b+DATA_BASE+slot*SLOT_SIZE;
                if(bytes_ff_rom(data,SLOT_SIZE)){
                    f.ok=1;
                    f.block_index=bi;
                    f.slot=slot;
                    f.rom_off=data;
                    return f;
                }
                if(!mark_spent(desc))return f;
            }
        }
        return f;
    }

    static void build_record(u32 seq,u32 sector,u32 flags,u32 slot){
        fill_sram32_ff();
        wr32_sram(0,RECORD_MAGIC);
        wr32_sram(4,seq);
        wr32_sram(8,sector);
        wr32_sram(12,flags);
        wr32_sram(16,~seq);
        wr32_sram(20,~(sector|(flags<<8)));
        wr32_sram(24,RECORD_FMT^slot);
        wr32_sram(28,COMMIT_MAGIC);
    }

    static int append_record_at(const CompactCfg*c,u32 group,FreeSlot f,u32 seq,u32 sector,u32 flags,int data_in_sram){
        u32 b=c->block[group*3u+f.block_index],desc=b+META_REC_BASE+f.slot*META_REC_SIZE;
        if(flags==RECORD_DATA){
            if(!data_in_sram)return 0;
            if(m36c_run_worker(WORK_PROGRAM4K,f.rom_off)!=0u)return 0;
        }
        build_record(seq,sector,flags,f.slot);
        if(m36c_run_worker(WORK_PROGRAM32,desc)!=0u)return 0;
        return 1;
    }

    static int init_if_needed(const CompactCfg*c,Active*a){
        if(a->valid)return 1;
        if(!start_group(c,0u,1u))return 0;
        if(!commit_group(c,0u,1u))return 0;
        a->valid=1;
        a->gen=1u;
        a->group=0u;
        return 1;
    }

    static int compact_generation(const CompactCfg*c,Active*a){
        u32 target,gen,sector,seq=0;
        Latest l;
        FreeSlot f;
        if(!a->valid)return init_if_needed(c,a);
        target=a->group^1u;
        gen=a->gen+1u;
        if(gen==0u||gen==0xFFFFFFFFu)gen=1u;
        if(!start_group(c,target,gen))return 0;
        for(sector=0;sector<32u;sector++){
            l=find_latest_group(c,a->group,sector);
            if(!l.found||l.flags==RECORD_ERASE)continue;
            copy_rom_to_sram(l.rom_off,SLOT_SIZE);
            f=find_free_group(c,target);
            if(!f.ok)return 0;
            seq++;
            if(!append_record_at(c,target,f,seq,sector,RECORD_DATA,1))return 0;
        }
        if(!commit_group(c,target,gen))return 0;
        a->group=target;
        a->gen=gen;
        a->valid=1;
        diag32(28,FLAG_COMPACT);
        return 1;
    }

    static int ensure_free(const CompactCfg*c,Active*a,FreeSlot*f){
        if(!init_if_needed(c,a))return 0;
        *f=find_free_group(c,a->group);
        if(f->ok)return 1;
        if(!compact_generation(c,a))return 0;
        *f=find_free_group(c,a->group);
        return f->ok;
    }

    static int logical_is_ff(const CompactCfg*c,Active a,u32 sector){
        Latest l;
        if(!a.valid)return 1;
        l=find_latest_group(c,a.group,sector);
        return !l.found||l.flags==RECORD_ERASE;
    }

    static int logical_equals_sram(const CompactCfg*c,Active a,u32 sector){
        Latest l;
        if(!a.valid)return sram_all_ff(0,SLOT_SIZE);
        l=find_latest_group(c,a.group,sector);
        if(!l.found||l.flags==RECORD_ERASE)return sram_all_ff(0,SLOT_SIZE);
        return bytes_equal_sram_rom(0,l.rom_off,SLOT_SIZE);
    }

    static int logical_read_sector(const CompactCfg*c,Active a,u32 sector,u8*dst,u32 within,u32 n){
        Latest l;
        if(!a.valid){
            fill_dst_ff(dst,n);
            return 0;
        }
        l=find_latest_group(c,a.group,sector);
        if(!l.found||l.flags==RECORD_ERASE){
            fill_dst_ff(dst,n);
            return 0;
        }
        copy_rom_to_dst(l.rom_off+within,dst,n);
        return 0;
    }

    static int op_read(const CompactCfg*c,u8*dst,u32 off,u32 count){
        Active a=find_active(c);
        u32 total=c->sector_count<<12,sector,within,take;
        if(off>total||count>total-off)return 1;
        while(count){
            sector=off>>12;
            within=off&0xFFFu;
            take=0x1000u-within;
            if(take>count)take=count;
            if(logical_read_sector(c,a,sector,dst,within,take))return 1;
            dst+=take;
            off+=take;
            count-=take;
        }
        return 0;
    }

    static int op_write_sector(const CompactCfg*c,const u8*src,u32 sector){
        Active a=find_active(c);
        FreeSlot f;
        u32 seq;
        if(sector>=32u)return 1;
        copy_to_sram(src,SLOT_SIZE);
        if(logical_equals_sram(c,a,sector)){
            diag_result(0,FLAG_NOOP);
            return 0;
        }
        if(!ensure_free(c,&a,&f))return 2;
        copy_to_sram(src,SLOT_SIZE);
        seq=max_seq_group(c,a.group)+1u;
        if(seq==0u||seq==0xFFFFFFFFu){
            if(!compact_generation(c,&a))return 3;
            f=find_free_group(c,a.group);
            if(!f.ok)return 3;
            seq=max_seq_group(c,a.group)+1u;
            copy_to_sram(src,SLOT_SIZE);
        }
        if(!append_record_at(c,a.group,f,seq,sector,RECORD_DATA,1))return 4;
        return 0;
    }

    static int op_erase_sector(const CompactCfg*c,u32 sector){
        Active a=find_active(c);
        FreeSlot f;
        u32 seq;
        if(sector>=32u)return 1;
        if(logical_is_ff(c,a,sector)){
            diag_result(0,FLAG_NOOP);
            return 0;
        }
        if(!ensure_free(c,&a,&f))return 2;
        seq=max_seq_group(c,a.group)+1u;
        if(!append_record_at(c,a.group,f,seq,sector,RECORD_ERASE,0))return 3;
        return 0;
    }

    static int op_write_byte(const CompactCfg*c,u32 sector,u32 off,u32 value){
        Active a=find_active(c);
        Latest l;
        FreeSlot f;
        u32 seq;
        if(sector>=32u||off>=0x1000u)return 1;
        l=a.valid?find_latest_group(c,a.group,sector):(Latest){
            0,0,0,0
        };
        if(!l.found||l.flags==RECORD_ERASE){
            u32 i;
            for(i=0;i<SLOT_SIZE;i++)SRAM8(i)=0xFFu;
        } else  copy_rom_to_sram(l.rom_off,SLOT_SIZE);
        if(SRAM8(off)==(u8)value){
            diag_result(0,FLAG_NOOP);
            return 0;
        }
        if(!ensure_free(c,&a,&f))return 2;
        /* compaction may have overwritten scratch, reload current state */l=find_latest_group(c,a.group,sector);
        if(!l.found||l.flags==RECORD_ERASE){
            u32 i;
            for(i=0;i<SLOT_SIZE;i++)SRAM8(i)=0xFFu;
        } else  copy_rom_to_sram(l.rom_off,SLOT_SIZE);
        SRAM8(off)=(u8)value;
        seq=max_seq_group(c,a.group)+1u;
        if(!append_record_at(c,a.group,f,seq,sector,RECORD_DATA,1))return 3;
        return 0;
    }

    static int op_erase_chip(const CompactCfg*c){
        Active a=find_active(c);
        u32 target,gen;
        if(!a.valid){
            target=0u;
            gen=1u;
        } else {
            target=a.group^1u;
            gen=a.gen+1u;
            if(gen==0u||gen==0xFFFFFFFFu)gen=1u;
        }
        if(!start_group(c,target,gen))return 2;
        if(!commit_group(c,target,gen))return 3;
        return 0;
    }

    u32 m36c_dispatch_c(u32 op,u32 a0,u32 a1,u32 a2){
        const CompactCfg*c=m36c_get_config();
        u32 rc;
        diag_begin(op);
        if(c->sector_count!=32u||c->layout_magic!=0x314D4336u){
            diag_result(0xE0u,0);
            return 0xE0u;
        }
        if(op==2u)rc=(u32)op_read(c,(u8*)a0,a1,a2);
        else if(op==3u)rc=(u32)op_write_sector(c,(const u8*)a0,a1);
        else if(op==4u)rc=(u32)op_erase_chip(c);
        else if(op==5u)rc=(u32)op_erase_sector(c,a0);
        else if(op==6u)rc=(u32)op_write_byte(c,a0,a1,a2);
        else rc=1u;
        if(rc)diag_result(rc,rd32_sram(DIAG_BASE+28u));
        else if(rd32_sram(DIAG_BASE+28u)==0u)diag_result(0,FLAG_PROGRAM_ONLY);
        return rc;
    }

}
