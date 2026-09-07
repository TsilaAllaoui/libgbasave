/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gbasave/embedded/superfw_savepatch_port.h"

/* Derived from SuperFW 0.20 patchengine.c / patcher.c / save_signatures.h.
* SuperFW is GPL-3.0-or-later. This file intentionally keeps only save-type
* discovery and save-handler routing information. UI, SuperCard mapping,
* WAITCNT, RTC and in-game menu patching are not ported here. */

#include "gbasave/embedded/superfw_save_signatures.h"

#define GUESS_SRAM      (1u << 0)
#define GUESS_EEPROM    (1u << 1)
#define GUESS_FLASH     (1u << 2)
#define GUESS_FLASH64   (1u << 3)
#define GUESS_FLASH128  (1u << 4)

#define SRAM_V_WORD0     0x4D415253u
#define SRAM_V_WORD1     0x3131565Fu
#define SRAM_F_WORD1     0x565F465Fu
#define EEPROM_V_WORD0   0x52504545u
#define EEPROM_V_WORD1   0x565F4D4Fu
#define FLASH_V_WORD0    0x53414C46u
#define FLASH_V_WORD1    0x31565F48u
#define FLASH512_WORD1   0x32313548u
#define FLASH1M_WORD1    0x5F4D3148u

static uint16_t rd16p(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32p(const uint8_t *p) {
    return (uint32_t)rd16p(p) | ((uint32_t)rd16p(p+2) << 16);
}

static void zero_mem(void *vp, uint32_t n) {

    uint8_t *p=(uint8_t*)vp;
    while(n--) *p++=0;

}

static void copy_mem(void *vd, const void *vs, uint32_t n) {

    uint8_t *d=(uint8_t*)vd;
    const uint8_t*s=(const uint8_t*)vs;
    while(n--) *d++=*s++;

}

static int eq5(const uint8_t*a,const uint8_t*b){
    unsigned i;
    for(i=0;i<5;i++){
        if(a[i]!=b[i])return 0;
    }
    return 1;
}

static int cmp5(const uint8_t*a,const uint8_t*b){
    unsigned i;
    for(i=0;i<5;i++){
        if(a[i]<b[i])return -1;
        if(a[i]>b[i])return 1;
    }
    return 0;
}

static int match_sig(const uint8_t *p, uint32_t avail, const uint16_t *sig, uint32_t bytes) {

    uint32_t i;
    if(avail<bytes)return 0;

    for(i=0;i<bytes/2u;i++) {
        uint16_t s=sig[i];
        if(s && rd16p(p+i*2u)!=s)return 0;
    }

    return 1;

}

static int has_op_at(const SfwSavePlan *p, uint8_t kind, uint32_t off) {

    uint32_t i;
    for(i=0;i<p->op_count;i++)if(p->op[i].kind==kind&&p->op[i].offset==off)return 1;
    return 0;

}

static void push_op(SfwSavePlan *p, uint8_t kind, uint32_t off) {

    SfwSaveOp *o;
    if(has_op_at(p,kind,off))return;

    if(p->op_count>=SFW_MAX_SAVE_OPS){
        p->overflow=1;
        return;
    }

    o=&p->op[p->op_count++];
    zero_mem(o,sizeof(*o));
    o->kind=kind;
    o->offset=off;

}

static void push_raw(SfwSavePlan*p,uint32_t off,const uint8_t*data,uint32_t n){

    SfwSaveOp*o;
    if(n>SFW_MAX_RAW_BYTES){
        p->overflow=1;
        return;
    }
    if(p->op_count>=SFW_MAX_SAVE_OPS){
        p->overflow=1;
        return;
    }

    o=&p->op[p->op_count++];
    zero_mem(o,sizeof(*o));
    o->kind=SFW_OP_RAW_BYTES;
    o->offset=off;
    o->raw_len=(uint8_t)n;
    copy_mem(o->raw,data,n);

}

static int isromaddr(uint32_t a){
    uint32_t h=a>>24;
    return h==8u||h==9u;
}

static int isthumbromaddr(uint32_t a){
    return isromaddr(a)&&(a&1u)!=0u;
}

/* Nintendo GBA Flash SDK setup tables hold Thumb function pointers. Reject
* even handler pointers instead of rounding a malformed/middle-of-function
* address into a patch target. Known retail DB entries are unaffected. */
static int isromramaddr(uint32_t a){
    uint32_t h=a>>24;
    return h==8u||h==9u||h==2u||h==3u;
}

static int valid_flashid(uint16_t did){
    static const uint16_t t[]={
        0x0000,0x3D1F,0xD4BF,0x1B32,0x1CC2,0x09C2,0x1362
    };
    unsigned i;
    for(i=0;i<sizeof(t)/sizeof(t[0]);i++)if(t[i]==did)return 1;
    return 0;
}

static int isflash128(uint16_t did){
    return did==0x09C2u||did==0x1362u;
}

static int flash_info_v1_ok(const t_flash_setup_info_v1 *s){

    if(s->zero_pad1||s->zero_pad2)return 0;

    if(s->flash_size!=65536u&&s->flash_size!=131072u)return 0;

    if(s->sector_size!=128u&&s->sector_size!=4096u)return 0;

    if(s->shift_amount!=7u&&s->shift_amount!=12u)return 0;

    if(s->ws[0]>=4u||s->ws[1]>=4u)return 0;

    if(!isthumbromaddr(s->program_sector_fnptr)||!isthumbromaddr(s->erase_chip_fnptr)||!isthumbromaddr(s->erase_sector_fnptr)||!isthumbromaddr(s->wait_flash_write_fnptr)||!isromramaddr(s->timeout_lut_ptr))return 0;

    if(s->flash_size!=(uint32_t)s->sector_count*s->sector_size)return 0;

    return valid_flashid(s->device_id);

}

static int flash_info_v2_ok(const t_flash_setup_info_v2 *s){

    if(s->zero_pad1||s->zero_pad2)return 0;

    if(s->flash_size!=65536u&&s->flash_size!=131072u)return 0;

    if(s->sector_size!=128u&&s->sector_size!=4096u)return 0;

    if(s->shift_amount!=7u&&s->shift_amount!=12u)return 0;

    if(s->ws[0]>=4u||s->ws[1]>=4u)return 0;

    if(!isthumbromaddr(s->program_byte_fnptr)||!isthumbromaddr(s->program_sector_fnptr)||!isthumbromaddr(s->erase_chip_fnptr)||!isthumbromaddr(s->erase_sector_fnptr)||!isthumbromaddr(s->wait_flash_write_fnptr)||!isromramaddr(s->timeout_lut_ptr))return 0;

    if(s->flash_size!=(uint32_t)s->sector_count*s->sector_size)return 0;

    return valid_flashid(s->device_id);

}

void sfw_saveplan_init(SfwSavePlan *p, uint32_t filesize){
    zero_mem(p,sizeof(*p));
    p->filesize=filesize;
}

void sfw_saveplan_scan_filtered(SfwSavePlan *p,const uint8_t*data,uint32_t len,uint32_t base,uint32_t accept_start,uint8_t family){

    uint32_t i,abs,w0,w1,avail;

    if(!p||!data||len<4u)return;

    if(!(family&SFW_SCAN_ALL))family=SFW_SCAN_ALL;

    for(i=0;i+3u<len;i+=4u){

        abs=base+i;
        if(abs<accept_start)continue;
        avail=len-i;
        w0=rd32p(data+i);
        w1=avail>=8u?rd32p(data+i+4u):0;

        if(w0==SRAM_V_WORD0&&(w1==SRAM_V_WORD1||w1==SRAM_F_WORD1))p->save_guess|=GUESS_SRAM;

        else if(w0==EEPROM_V_WORD0&&w1==EEPROM_V_WORD1)p->save_guess|=GUESS_EEPROM;

        else if(w0==FLASH_V_WORD0){
            if(w1==FLASH_V_WORD1)p->save_guess|=GUESS_FLASH;
            else if(w1==FLASH512_WORD1)p->save_guess|=GUESS_FLASH64;
            else if(w1==FLASH1M_WORD1)p->save_guess|=GUESS_FLASH128;
        }

        if(family&SFW_SCAN_EEPROM){

            if(w0==eeprom_v1_read_word0&&match_sig(data+i,avail,eeprom_v1_read_sig,sizeof(eeprom_v1_read_sig)))push_op(p,SFW_OP_EEPROM_READ,abs);

            else if(w0==eeprom_v2_read_word0&&match_sig(data+i,avail,eeprom_v2_read_sig,sizeof(eeprom_v2_read_sig)))push_op(p,SFW_OP_EEPROM_READ,abs);

            else if(w0==eeprom_v1_write_word0&&match_sig(data+i,avail,eeprom_v1_write_sig,sizeof(eeprom_v1_write_sig)))push_op(p,SFW_OP_EEPROM_WRITE,abs);

            else if(w0==eeprom_v2_write_word0&&match_sig(data+i,avail,eeprom_v2_write_sig,sizeof(eeprom_v2_write_sig)))push_op(p,SFW_OP_EEPROM_WRITE,abs);

            else if(w0==eeprom_v3_write_word0&&match_sig(data+i,avail,eeprom_v3_write_sig,sizeof(eeprom_v3_write_sig)))push_op(p,SFW_OP_EEPROM_WRITE,abs);

            else if(w0==eeprom_v4_write_word0&&match_sig(data+i,avail,eeprom_v4_write_sig,sizeof(eeprom_v4_write_sig)))push_op(p,SFW_OP_EEPROM_WRITE,abs);

        }

        if(family&SFW_SCAN_FLASH){

            if(w0==flash_v1_read_word0&&match_sig(data+i,avail,flash_v1_read_sig,sizeof(flash_v1_read_sig)))push_op(p,SFW_OP_FLASH_READ,abs);

            else if(w0==flash_v23_read_word0){
                if(match_sig(data+i,avail,flash_v2_read_sig,sizeof(flash_v2_read_sig)))push_op(p,SFW_OP_FLASH_READ,abs);
                if(match_sig(data+i,avail,flash_v3_read_sig,sizeof(flash_v3_read_sig)))push_op(p,SFW_OP_FLASH_READ,abs);
            }

            else if(w0==flash_v1_ident_word0&&match_sig(data+i,avail,flash_v1_ident_sig,sizeof(flash_v1_ident_sig)))push_op(p,SFW_OP_FLASH_IDENT,abs);

            else if(w0==flash_v2_ident_word0&&match_sig(data+i,avail,flash_v2_ident_sig,sizeof(flash_v2_ident_sig)))push_op(p,SFW_OP_FLASH_IDENT,abs);

            else if(w0==flash_v1_verify_word0&&match_sig(data+i,avail,flash_v1_verify_sig,sizeof(flash_v1_verify_sig)))push_op(p,SFW_OP_FLASH_VERIFY,abs);

            else if(w0==flash_v23_verify_word0){
                if(match_sig(data+i,avail,flash_v2_verify_sig,sizeof(flash_v2_verify_sig)))push_op(p,SFW_OP_FLASH_VERIFY,abs);
                if(match_sig(data+i,avail,flash_v3_verify_sig,sizeof(flash_v3_verify_sig)))push_op(p,SFW_OP_FLASH_VERIFY,abs);
            }

            else if(w0==flash_v23_verify_n_word0&&
                    match_sig(data+i,avail,flash_v23_verify_n_sig,sizeof(flash_v23_verify_n_sig)))
                push_op(p,SFW_OP_FLASH_VERIFY,abs);

            else if(avail>=sizeof(t_flash_setup_info_v1)){

                uint32_t v1_size=(avail>=28u)?rd32p(data+i+20u):0u;

                uint32_t v1_sector=(avail>=32u)?rd32p(data+i+24u):0u;

                uint32_t v2_size=(avail>=32u)?rd32p(data+i+24u):0u;

                uint32_t v2_sector=(avail>=36u)?rd32p(data+i+28u):0u;

                int maybe_v1=(v1_size==65536u||v1_size==131072u)&&(v1_sector==128u||v1_sector==4096u);

                int maybe_v2=(avail>=sizeof(t_flash_setup_info_v2))&&(v2_size==65536u||v2_size==131072u)&&(v2_sector==128u||v2_sector==4096u);

                if(maybe_v2){

                    const t_flash_setup_info_v2 *v2=(const t_flash_setup_info_v2*)(const void*)(data+i);

                    if(flash_info_v2_ok(v2)){

                        push_op(p,SFW_OP_FLASH_ERASE_CHIP,v2->erase_chip_fnptr&0x01FFFFFEu);
                        push_op(p,SFW_OP_FLASH_ERASE_SECTOR,v2->erase_sector_fnptr&0x01FFFFFEu);
                        push_op(p,SFW_OP_FLASH_WRITE_SECTOR,v2->program_sector_fnptr&0x01FFFFFEu);
                        push_op(p,SFW_OP_FLASH_WRITE_BYTE,v2->program_byte_fnptr&0x01FFFFFEu);

                        if(v2->device_id){
                            if(isflash128(v2->device_id))p->flash128cnt++;
                            else p->flash64cnt++;
                        }

                    }

                }

                if(maybe_v1){

                    const t_flash_setup_info_v1 *v1=(const t_flash_setup_info_v1*)(const void*)(data+i);

                    if(flash_info_v1_ok(v1)){

                        push_op(p,SFW_OP_FLASH_ERASE_CHIP,v1->erase_chip_fnptr&0x01FFFFFEu);
                        push_op(p,SFW_OP_FLASH_ERASE_SECTOR,v1->erase_sector_fnptr&0x01FFFFFEu);
                        push_op(p,SFW_OP_FLASH_WRITE_SECTOR,v1->program_sector_fnptr&0x01FFFFFEu);

                        if(v1->device_id){
                            if(isflash128(v1->device_id))p->flash128cnt++;
                            else p->flash64cnt++;
                        }

                    }

                }

            }

        }

    }

}

void sfw_saveplan_scan(SfwSavePlan *p,const uint8_t*data,uint32_t len,uint32_t base,uint32_t accept_start){
    sfw_saveplan_scan_filtered(p,data,len,base,accept_start,SFW_SCAN_ALL);
}

static void filter_kind_group(SfwSavePlan*p,int eeprom){
    uint32_t i=0;
    while(i<p->op_count){
        uint8_t k=p->op[i].kind;
        int keep=eeprom?(k==SFW_OP_EEPROM_READ||k==SFW_OP_EEPROM_WRITE):
            ((k>=SFW_OP_FLASH_READ&&k<=SFW_OP_FLASH_VERIFY)||k==SFW_OP_FLASH_SWITCH_BANK);
        if(!keep){
            uint32_t j;
            for(j=i+1;j<p->op_count;j++)copy_mem(&p->op[j-1],&p->op[j],sizeof(p->op[0]));
            p->op_count--;
        } else  i++;
    }
}

void sfw_saveplan_finalize(SfwSavePlan*p){

    int ecomplete,fcomplete;
    uint32_t eguess,fguess,sguess;

    if(!p)return;

    ecomplete=sfw_saveplan_has(p,SFW_OP_EEPROM_READ)&&sfw_saveplan_has(p,SFW_OP_EEPROM_WRITE);

    fcomplete=sfw_saveplan_has(p,SFW_OP_FLASH_READ)&&sfw_saveplan_has(p,SFW_OP_FLASH_ERASE_CHIP)&&sfw_saveplan_has(p,SFW_OP_FLASH_ERASE_SECTOR)&&sfw_saveplan_has(p,SFW_OP_FLASH_WRITE_SECTOR);

    eguess=p->save_guess&GUESS_EEPROM;

    fguess=p->save_guess&(GUESS_FLASH|GUESS_FLASH64|GUESS_FLASH128);

    sguess=p->save_guess&GUESS_SRAM;

    /* Prefer a complete SDK handler family over unrelated strings embedded in
    data/text.  This avoids the old ambiguous-guess => SRAM behavior. */
    if(ecomplete&&!fcomplete){
        p->save_type=SFW_SAVE_EEPROM64K;
        filter_kind_group(p,1);
        return;
    }

    if(fcomplete&&!ecomplete){

        p->save_type=((p->save_guess&GUESS_FLASH128)||(p->flash128cnt>p->flash64cnt&&p->flash128cnt))?SFW_SAVE_FLASH1024K:SFW_SAVE_FLASH512K;

        filter_kind_group(p,0);
        return;

    }

    if(ecomplete&&fcomplete){

        if(eguess&&!fguess){
            p->save_type=SFW_SAVE_EEPROM64K;
            filter_kind_group(p,1);
            return;
        }

        if(fguess&&!eguess){
            p->save_type=((p->save_guess&GUESS_FLASH128)||(p->flash128cnt>p->flash64cnt&&p->flash128cnt))?SFW_SAVE_FLASH1024K:SFW_SAVE_FLASH512K;
            filter_kind_group(p,0);
            return;
        }

        /* Two genuinely complete save APIs with no decisive marker is unsafe
        to auto-patch.  Leave it unsupported rather than guessing SRAM. */
        p->save_type=SFW_SAVE_NONE;
        p->op_count=0;
        return;

    }

    /* String-only fallbacks are useful for SRAM and for reporting incomplete
    EEPROM/Flash scans, but sfwPlanUsable() will still reject missing APIs. */
    if(eguess&&!fguess&&!sguess){
        p->save_type=SFW_SAVE_EEPROM64K;
        filter_kind_group(p,1);
        return;
    }

    if(fguess&&!eguess&&!sguess){
        p->save_type=(p->save_guess&GUESS_FLASH128)?SFW_SAVE_FLASH1024K:SFW_SAVE_FLASH512K;
        filter_kind_group(p,0);
        return;
    }

    if(sguess&&!eguess&&!fguess){
        p->save_type=SFW_SAVE_SRAM;
        p->op_count=0;
        return;
    }

    p->save_type=SFW_SAVE_NONE;
    p->op_count=0;

}

uint32_t sfw_save_type_size(uint8_t t){
    switch(t){
        case SFW_SAVE_SRAM:return 32768u;
        case SFW_SAVE_EEPROM4K:return 512u;
        case SFW_SAVE_EEPROM64K:return 8192u;
        case SFW_SAVE_FLASH512K:return 65536u;
        case SFW_SAVE_FLASH1024K:return 131072u;
        default:return 0u;
    }
}

int sfw_saveplan_has(const SfwSavePlan*p,uint8_t k){
    uint32_t i;
    if(!p)return 0;
    for(i=0;i<p->op_count;i++)if(p->op[i].kind==k)return 1;
    return 0;
}

int sfw_saveplan_usable(const SfwSavePlan*p){
    if(!p||p->overflow)return 0;
    if(p->save_type==SFW_SAVE_SRAM)return 1;
    if(p->save_type==SFW_SAVE_EEPROM4K||p->save_type==SFW_SAVE_EEPROM64K)
        return sfw_saveplan_has(p,SFW_OP_EEPROM_READ)&&sfw_saveplan_has(p,SFW_OP_EEPROM_WRITE);
    if(p->save_type==SFW_SAVE_FLASH512K||p->save_type==SFW_SAVE_FLASH1024K)
        return sfw_saveplan_has(p,SFW_OP_FLASH_READ)&&sfw_saveplan_has(p,SFW_OP_FLASH_ERASE_CHIP)&&
               sfw_saveplan_has(p,SFW_OP_FLASH_ERASE_SECTOR)&&sfw_saveplan_has(p,SFW_OP_FLASH_WRITE_SECTOR);
    return 0;
}

const char *sfw_save_type_name(uint8_t t){
    switch(t){
        case SFW_SAVE_SRAM:return "SRAM";
        case SFW_SAVE_EEPROM4K:return "EEPROM4K";
        case SFW_SAVE_EEPROM64K:return "EEPROM64K";
        case SFW_SAVE_FLASH512K:return "FLASH512";
        case SFW_SAVE_FLASH1024K:return "FLASH1M";
        default:return "NONE";
    }
}

/* SuperFW patch DB V1 reader. We import only save operations. */
typedef struct {
    uint32_t signature,dbversion,patchcnt,idxcnt;
    char date[8],version[8],creator[32];
}
DbHead;

typedef struct {
    uint8_t gcode[4];
    uint32_t offset;
}
DbIdx;

int sfw_saveplan_db_lookup(SfwSavePlan*p,const uint8_t*db,uint32_t dbsz,const uint8_t gc5[5]){

    const DbHead*h;
    const DbIdx*idx;
    const uint32_t*entries;
    const uint8_t*progpage;
    uint32_t programs_len[4]={
        0,0,0,0
    };
    uint8_t programs[4][60];
    uint32_t pi=0,pos,j;
    uint32_t lo,hi,match=0xFFFFFFFFu;

    if(!p||!db||dbsz<1536u)return 0;

    h=(const DbHead*)(const void*)db;

    if(h->signature!=0x31424450u||h->dbversion!=0x00010000u)return 0;

    if(1024u+512u*h->idxcnt>dbsz)return 0;

    idx=(const DbIdx*)(const void*)(db+1024u);
    entries=(const uint32_t*)(const void*)(db+1024u+512u*h->idxcnt);

    /* FIX79: SuperFW's DB index is sorted by 4-byte GameCode + revision.  The
    * old port linearly scanned all ~2900 entries on every selection.  Use a
    * bounded binary search so an embedded-DB hit is effectively constant time. */
    lo=0u;
    hi=h->patchcnt;

    while(lo<hi){

        uint32_t mid=lo+((hi-lo)>>1);
        uint8_t key[5];
        int c;

        key[0]=idx[mid].gcode[0];
        key[1]=idx[mid].gcode[1];
        key[2]=idx[mid].gcode[2];
        key[3]=idx[mid].gcode[3];
        key[4]=(uint8_t)(idx[mid].offset&0xFFu);

        c=cmp5(key,gc5);

        if(c<0)lo=mid+1u;
        else hi=mid;

    }

    if(lo>=h->patchcnt)return 0;

    {
        uint8_t key[5];
        key[0]=idx[lo].gcode[0];
        key[1]=idx[lo].gcode[1];
        key[2]=idx[lo].gcode[2];
        key[3]=idx[lo].gcode[3];
        key[4]=(uint8_t)(idx[lo].offset&0xFFu);
        if(!eq5(key,gc5))return 0;
        match=lo;
    }

    /* Parse tiny patch programs only after a key hit. */
    progpage=db+512u;
    pos=0;

    while(pos<512u&&pi<4u){

        uint32_t n=progpage[pos++];

        if(!n)break;

        if(n>60u||pos+n>512u)return 0;

        programs_len[pi]=n;
        copy_mem(programs[pi],progpage+pos,n);
        pos+=n;
        pi++;

    }

    {

        uint32_t off,phead,wcnt,scnt;
        const uint32_t*q;

        off=idx[match].offset>>8;
        q=entries+off;
        phead=*q++;
        wcnt=phead&0xFFu;
        scnt=(phead>>8)&0x1Fu;
        p->save_type=(uint8_t)((phead>>13)&7u);
        p->source_db=1u;

        q+=wcnt;

        for(j=0;j<scnt;j++){

            uint32_t v=q[j],opc=v>>28,arg=(v>>25)&7u,moff=v&0x01FFFFFFu;

            if(opc==8u){
                push_op(p,arg==0u?SFW_OP_EEPROM_READ:SFW_OP_EEPROM_WRITE,moff);
            }

            else if(opc==9u){
                static const uint8_t map[8]={
                    SFW_OP_FLASH_READ,SFW_OP_FLASH_ERASE_CHIP,SFW_OP_FLASH_ERASE_SECTOR,SFW_OP_FLASH_WRITE_SECTOR,SFW_OP_FLASH_WRITE_BYTE,0,SFW_OP_FLASH_IDENT,SFW_OP_FLASH_VERIFY
                };
                if(map[arg])push_op(p,map[arg],moff);
            }

            else if(opc==0u){
                if(arg<4u&&programs_len[arg])push_raw(p,moff,programs[arg],programs_len[arg]);
            }

            else if(opc==5u&&arg==0u){
                SfwSaveOp*o;
                if(p->op_count>=SFW_MAX_SAVE_OPS){
                    p->overflow=1;
                    continue;
                }
                o=&p->op[p->op_count++];
                zero_mem(o,sizeof(*o));
                o->offset=moff;
                o->kind=SFW_OP_RAW_THUMB_RET0;
            }

        }

        /* SuperFW IRQ DB ops are a separate list following save ops. */
        {

            uint32_t irqcnt=(phead>>16)&0xFFu;
            const uint32_t*iq=q+scnt;

            for(j=0;j<irqcnt;j++){

                uint32_t v=iq[j],opc=v>>28,arg=(v>>25)&7u,moff=v&0x01FFFFFFu;

                if(opc==0u&&arg==3u&&programs_len[3]==4u&&rd32p(programs[3])==0x03007FF4u){

                    uint32_t k;
                    int dup=0;
                    for(k=0;k<p->irq_count;k++)if(p->irq_offset[k]==moff){
                        dup=1;
                        break;
                    }

                    if(!dup&&p->irq_count<SFW_MAX_IRQ_OPS)p->irq_offset[p->irq_count++]=moff;

                }

                if(opc==3u)j+=(arg+1u+3u)/4u;

                else if(opc==4u)j+=arg+1u;

            }

        }

        return 1;

    }

}
static void compat_zero(SfwCompatPlan*p,uint32_t filesize){
    uint8_t*z=(uint8_t*)(void*)p;uint32_t i;
    for(i=0;i<sizeof(*p);i++)z[i]=0;
    p->filesize=filesize;p->virtual_end=filesize;
}
static int compat_push(SfwCompatPlan*p,uint32_t off,const uint8_t*data,uint32_t n){
    SfwCompatOp*o;uint32_t i,end;
    if(!n||n>SFW_MAX_COMPAT_BYTES||p->op_count>=SFW_MAX_COMPAT_PATCHES){p->overflow=1;return 0;}
    if(off>0xFFFFFFFFu-n){p->overflow=1;return 0;}
    o=&p->op[p->op_count++];o->offset=off;o->len=(uint8_t)n;
    for(i=0;i<n;i++)o->bytes[i]=data[i];
    end=off+n;if(end>p->virtual_end)p->virtual_end=end;
    return 1;
}

/* Expand SuperFW WAITCNT DB operations into concrete byte overlays. This keeps
 * the runtime overlay engine independent from SuperFW's compact opcode format.
 * Opcodes used by the selected ChisFlash rules are: raw program copy, Thumb
 * NOP, and literal word copy. Byte/ARM-NOP forms are supported as well. */
int sfw_waitcnt_db_lookup(SfwCompatPlan*p,const uint8_t*db,uint32_t dbsz,const uint8_t gc5[5]){
    const DbHead*h;const DbIdx*idx;const uint32_t*entries;const uint8_t*progpage;
    uint32_t programs_len[4]={0,0,0,0};uint8_t programs[4][60];
    uint32_t pi=0,pos,lo,hi,match=0xFFFFFFFFu,j,off,phead,wcnt;const uint32_t*q;
    static const uint8_t tnop[2]={0xC0u,0x46u};
    static const uint8_t anop[4]={0x00u,0x00u,0xA0u,0xE1u};
    if(!p)return 0;
    compat_zero(p,0u);
    if(!db||dbsz<1536u)return 0;
    h=(const DbHead*)(const void*)db;
    if(h->signature!=0x31424450u||h->dbversion!=0x00010000u)return 0;
    if(1024u+512u*h->idxcnt>dbsz)return 0;
    idx=(const DbIdx*)(const void*)(db+1024u);entries=(const uint32_t*)(const void*)(db+1024u+512u*h->idxcnt);
    lo=0u;hi=h->patchcnt;
    while(lo<hi){uint32_t mid=lo+((hi-lo)>>1);uint8_t key[5];int c;
        key[0]=idx[mid].gcode[0];key[1]=idx[mid].gcode[1];key[2]=idx[mid].gcode[2];key[3]=idx[mid].gcode[3];key[4]=(uint8_t)(idx[mid].offset&0xFFu);
        c=cmp5(key,gc5);if(c<0)lo=mid+1u;else hi=mid;}
    if(lo>=h->patchcnt)return 0;
    {uint8_t key[5];key[0]=idx[lo].gcode[0];key[1]=idx[lo].gcode[1];key[2]=idx[lo].gcode[2];key[3]=idx[lo].gcode[3];key[4]=(uint8_t)(idx[lo].offset&0xFFu);if(!eq5(key,gc5))return 0;match=lo;}
    progpage=db+512u;pos=0u;
    while(pos<512u&&pi<4u){uint32_t n=progpage[pos++];if(!n)break;if(n>60u||pos+n>512u)return 0;programs_len[pi]=n;copy_mem(programs[pi],progpage+pos,n);pos+=n;pi++;}
    off=idx[match].offset>>8;q=entries+off;phead=*q++;wcnt=phead&0xFFu;
    p->source_db=1u;
    j=0u;
    while(j<wcnt){
        uint32_t v=q[j++],opc=v>>28,arg=(v>>25)&7u,moff=v&0x01FFFFFFu,n,k;
        uint8_t tmp[SFW_MAX_COMPAT_BYTES];
        if(opc==0u){if(arg>=4u||!programs_len[arg]||!compat_push(p,moff,programs[arg],programs_len[arg]))return 0;}
        else if(opc==1u){n=2u*(arg+1u);for(k=0;k<n;k+=2u){tmp[k]=tnop[0];tmp[k+1u]=tnop[1];}if(!compat_push(p,moff,tmp,n))return 0;}
        else if(opc==2u){n=4u*(arg+1u);for(k=0;k<n;k+=4u){tmp[k]=anop[0];tmp[k+1u]=anop[1];tmp[k+2u]=anop[2];tmp[k+3u]=anop[3];}if(!compat_push(p,moff,tmp,n))return 0;}
        else if(opc==3u){uint32_t words=(arg+1u+3u)/4u;if(j+words>wcnt)return 0;n=arg+1u;for(k=0;k<n;k++)tmp[k]=(uint8_t)(q[j+(k>>2)]>>(8u*(k&3u)));j+=words;if(!compat_push(p,moff,tmp,n))return 0;}
        else if(opc==4u){uint32_t words=arg+1u;if(j+words>wcnt||4u*words>SFW_MAX_COMPAT_BYTES)return 0;n=4u*words;for(k=0;k<n;k++)tmp[k]=(uint8_t)(q[j+(k>>2)]>>(8u*(k&3u)));j+=words;if(!compat_push(p,moff,tmp,n))return 0;}
        else return 0;
    }
    return !p->overflow;
}

