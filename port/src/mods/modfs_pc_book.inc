/* PC diary selector artwork. Native callbacks still update all three
 * model tracks; only their draw submission is replaced. Frame selection
 * samples semantic PC phases over the original PS1 track durations. */
#include "modfs_flc.h"
#define PC_BOOK_W 196u
#define PC_BOOK_H 82u
#define PC_BOOK_FRAMES 18u
#define PC_BOOK_BYTES (PC_BOOK_W*PC_BOOK_H*PC_BOOK_FRAMES*4u)
static void pc_book_load(const char *mod,const char *root) {
    for(unsigned wrapper=0;wrapper<2;wrapper++) {
        char rel[512],path[1024];snprintf(rel,sizeof rel,"%sDIARY/BOOK.FLC",wrapper?"PSX_DATA/":"");
        if(!pc_bitmap_path(root,rel,path))continue;
        pc_bitmap_source_add(mod,rel);mod_hash_file(rel,path);FILE *f=fopen(path,"rb");long size=-1;
        if(f){if(!fseek(f,0,SEEK_END))size=ftell(f);fclose(f);}
        long n=0;uint8_t *raw=size>=128 && size<=8*1024*1024?load(path,&n):NULL;
        size_t old=pc_book?PC_BOOK_BYTES:0;
        uint8_t *p=raw && n==size && pc_picture_fits(old,PC_BOOK_BYTES)?
            modfs_flc_decode(raw,(size_t)n,PC_BOOK_W,PC_BOOK_H,PC_BOOK_FRAMES):NULL;
        free(raw);
        if(!p){nskipped++;note("mods: %s/%s: invalid196x82x18 indexed FLC or picture budget: native/previous book retained",mod,rel);continue;}
        free(pc_book);pc_book=p;pc_diary_bytes=pc_diary_bytes-old+PC_BOOK_BYTES;nused++;
        note("mods: %s/%s: PC book frames, original PS1 model timing retained",mod,rel);
    }
}
/* GsOT length is a log2 capacity, org is its tag array. The original
 * clear/sort consumers establish this20-byte layout. Bound every snapshot
 * and validate both tables before replacing any visible model primitives. */
static int pc_book_tables(uint32_t org[2],uint32_t words[2]) {
    unsigned buffer=*ram_ptr(0x80062D1Cu);if(buffer>1)return 0;
    static const uint32_t bases[]={0x800B3D70u,0x800B4BB8u};
    for(unsigned i=0;i<2;i++) {
        uint32_t table=bases[i]+buffer*20,length=pc_ram32(table);
        if(length>11)return 0;
        words[i]=1u<<length;org[i]=pc_ram32(table+4);
        if(org[i]<0x80000000u || (org[i]&3) || (uint64_t)org[i]+words[i]*4>0x80200000u)return 0;
    }
    return 1;
}
static int pc_book_frame(void) {
    if(!pc_book || *ram_ptr(0x800CF66Bu) || !pc_bitmap_unoccluded() || pc_ram32(0x800C3E48u)!=13 || pc_ram32(0x800CF9C0u) ||
       pc_ram16(0x800C09D4u)!=0x40 || !pc_ram32(0x800CF8BCu) || *ram_ptr(0x800CF7C8u)!=5 || pc_ram32(0x800CF878u)!=3)return -1;
    GpuDisplay d;gpu_display(&d);if(!d.enabled || d.rgb24 || d.w<320 || d.h!=240)return -1;
    uint32_t org[2],words[2];if(!pc_book_tables(org,words))return -1;
    unsigned count=pc_ram16(0x800CF98Cu),cursor=pc_ram16(0x800CF98Au);
    if(!count || count>60 || cursor>count+1)return -1;
    uint32_t models[3];
    for(unsigned i=0;i<3;i++) {
        models[i]=pc_ram32(0x800CF87Cu+i*4);
        if(models[i]<0x80000000u || models[i]>0x80800000u-28 || (models[i]&3))return -1;
        for(unsigned j=0;j<i;j++)if(models[i]==models[j])return -1;
    }
    unsigned mode=pc_ram32(0x800CF8C0u),left=pc_ram16(models[1]+0xA);
    if(mode==0 || mode==1 || mode==2 || mode==13)return pc_ram32(0x800CF9C4u)?1:0;
    if(mode==4)return 2;
    if(mode==5) { /* file_viewer:model_turn(...,31), anim_UpdateTracks --left */
        if(left>31)return -1;
        unsigned f=(31-left)*12/31;return 2+(int)(f>11?11:f);
    }
    if(mode==11)return 13;
    if(mode==12) { /* original reverse track is explicitly30 updates */
        if(left>30)return -1;
        unsigned f=(30-left)*12/30;return 13-(int)(f>11?11:f);
    }
    if(mode==6) {
        left=pc_ram16(models[2]+0xA);if(!left)return 13;if(left>20)return -1;
        unsigned f=(20-left)*4/20;if(f>3)f=3;
        return (int8_t)*ram_ptr(models[2]+0xD)<0?17-(int)f:14+(int)f;
    }
    return -1;
}
int modfs_pc_book_object(uint32_t index) { return index<5 && pc_book_frame()>=0; }
int modfs_pc_book_begin(ModPcBookDraw *draw) {
    if(!draw || pc_book_frame()<0 || !pc_book_tables(draw->org,draw->words))return 0;
    for(unsigned i=0;i<2;i++)memcpy(draw->tags[i],ram_ptr(draw->org[i]),draw->words[i]*4);
    return 1;
}
void modfs_pc_book_end(const ModPcBookDraw *draw) {
    uint32_t org[2],words[2];
    if(!draw || pc_book_frame()<0 || !pc_book_tables(org,words))return;
    for(unsigned i=0;i<2;i++)if(org[i]!=draw->org[i] || words[i]!=draw->words[i])return;
    /* GsSortObject must run: it also executes animation tracks. Detach only
     * the primitives it linked to these already-owned ordering tables. */
    for(unsigned i=0;i<2;i++)memcpy(ram_ptr(org[i]),draw->tags[i],words[i]*4);
}
static void pc_book_composite(uint32_t *out,int w,int h,int native_w) {
    int frame=pc_book_frame();if(frame<0)return;
    const uint8_t *p=pc_book+(size_t)frame*PC_BOOK_W*PC_BOOK_H*4;
    int left=(native_w-320)/2+67,top=49;
    int x0=(int)(((int64_t)left*w+native_w-1)/native_w),x1=(int)(((int64_t)(left+PC_BOOK_W)*w+native_w-1)/native_w);
    int y0=(int)(((int64_t)top*h+239)/240),y1=(int)(((int64_t)(top+PC_BOOK_H)*h+239)/240);
    for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++) {
        double sx=(double)x*native_w/w-left,sy=(double)y*240/h-top;
        out[(size_t)y*w+x]=pc_diary_sample(p,PC_BOOK_W,PC_BOOK_H,sx,sy,sx+(double)native_w/w,sy+240.0/h,out[(size_t)y*w+x]);
    }
}
