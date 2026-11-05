/* Complete PC documents own only their presentation. Native document IDs,
 * acquisition, page-turn callbacks, repeat delay and closing remain original.
 * A document is committed atomically; missing companions retain native pages. */
#include "modfs_pc_diaries.h"
typedef struct { uint8_t *foreground[9], *background[3]; unsigned pages, backgrounds; } PcDiary;
static PcDiary pc_diaries[13];
static uint8_t *pc_diary_titles[13];
static unsigned pc_diary_title_bounds[13][4];
static uint8_t *pc_diary_keys;
static uint8_t *pc_book;
static void pc_book_load(const char *mod,const char *root);
static void pc_diary_keys_load(const char *mod,const char *root);
static ModPcDiaryState pc_diary_state = {1, UINT32_MAX, UINT32_MAX};
int modfs_pc_has_diaries(void) { if(pc_diary_keys || pc_book)return 1; for (unsigned i=0;i<13;i++) if (pc_diaries[i].pages || pc_diary_titles[i]) return 1; return 0; }
uint32_t modfs_pc_diary_pages(uint32_t doc) { return doc<13 ? pc_diaries[doc].pages : 0; }
static size_t pc_diary_size(const PcDiary *d) { return (size_t)d->pages*512*512*4+(size_t)d->backgrounds*512*448*4; }
static void pc_diary_disable(unsigned doc) {
    if (doc>=13) return;
    PcDiary *d=&pc_diaries[doc];pc_diary_bytes-=pc_diary_size(d);
    for(unsigned i=0;i<9;i++) free(d->foreground[i]);
    for(unsigned i=0;i<3;i++) free(d->background[i]);
    memset(d,0,sizeof *d);
    if (pc_diary_state.doc==doc) pc_diary_state=(ModPcDiaryState){1,UINT32_MAX,UINT32_MAX};
}
/* Exact canvases bound decode allocation before stb is called. Encoded files
 * are separately bounded; no gigantic/truncated input reaches the decoder. */
static int pc_diary_picture(const char *path,unsigned width,unsigned height,int key,uint8_t **out) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int good=!fseek(f,0,SEEK_END);long size=good?ftell(f):-1;fclose(f);
    if(size<=0 || size>8*1024*1024)return 0;
    long n=0;uint8_t *raw=load(path,&n);int w=0,h=0,c=0;
    good=raw && n==size && stbi_info_from_memory(raw,(int)n,&w,&h,&c) && w==(int)width && h==(int)height;
    uint8_t *p=good?stbi_load_from_memory(raw,(int)n,&w,&h,&c,4):NULL;free(raw);
    if(!p || w!=(int)width || h!=(int)height){stbi_image_free(p);return 0;}
    if(key)for(size_t i=0;i<(size_t)width*height*4u;i+=4)if(!(p[i]|p[i+1]|p[i+2]))p[i+3]=0;
    *out=p;return 1;
}
/* D-NAME uses decimal PC IDs and a640x480 canvas. Labels are independent
 * resources, not page companions; invalid replacements retain the prior one. */
static void pc_diary_titles_load(const char *mod,const char *root) {
    static const char *const ext[]={"BMP","PNG","JPG","JPEG","TGA"};
    const size_t bytes=640u*480u*4u;
    for(unsigned i=0;i<13;i++) {
        const PcDiaryProfile *p=&modfs_pc_diaries[i];
        for(unsigned wrapper=0;wrapper<2;wrapper++)for(unsigned e=0;e<5;e++) {
            char rel[512],path[1024];snprintf(rel,sizeof rel,"%sDIARY/D-NAME%02u.%s",wrapper?"PSX_DATA/":"",p->pc,ext[e]);
            if(!pc_bitmap_path(root,rel,path))continue;
            pc_bitmap_source_add(mod,rel);mod_hash_file(rel,path);uint8_t *image=NULL;
            size_t before=pc_diary_titles[p->ps1]?bytes:0;
            if(!pc_diary_picture(path,640,480,1,&image) || !pc_picture_fits(before,bytes)) {
                free(image);nskipped++;note("mods: %s/%s: invalid640x480 title or picture budget: native/previous label retained",mod,rel);continue;
            }
            /* Transparent margins do not need per-frame area sampling.
             * Bounds come from this replacement, never the original art. */
            unsigned bounds[4]={640,480,0,0};
            for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)if(image[((size_t)y*640+x)*4+3]) {
                if(x<bounds[0])bounds[0]=x;if(y<bounds[1])bounds[1]=y;
                if(x+1>bounds[2])bounds[2]=x+1;if(y+1>bounds[3])bounds[3]=y+1;
            }
            memcpy(pc_diary_title_bounds[p->ps1],bounds,sizeof bounds);
            free(pc_diary_titles[p->ps1]);pc_diary_titles[p->ps1]=image;pc_diary_bytes=pc_diary_bytes-before+bytes;nused++;
            note("mods: %s/%s: PC title%u -> native document%u",mod,rel,p->pc,p->ps1);
        }
    }
}
static void pc_diaries_load(const char *mod,const char *root) {
    pc_book_load(mod,root);
    pc_diary_keys_load(mod,root);
    pc_diary_titles_load(mod,root);
    static const char *const ext[]={"BMP","PNG","JPG","JPEG","TGA"};
    for(unsigned n=0;n<sizeof modfs_pc_diaries/sizeof modfs_pc_diaries[0];n++) {
        const PcDiaryProfile *p=&modfs_pc_diaries[n];PcDiary *old=&pc_diaries[p->ps1], next=*old;
        unsigned found=0;int good=1;
        for(unsigned k=0;k<p->foregrounds+p->backgrounds;k++) {
            int bg=k>=p->foregrounds;unsigned index=bg?k-p->foregrounds:k;
            uint8_t **slot=bg?&next.background[index]:&next.foreground[index];
            uint8_t *original=bg?old->background[index]:old->foreground[index];
            char suffix[24];if(!bg)snprintf(suffix,sizeof suffix,"%X",index);
            else if(p->backgrounds==1)snprintf(suffix,sizeof suffix,"B");
            else snprintf(suffix,sizeof suffix,"B%X",index);
            for(unsigned wrapper=0;wrapper<2;wrapper++)for(unsigned e=0;e<sizeof ext/sizeof ext[0];e++) {
                char rel[512],path[1024];snprintf(rel,sizeof rel,"%sDIARY/FILE%02X/FILE%02X_%s.%s",wrapper?"PSX_DATA/":"",p->pc,p->pc,suffix,ext[e]);
                if(!pc_bitmap_path(root,rel,path))continue;
                found++;pc_bitmap_source_add(mod,rel);mod_hash_file(rel,path);
                uint8_t *image=NULL;
                if(!pc_diary_picture(path,512,bg?448:512,!bg,&image)){good=0;continue;}
                if(*slot!=original)free(*slot);*slot=image;
            }
            if(!*slot)good=0;
        }
        if(!found)continue;
        next.pages=p->foregrounds;next.backgrounds=p->backgrounds;
        size_t before=pc_diary_size(old),after=pc_diary_size(&next);
        if(!pc_picture_fits(before,after))good=0;
        if(!good) {
            for(unsigned k=0;k<9;k++)if(next.foreground[k]!=old->foreground[k])free(next.foreground[k]);
            for(unsigned k=0;k<3;k++)if(next.background[k]!=old->background[k])free(next.background[k]);
            note("mods: %s: PC diary FILE%02X incomplete, invalid canvas or picture budget: native/previous complete document retained",mod,p->pc);continue;
        }
        for(unsigned k=0;k<9;k++)if(next.foreground[k]!=old->foreground[k])free(old->foreground[k]);
        for(unsigned k=0;k<3;k++)if(next.background[k]!=old->background[k])free(old->background[k]);
        *old=next;pc_diary_bytes=pc_diary_bytes-before+after;nused+=(int)found;
        note("mods: %s: complete PC diary FILE%02X -> PS1 %u (%u foregrounds, %u backgrounds), native controls retained",mod,p->pc,p->ps1,p->foregrounds,p->backgrounds);
    }
}
int modfs_pc_diary_load(uint32_t doc,uint32_t page) {
    if(!page || page>modfs_pc_diary_pages(doc))return 0;
    pc_diary_state=(ModPcDiaryState){1,doc,page};return 1;
}
void modfs_pc_diary_get_state(ModPcDiaryState *s) { *s=pc_diary_state; }
static int pc_diary_valid(const ModPcDiaryState *s) {
    return s && s->version==1 && ((s->doc==UINT32_MAX && s->page==UINT32_MAX) ||
        (s->doc<13 && s->page && s->page<=modfs_pc_diary_pages(s->doc)));
}
void modfs_pc_diary_set_state(const ModPcDiaryState *s) {
    pc_diary_state=pc_diary_valid(s)?*s:(ModPcDiaryState){1,UINT32_MAX,UINT32_MAX};
}
static uint32_t pc_diary_word(const uint8_t *ram,uint32_t a) { return rd32(ram+(a&0x1fffffu)); }
static uint32_t pc_diary_current(const uint8_t *ram) {
    uint32_t f=pc_diary_word(ram,0x800CF9C0u);
    if(f)return f<=13?f-1:UINT32_MAX;
    unsigned cursor=rd16(ram+0xCF98A),count=rd16(ram+0xCF98C);
    return cursor && cursor<=count && cursor<=60?ram[0xB3E68+cursor-1]:UINT32_MAX;
}
static int pc_diary_reading(const uint8_t *ram) {
    unsigned mode=pc_diary_word(ram,0x800CF8C0u);
    return pc_diary_word(ram,0x800C3E48u)==13 && mode>=7 && mode<=9 && rd16(ram+0xC09D4)==0x800;
}
int modfs_pc_diary_check_state(const ModPcDiaryState *s,const void *data,size_t bytes) {
    if(bytes!=RAM_GAME || !data || (s && !pc_diary_valid(s)))return 0;
    const uint8_t *ram=data;
    if(!pc_diary_reading(ram))return 1;
    uint32_t doc=pc_diary_current(ram),count=modfs_pc_diary_pages(doc);
    if(!count)return 1;
    uint32_t page=pc_diary_word(ram,0x800CF990u);
    return s && s->doc==doc && pc_diary_word(ram,0x800CF994u)==count && page>=1 && page<=count+1;
}
static uint32_t pc_diary_panel(void) {
    uint32_t found=0;
    for(unsigned i=0;i<0xB4;i++) {
        uint32_t p=0x800C09D8u+i*0x44u;unsigned flags=pc_ram16(p+0x1C);
        if(pc_ram32(p+0x3C)!=0x800C6C40u || (flags&0x181))continue;
        if(found)return 0;found=p;
    }
    return found;
}
/* Integrate the source footprint of a displayed pixel, with premultiplied
 * alpha. Sampling at display resolution retains HD source detail and avoids
 * choosing only one colour of an indexed picture's dither pattern at 1x.
 * Outside the source is transparent, including during native page slides. */
static uint32_t pc_diary_sample(const uint8_t *p, int sw, int sh, double x0, double y0,
                                double x1, double y1, uint32_t under) {
    double area=(x1-x0)*(y1-y0), alpha=0, colour[3]={0,0,0};
    if(area<=0)return under;
    double left=x0<0?0:x0, top=y0<0?0:y0;
    double right=x1>sw?sw:x1, bottom=y1>sh?sh:y1;
    if(left>=right || top>=bottom)return under;
    for(int y=(int)floor(top); y<(int)ceil(bottom); y++) {
        double dy=(bottom<y+1?bottom:y+1)-(top>y?top:y);
        for(int x=(int)floor(left); x<(int)ceil(right); x++) {
            double dx=(right<x+1?right:x+1)-(left>x?left:x);
            const uint8_t *q=p+((size_t)y*sw+x)*4;
            double weight=dx*dy*q[3];alpha+=weight;
            for(int c=0;c<3;c++)colour[c]+=q[2-c]*weight;
        }
    }
    double total=255*area;uint32_t out=0;
    for(int c=0;c<3;c++) {
        double value=(colour[c]+((under>>(8*c))&255)*(total-alpha))/total;
        unsigned v=(unsigned)(value<0?0:value>255?255:value+0.5);
        out|=v<<(8*c);
    }
    return out;
}
/* Resolve the name actually emitted by the native menu, not the cursor:
 * n_files_Screen can advance that cursor after emitting this frame's text.
 * No cache or new saved state is needed. Duplicate/unmatched identities fall
 * back conservatively to native glyphs. */
static const uint8_t *pc_diary_title_find(uint32_t text) {
    if(text!=0x800EFD70u || !pc_bitmap_unoccluded() || pc_ram32(0x800C3E48u)!=13 ||
       pc_ram32(0x800CF8C0u)!=6 || pc_ram32(0x800CF9C0u) || pc_ram16(0x800C09D4u)!=0x40 ||
       pc_ram32(0x800BA9F4u)!=2 || pc_ram32(0x800BA9C4u))return NULL;
    unsigned count=pc_ram16(0x800CF98Cu);if(!count || count>60)return NULL;
    int active=0;for(unsigned i=0;i<8;i++)if(pc_ram32(0x800BA8E8u+i*16)==text)active++;
    if(active!=1)return NULL;
    unsigned found=13;
    for(unsigned doc=0;doc<13;doc++) {
        uint32_t name=pc_ram32(0x800CF420u+doc*4);if(name<0x80000000u || name>0x80800000u-512 || (name&1))continue;
        int same=0;
        for(unsigned k=0;k<256;k++) {
            unsigned a=pc_ram16(text+k*2),b=pc_ram16(name+k*2);
            if(a!=b)break;
            if(a==0xFFFF){same=k!=0;break;}
        }
        if(same){if(found!=13)return NULL;found=doc;}
    }
    return found<13?pc_diary_titles[found]:NULL;
}
static int pc_diary_title_message(uint32_t text,int x,int y,int w,int h) {
    if(x<0 || y<0 || w<=0 || h<=0 || (int64_t)x+w>320 || (int64_t)y+h>240 || !pc_diary_title_find(text))return 0;
    GpuDisplay d;gpu_display(&d);return d.enabled && !d.rgb24 && d.w>=320 && d.h==240;
}
static void pc_diary_titles_composite(uint32_t *out,int w,int h,int native_w) {
    const uint8_t *p=pc_diary_title_find(0x800EFD70u);if(!p)return;
    GpuDisplay d;gpu_display(&d);if(!d.enabled || d.rgb24 || d.w<320 || d.h!=240)return;
    int left=(native_w-320)/2;
    int x0=(int)(((int64_t)left*w+native_w-1)/native_w),x1=(int)(((int64_t)(left+320)*w+native_w-1)/native_w);
    unsigned doc=0;while(doc<13 && pc_diary_titles[doc]!=p)doc++;if(doc==13)return;
    const unsigned *bounds=pc_diary_title_bounds[doc];if(bounds[2]<=bounds[0] || bounds[3]<=bounds[1])return;
    int a=(int)floor((left+bounds[0]*0.5)*w/native_w),b=(int)ceil((left+bounds[2]*0.5)*w/native_w);
    if(a>x0)x0=a;if(b<x1)x1=b;
    int y0=(int)floor((double)bounds[1]*h/480),y1=(int)ceil((double)bounds[3]*h/480);
    for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++) {
        double sx0=((double)x*native_w/w-left)*2,sx1=((double)(x+1)*native_w/w-left)*2;
        out[(size_t)y*w+x]=pc_diary_sample(p,640,480,sx0,(double)y*480/h,sx1,(double)(y+1)*480/h,out[(size_t)y*w+x]);
    }
}
static void pc_diary_composite(uint32_t *out,int w,int h,int native_w) {
    const ModPcDiaryState *s=&pc_diary_state;
    if(!modfs_pc_has_diaries() || !pc_diary_valid(s) || s->doc>=13 || !pc_bitmap_unoccluded() ||
        pc_ram32(0x800CF8C0u)!=8 || !pc_diary_reading(g_ram) || pc_diary_current(g_ram)!=s->doc ||
        pc_ram32(0x800CF994u)!=modfs_pc_diary_pages(s->doc))return;
    uint32_t panel=pc_diary_panel();if(!panel)return;
    const PcDiary *d=&pc_diaries[s->doc];const uint8_t *fg=d->foreground[s->page-1],*bg=NULL;
    if(d->backgrounds)bg=d->background[s->page<=d->backgrounds?s->page-1:d->backgrounds-1];
    int left=(native_w-320)/2+32,top=8;
    int fx=native_w/2+(int16_t)pc_ram16(panel+0x10)/16,fy=120+(int16_t)pc_ram16(panel+0x12)/16;
    int x0=(int)(((int64_t)left*w+native_w-1)/native_w),x1=(int)(((int64_t)(left+256)*w+native_w-1)/native_w);
    int y0=(int)(((int64_t)top*h+239)/240),y1=(int)(((int64_t)(top+224)*h+239)/240);
    for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++) {
        double nx0=(double)x*native_w/w,nx1=(double)(x+1)*native_w/w;
        double ny0=(double)y*240/h,ny1=(double)(y+1)*240/h;
        uint32_t v=0;
        if(bg)v=pc_diary_sample(bg,512,448,(nx0-left)*2,(ny0-top)*2,(nx1-left)*2,(ny1-top)*2,0);
        v=pc_diary_sample(fg,512,512,(nx0-fx)*2,(ny0-fy)*(512.0/224),
                          (nx1-fx)*2,(ny1-fy)*(512.0/224),v);
        out[(size_t)y*w+x]=v;
    }
}

#include "modfs_pc_diary_keys.inc"

#include "modfs_pc_book.inc"
