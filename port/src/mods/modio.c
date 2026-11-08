#include "modio.h"
#include <string.h>
#define BASE UINT32_C(0x80200000)
#define END UINT32_C(0x80800000)
typedef struct { uint32_t table,index,lba,sectors; } Resource;
static Resource resources[13];
static uint32_t base,capacity,mask;
static int lease=-1;
static int slot(uint32_t table,int index) {
    if(table==0x800A195Cu && index>=0x186 && index<=0x188)return index-0x186;
    if(table==0x800A2B3Cu && index>=0 && index<10)return index+3;
    return -1;
}
uint32_t modio_original_size(uint32_t table,int index) {
    static const uint32_t sectors[13]={15,15,29,21,38,15,31,31,9,25,7,17,17};
    int s=slot(table,index);return s<0?0:sectors[s]*2048;
}
static uint32_t u32(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned u16(const uint8_t *p) {return p[0]|(unsigned)p[1]<<8;}
static int tim_block(const uint8_t *p,size_t remaining,uint32_t *bytes) {
    if(remaining<12)return 0;
    uint32_t size=u32(p),x=u16(p+4),y=u16(p+6),w=u16(p+8),h=u16(p+10);
    uint64_t required=12+(uint64_t)w*h*2;
    if(!w || !h || x+w>1024 || y+h>512 || size!=required || size>remaining)return 0;
    *bytes=size;return 1;
}
int modio_tim_valid(const void *data,size_t bytes) {
    const uint8_t *p=data;uint32_t block;size_t at=8;
    if(!p || bytes<20 || u32(p)!=0x10 || (u32(p+4)&~11u))return 0;
    if(u32(p+4)&8) {if(!tim_block(p+at,bytes-at,&block))return 0;at+=block;}
    return tim_block(p+at,bytes-at,&block);
}
int modio_boot(uint32_t address,uint32_t bytes) {
    if(lease>=0)return 0;
    if(address || bytes) {
        if(address<BASE || address>=END || (address&15) || !bytes || (bytes&2047) || bytes>END-address)return 0;
    }
    base=address;capacity=bytes;mask=0;memset(resources,0,sizeof resources);return 1;
}
uint32_t modio_capacity(void) {return capacity;}
int modio_active(void) {return mask!=0;}
int modio_busy(void) {return lease>=0;}
int modio_register(uint32_t table,uint32_t index,uint32_t lba,uint32_t sectors) {
    int s=slot(table,(int)index);uint64_t bytes=(uint64_t)sectors*2048;
    if(s<0 || !base || bytes<=modio_original_size(table,(int)index) || bytes>capacity ||
       !lba || (uint64_t)lba+sectors>UINT64_C(0x100000000) || lease>=0)return 0;
    Resource r={table,index,lba,sectors};
    if(mask&(1u<<s))return !memcmp(&resources[s],&r,sizeof r);
    for(unsigned i=0;i<13;i++)if((mask&(1u<<i)) && lba<(uint64_t)resources[i].lba+resources[i].sectors &&
        resources[i].lba<(uint64_t)lba+sectors)return 0;
    resources[s]=r;mask|=1u<<s;return 1;
}
int modio_acquire(uint32_t table,uint32_t index,uint32_t original,uint32_t *dest) {
    int s=slot(table,(int)index);if(!dest)return 0;
    if(s<0 || !(mask&(1u<<s))) {*dest=original;return 1;}
    if(lease>=0)return 0;
    lease=s;*dest=base;return 2;
}
int modio_release(uint32_t table,uint32_t index) {
    if(lease<0 || slot(table,(int)index)!=lease)return 0;
    lease=-1;return 1;
}
int modio_check_position(const uint8_t p[3],uint32_t sectors,uint32_t dest) {
    if(!mask)return 1;
    unsigned v[3];
    for(unsigned i=0;i<3;i++) {if((p[i]&15)>9 || (p[i]>>4)>9)return 0;v[i]=(p[i]>>4)*10+(p[i]&15);}
    if(v[1]>=60 || v[2]>=75)return 0;
    uint32_t absolute=(v[0]*60+v[1])*75+v[2];
    return absolute>=150 && modio_check_read(absolute-150,sectors,dest);
}
int modio_check_read(uint32_t lba,uint32_t sectors,uint32_t dest) {
    if(!mask)return 1;
    uint64_t end=(uint64_t)lba+sectors;
    uint32_t physical=dest&0x1FFFFFFFu,owned=base-0x80000000u;
    uint64_t stop=(uint64_t)physical+(uint64_t)sectors*2048;
    int touches=sectors && physical<(uint64_t)owned+capacity && owned<stop;
    int found=-1;
    if(end>UINT64_C(0x100000000))return 0;
    for(unsigned i=0;i<13;i++)if((mask&(1u<<i)) && lba<(uint64_t)resources[i].lba+resources[i].sectors && resources[i].lba<end) {
        if(found>=0 || lba!=resources[i].lba || sectors!=resources[i].sectors)return 0;
        found=(int)i;
    }
    if(found<0)return !touches;
    return found==lease && physical==owned;
}
void modio_state_get(ModIoState *s) {
    memset(s,0,sizeof *s);if(!mask)return;
    s->version=1;s->base=base;s->capacity=capacity;s->mask=mask;
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<13;i++)if(mask&(1u<<i)) {
        uint32_t values[4]={resources[i].table,resources[i].index,resources[i].lba,resources[i].sectors};
        for(unsigned k=0;k<4;k++)for(unsigned b=0;b<4;b++)hash=(hash^(uint8_t)(values[k]>>(b*8)))*UINT64_C(1099511628211);
    }
    s->signature=hash;
}
int modio_state_valid(const ModIoState *s,size_t bytes) {
    if(!mask)return s==NULL;
    ModIoState expected;modio_state_get(&expected);
    return lease<0 && s && bytes==END-BASE && !memcmp(s,&expected,sizeof expected);
}
int modio_static_copy(void *dest,const void *source,size_t bytes) {
    if(!mask){memcpy(dest,source,bytes);return 1;}
    size_t offset=base-BASE;
    if(offset>bytes || capacity>bytes-offset)return 0;
    memcpy(dest,source,offset);
    memcpy((uint8_t*)dest+offset+capacity,(const uint8_t*)source+offset+capacity,bytes-offset-capacity);
    return 1;
}
