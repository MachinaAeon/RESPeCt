/* PC KEY-AB presentation, derived exclusively from original HUD owners.
 * Atlas crops/destinations follow the PC consumers; selection and blink use
 * the PS1 callbacks. No host timer, cursor, or additional saved state. */
static void pc_diary_keys_load(const char *mod,const char *root) {
    static const char *const ext[]={"BMP","PNG","JPG","JPEG","TGA"};
    const size_t bytes=320u*240u*4u;
    for(unsigned wrapper=0;wrapper<2;wrapper++)for(unsigned e=0;e<5;e++) {
        char rel[512],path[1024];snprintf(rel,sizeof rel,"%sDIARY/KEY-AB.%s",wrapper?"PSX_DATA/":"",ext[e]);
        if(!pc_bitmap_path(root,rel,path))continue;
        pc_bitmap_source_add(mod,rel);mod_hash_file(rel,path);uint8_t *p=NULL;
        size_t old=pc_diary_keys?bytes:0;
        if(!pc_diary_picture(path,320,240,1,&p) || !pc_picture_fits(old,bytes)) {
            free(p);nskipped++;note("mods: %s/%s: invalid320x240 keys or picture budget: native/previous keys retained",mod,rel);continue;
        }
        free(pc_diary_keys);pc_diary_keys=p;pc_diary_bytes=pc_diary_bytes-old+bytes;nused++;
        note("mods: %s/%s: PC diary keys, native callbacks and blink retained",mod,rel);
    }
}
typedef struct { uint32_t panel; int sx,sy,x,y,h,draw; } PcDiaryKey;
static int pc_diary_key_owner(uint32_t p,uint32_t callback) {
    return p>=0x800C09D8u && p<0x800C09D8u+180*0x44u && !((p-0x800C09D8u)%0x44u) &&
        pc_ram32(p+0x3C)==callback && !(pc_ram16(p+0x1C)&1) && (pc_ram16(p+0x1C)&((callback==0x800CBCFCu || callback==0x800CBE20u)?0x40:0x800));
}
static unsigned pc_diary_keys_resolve(PcDiaryKey out[4]) {
    if(!pc_diary_keys || !pc_bitmap_unoccluded() || pc_ram32(0x800C3E48u)!=13)return 0;
    GpuDisplay d;gpu_display(&d);if(!d.enabled || d.rgb24 || d.w<320 || d.h!=240)return 0;
    unsigned mode=pc_ram32(0x800CF8C0u);
    if(pc_ram16(0x800C09D4u)!=(mode==6?0x40:0x800))return 0;
    if(mode==6 && !pc_ram32(0x800CF9C0u)) {
        unsigned count=pc_ram16(0x800CF98Cu),cursor=pc_ram16(0x800CF98Au);
        if(!count || count>60 || cursor>count+1)return 0;
        static const uint32_t slots[]={0x800CF9A8u,0x800CF9AAu,0x800CF99Cu,0x800CF99Eu};
        for(unsigned i=0;i<4;i++) {
            unsigned index=pc_ram16(slots[i]);if(index>=180)return 0;
            uint32_t p=0x800C09D8u+index*0x44u;
            if(!pc_diary_key_owner(p,i<2?0x800CBCFCu:0x800CBE20u) || pc_ram16(p+0x24)!=(i&1))return 0;
            for(unsigned j=0;j<i;j++)if(out[j].panel==p)return 0;
            int side=i&1;
            if(i<2)out[i]=(PcDiaryKey){p,side?22:1,1+11*((pc_ram16(0x800CF9B8u+side*2)>>3)&1),side?242:69,74,10,!!(pc_ram16(p+0x1C)&4)};
            else {
                int selected=side?cursor==0:cursor==count+1;
                out[i]=(PcDiaryKey){p,1,selected?23:36,side?69:242,84,12,
                    !!(pc_ram16(p+0x1C)&4) && (side?cursor<2:cursor>=count)};
            }
        }
        return 4;
    }
    if(mode!=7 && mode!=8)return 0;
    unsigned page=pc_ram32(0x800CF990u),pages=pc_ram32(0x800CF994u);
    if(!pages || pages>255 || !page || page>pages+1)return 0;
    uint32_t owner=pc_diary_panel();if(!owner)return 0;
    unsigned index=(owner-0x800C09D8u)/0x44u;uint32_t panels[3]={0};
    for(unsigned i=0;i<180;i++) {
        uint32_t p=0x800C09D8u+i*0x44u,cb=pc_ram32(p+0x3C);
        if((cb!=0x800C71B8u && cb!=0x800C7334u) || !pc_diary_key_owner(p,cb) || pc_ram16(p+0x24)!=index)continue;
        unsigned side=cb==0x800C7334u?2:((int16_t)pc_ram16(p+0x10)>0);
        if(panels[side])return 0;panels[side]=p;
    }
    if(!panels[0] || !panels[1] || !panels[2])return 0;
    int still=!pc_ram16(owner+0x22);
    for(unsigned i=0;i<3;i++) {
        uint32_t p=panels[i];
        out[i]=(PcDiaryKey){p,i==1?22:1,i==2?(page==pages?36:23):1+11*((pc_ram16(p+0x26)>>3)&1),i?297:3,84,i==2?12:10,
            still && (pc_ram16(p+0x1C)&4) && (i==0?page>1:i==1?page<pages:page>=pages)};
    }
    return 3;
}
int modfs_pc_diary_panel(uint32_t panel) {
    if(!pc_diary_keys || panel<0x800C09D8u || panel>=0x800C09D8u+180*0x44u || (panel-0x800C09D8u)%0x44u)return 0;
    /* Most HUD panels are unrelated; avoid resolving their owners. */
    uint32_t cb=pc_ram32(panel+0x3C);
    if(cb!=0x800C71B8u && cb!=0x800C7334u && cb!=0x800CBCFCu && cb!=0x800CBE20u)return 0;
    PcDiaryKey keys[4];unsigned n=pc_diary_keys_resolve(keys);
    for(unsigned i=0;i<n;i++)if(keys[i].panel==panel)return 1;
    return 0;
}
static void pc_diary_keys_composite(uint32_t *out,int w,int h,int native_w) {
    PcDiaryKey keys[4];unsigned n=pc_diary_keys_resolve(keys);int left=(native_w-320)/2;
    for(unsigned i=0;i<n;i++)if(keys[i].draw) {
        const PcDiaryKey *k=&keys[i];int dx=left+k->x;
        int x0=(int)(((int64_t)dx*w+native_w-1)/native_w),x1=(int)(((int64_t)(dx+20)*w+native_w-1)/native_w);
        int y0=(int)(((int64_t)k->y*h+239)/240),y1=(int)(((int64_t)(k->y+k->h)*h+239)/240);
        for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++) {
            double ax=k->sx+(double)x*native_w/w-dx,ay=k->sy+(double)y*240/h-k->y;
            out[(size_t)y*w+x]=pc_diary_sample(pc_diary_keys,320,240,ax,ay,ax+(double)native_w/w,ay+240.0/h,out[(size_t)y*w+x]);
        }
    }
}
