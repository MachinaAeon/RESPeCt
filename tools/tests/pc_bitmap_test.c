/* Real image decoding and production manifest/mapping/compositing, no game assets. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define STB_IMAGE_IMPLEMENTATION
#include "modfs.c"
#include "message.c"
#undef far
#include "display.c"
uint8_t g_ram[RAM_SIZE]; uint16_t g_vram[512][1024]; int g_ram_ext_used;
int modmem_enabled(void) { return 0; }
uint32_t g_frame,g_mdec_frame; size_t g_hd_budget=(size_t)1536<<20;
void rt_log(const char *fmt,...){va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);fputc('\n',stderr);va_end(a);}
void rt_fatal(const char *fmt,...){va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);fputc('\n',stderr);va_end(a);abort();}
/* modplug.c: no plugins in these tests */
int modplug_add(const char *mod, const char *mod_dir, const char *up, const char *path) { (void)mod; (void)mod_dir; (void)up; (void)path; return 0; }
int modplug_enabled(void) { return 0; }
uint32_t disc_image_sectors(void){return DISC_SECTORS;}
int cd_read_sector_raw(uint32_t lba,uint8_t out[2352]){memset(out,0,2352);return 0;}
void texpack_add_image(uint32_t h,uint32_t p,int w,int z,uint32_t *d){free(d);}
void texpack_upload(int x,int y,int w,int h){}
void gpu_display(GpuDisplay *d){*d=(GpuDisplay){.w=320,.h=240,.enabled=1};}
int g_widescreen;uint8_t g_sxy_mark[RAM_GAME/4];void sxy_load(uint32_t a,uint32_t b){abort();}
Cpu g_cpu; uint8_t g_scratch[SCRATCH_SIZE]; int g_native_depth, g_rt_budget=1000000; uint32_t g_poll_pc;
static int sorted, gpu_mode; static uint16_t test_hires[2048*1024];
void rt_poll(void){abort();}
void rt_call(uint32_t a){if(a==0x8007D034u)n_msg_Layout();else if(a==0x8002C4B8u)sorted++;else if(a==0x8007CF08u || a==0x8007CEB8u){}else abort();}
int render_scale(void){return 2;} int render_on_gpu(void){return gpu_mode;} void render_sync(void){}
void render_want(int x,int y,int w,int h){} const uint16_t *render_hires(int *stride){*stride=2048;return test_hires;}
const uint16_t *render_low(void){return NULL;} const uint8_t *render_bare(void){return NULL;} const float *render_depth(void){return NULL;}
int g_gap_fill=0;
void rgpu_picture(int x,int y,int w,int h,int gaps,const uint8_t *gamma,uint32_t *out,int ow,int oh,int smooth){memset(out,0,(size_t)ow*oh*4);}

void spu_xa_pcm(const int16_t *lr,int frames,int rate){abort();}
unsigned long mz_crc32(unsigned long c,const unsigned char *p,size_t n){
 uint32_t crc=(uint32_t)c^0xffffffffu;while(n--){crc^=*p++;for(int k=0;k<8;k++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return crc^0xffffffffu;
}
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
#if __has_include("modfs_pc_text.inc")
static void bitmap(const char *path,int w,int h,int red){
 FILE *f=fopen(path,"wb");check(f!=NULL,"create authored bitmap");int stride=(w*3+3)&~3,n=54+stride*h;uint8_t *b=calloc(1,n);
 b[0]='B';b[1]='M';wr32(b+2,n);wr32(b+10,54);wr32(b+14,40);wr32(b+18,w);wr32(b+22,h);wr16(b+26,1);wr16(b+28,24);
 for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(red<2)b[54+y*stride+x*3+(red?2:1)]=255;
 fwrite(b,1,n,f);fclose(f);free(b);
}
static void manifest(const char *text){FILE *f=fopen("fixture/pc-bitmaps.txt","wb");check(f!=NULL,"open manifest");fputs(text,f);fclose(f);pc_bitmaps_load("test","fixture");}
static void room(void){wr32(ram_ptr(0x800B4BA0),0);wr32(ram_ptr(0x800B3E20),4);wr32(ram_ptr(0x800BBB7C),0x80110000);wr16(ram_ptr(0x80110000),1);wr16(ram_ptr(0x80110004),8);wr16(ram_ptr(0x80110008),29);wr16(ram_ptr(0x8011000a),0xffff);wr32(ram_ptr(0x800BA9F4),2);wr32(ram_ptr(0x800BA8E8),0x80110008);}
int main(int argc,char **argv){
 mkdir("fixture");bitmap("fixture/image.bmp",4,4,1);room();
 if(!strncmp(argv[1],"automatic",9)){
  mkdir("fixture/PSX_DATA");mkdir("fixture/PSX_DATA/Message");
  bitmap("fixture/PSX_DATA/Message/0003-00.bmp",512,512,1);
  if(!strcmp(argv[1],"automatic-invalid"))bitmap("fixture/PSX_DATA/Message/0003-00.bmp",4,4,1);
  pc_bitmaps_load("test","fixture");
  if(!strcmp(argv[1],"automatic-invalid")){check(!nused && !modfs_pc_has_bitmaps(),"wrong PC canvas falls back");puts("PASS");return 0;}
  check(nused==1 && modfs_pc_has_bitmaps(),"verified PC path loads without a manifest");
  check(pc_bitmaps_source("test","PSX_DATA/MESSAGE/0003-00.BMP"),"automatic source excluded from ordinary dispatch");
#ifdef MODFS_PC_AUTOMATIC
  /* Generated stream, not original game text. Give it the matching profile
   * to isolate guard behavior from the copyrighted reference metadata. */
  wr16(ram_ptr(0x80110000),3);for(int i=0;i<3;i++)wr16(ram_ptr(0x80110004+i*4),16+i*200);
  uint32_t root=0x80110000+416;for(int i=0;i<68;i++)wr16(ram_ptr(root+i*2),i==67?0xffff:29);
  wr32(ram_ptr(0x800BA8E8),root);
  check(!modfs_pc_message(root,0,0,8,8),"different native text cannot use reference ranges");
  pc_bitmaps[0].native_crc=(uint32_t)mz_crc32(0,ram_ptr(root),68*2);
  check(modfs_pc_message(root,0,0,8,8),"matching native stream selects verified message identity");
  check(!modfs_pc_message(root+136,0,0,8,8),"cursor past the native stream falls back");
  uint32_t *out=calloc(320*240,4);modfs_pc_composite(out,320,240,320,240);check(out[1000]==0xff0000,"whole PC canvas keeps its placement");free(out);
  wr16(ram_ptr(root+2),30);check(!modfs_pc_message(root,0,0,8,8),"UTF8 or raw changed stream falls back");wr16(ram_ptr(root+2),29);
  modfs_pc_caption(2,0);wr32(ram_ptr(0x800BA9E0),2);wr32(ram_ptr(0x800BA9E8),0x800BB570);
  check(modfs_pc_caption_message(0,0,8,8),"caption applies the same reference guard");wr16(ram_ptr(root+2),30);
  check(!modfs_pc_caption_message(0,0,8,8),"changed caption stream also falls back");wr16(ram_ptr(root+2),29);
  if(!strcmp(argv[1],"automatic-pages")){
   wr16(ram_ptr(root+12),0xfffe);wr16(ram_ptr(root+30),0xfffe);
   pc_bitmaps[0].native_crc=(uint32_t)mz_crc32(0,ram_ptr(root),68*2);
   wr16(ram_ptr(0x800BA8E8+10),1);
   check(!modfs_pc_message(root,0,0,8,8),"three-line profile cannot reveal a native second page early");
   wr16(ram_ptr(0x800BA8E8+10),2);check(modfs_pc_message(root,0,0,8,8),"same reference fits an actual three-line message box");
   check(!modfs_pc_caption_message(0,0,8,8),"multi-page caption retains its original per-page glyphs");
  }
  if(!strcmp(argv[1],"automatic-priority")){
   manifest("ROOM 4 2 0 68 image.bmp 0 0 4 4 0 0 320 240 alpha\n");
   check(pc_bitmap_find(root)->native_words==0,"authored mapping wins over automatic profile in the same mod");
  }
#else
  check(0,"automatic native stream guards are absent");
#endif
 }else if(!strcmp(argv[1],"mapping")){
  manifest("ROOM 4 0 0 1 image.bmp 0 0 4 4 10 20 8 8 alpha\n");
  check(nused==1 && modfs_pc_has_bitmaps(),"valid bitmap registered");
  check(modfs_pc_message(0x80110008,10,20,8,8),"exact original room identity and offset selected");
  check(!modfs_pc_message(0x8011000a,10,20,8,8),"unmapped segment falls back");
  check(!modfs_pc_message(0x80110008,9,20,8,8),"partially covered glyph preserved");
  uint32_t *out=calloc(320*240,4);modfs_pc_composite(out,320,240,320,240);
  check(out[20*320+10]==0xff0000 && out[19*320+10]==0,"authored destination composited with crop");
  wr32(ram_ptr(0x800B3E20),3);check(!modfs_pc_message(0x80110008,10,20,8,8),"same RAM reused by another room falls back");
  wr32(ram_ptr(0x800B3E20),4);wr32(ram_ptr(0x800BA9F4),0);memset(out,0,320*240*4);modfs_pc_composite(out,320,240,320,240);check(out[20*320+10]==0,"cancel leaves no raster");free(out);
 }else if(!strcmp(argv[1],"priority")){
  manifest("ROOM 4 0 0 4 image.bmp 0 0 4 4 10 20 8 8 alpha\n");bitmap("fixture/second.bmp",4,4,0);
  manifest("ROOM 4 0 0 4 second.bmp 0 0 4 4 10 20 8 8 alpha\n");
  uint32_t *out=calloc(320*240,4);modfs_pc_composite(out,320,240,320,240);check(out[20*320+10]==0x00ff00,"later valid mapping wins");
  manifest("ROOM 4 0 0 4 absent.bmp 0 0 4 4 10 20 8 8 alpha\n");memset(out,0,320*240*4);modfs_pc_composite(out,320,240,320,240);check(out[20*320+10]==0x00ff00,"invalid later mapping preserves usable earlier mapping");free(out);
 }else if(!strcmp(argv[1],"invalid")){
  bitmap("fixture/placeholder.bmp",1,1,1);
  manifest("ROOM 4 0 0 4 ../outside.bmp 0 0 4 4 10 20 8 8 alpha\nROOM 4 0 0 4 /absolute.bmp 0 0 4 4 10 20 8 8 alpha\nROOM 4 0 0 4 C:/absolute.bmp 0 0 4 4 10 20 8 8 alpha\nROOM 4 0 0 4 placeholder.bmp 0 0 1 1 10 20 8 8 alpha\nROOM 4 0 0 4 image.bmp 3 0 4 4 10 20 8 8 alpha\nROOM 4 0 0 4 image.bmp 0 0 4 4 10 20 0 8 alpha\n");
  check(nused==0&&!modfs_pc_has_bitmaps(),"traversal, absolute paths, placeholders and invalid rectangles fall back");
 }else if(!strcmp(argv[1],"pickup")){
  manifest("PICKUP 1 0 128 image.bmp 0 0 4 4 10 20 8 8 alpha\n");wr32(ram_ptr(0x800BA8E8),0x800A88C8);modfs_pc_pickup(1);
  check(modfs_pc_message(0x800A88C8,10,20,8,8),"pickup selected by original item ID");uint32_t saved=modfs_pc_pickup_state();modfs_pc_pickup(2);check(!modfs_pc_message(0x800A88C8,10,20,8,8),"same pickup buffer with other ID falls back");modfs_pc_restore_pickup(saved);check(modfs_pc_message(0x800A88C8,10,20,8,8),"saved pickup identity restores mapping");modfs_pc_restore_pickup(999);check(!modfs_pc_message(0x800A88C8,10,20,8,8),"invalid state ID falls back");
 }else if(!strcmp(argv[1],"choice")){
  manifest("ROOM 4 0 0 4 image.bmp 0 0 4 4 10 20 8 8 alpha\n");wr32(ram_ptr(0x800BA9C4),1);check(!modfs_pc_message(0x80110008,10,20,8,8),"choice message retains original highlighted glyphs");uint32_t *out=calloc(320*240,4);modfs_pc_composite(out,320,240,320,240);check(out[20*320+10]==0,"bitmap does not cover native choices");free(out);
 }else if(!strcmp(argv[1],"caption")){
#ifndef MODFS_PC_CAPTIONS
 check(0,"caption bitmap identity and state support are absent");
#else
 manifest("\n# Caption uses an explicit native cursor range\nROOM 4 0 1 4 image.bmp 0 0 4 4 10 20 8 8 alpha\n");
 modfs_pc_caption(0,1);wr32(ram_ptr(0x800BA9E0),2);wr32(ram_ptr(0x800BA9E8),0x800BB572);wr32(ram_ptr(0x800BA9F4),0);
 check(modfs_pc_caption_message(10,20,8,8),"caption selects original ROOM identity and copied page offset");
 ModPcBitmapState state;modfs_pc_get_state(&state);check(modfs_pc_valid_state(&state),"mid-caption state accepted");modfs_pc_caption(UINT32_MAX,0);check(!modfs_pc_caption_message(10,20,8,8),"missing identity falls back");modfs_pc_set_state(&state);check(modfs_pc_caption_message(10,20,8,8),"mid-caption restored without a new timer");
 uint32_t *out=calloc(320*240,4);modfs_pc_composite(out,320,240,320,240);check(out[20*320+10]==0xff0000,"caption composited while message box is inactive");free(out);
 state.version=99;check(!modfs_pc_valid_state(&state),"incompatible bitmap state rejected");state.version=1;state.caption_index=256;check(!modfs_pc_valid_state(&state),"invalid caption identity rejected");
#endif
 }else if(!strcmp(argv[1],"entry")){
#ifndef MODFS_PC_CAPTIONS
 check(0,"pickup entry lifecycle support is absent");
#else
 manifest("PICKUP 1 0 128 image.bmp 0 0 4 4 10 20 8 8 alpha\n");modfs_pc_pickup(1);modfs_pc_entry(0x800A88C8);check(modfs_pc_message(0x800A88C8,10,20,8,8),"pickup identity consumed by the immediate native entry");modfs_pc_entry(0x800A88C8);check(!modfs_pc_message(0x800A88C8,10,20,8,8),"door reuse of scratch buffer cannot inherit pickup ID");
#endif
 }else if(!strcmp(argv[1],"ambiguous")){
 manifest("ROOM 4 0 0 4 image.bmp 0 0 4 4 10 20 8 8 alpha\n");wr16(ram_ptr(0x80110000),2);wr16(ram_ptr(0x80110004),12);wr16(ram_ptr(0x80110008),12);check(!modfs_pc_message(0x8011000c,10,20,8,8),"shared native pointers cannot invent a unique message identity");
 }else if(!strcmp(argv[1],"empty-image")){
 bitmap("fixture/empty.bmp",4,4,2);manifest("ROOM 4 0 0 4 empty.bmp 0 0 4 4 10 20 8 8 black\n");check(nused==0&&!modfs_pc_has_bitmaps(),"fully transparent/keyed-empty input cannot hide native text");
 }else if(!strcmp(argv[1],"native-message")){
 n_msg_InitSprites();room();R[29]=0x801f0000;
 manifest("ROOM 4 0 0 16 image.bmp 0 0 4 4 10 20 8 14 alpha\n");
 wr16(ram_ptr(0x800BA8E8+4),(uint16_t)-150);wr16(ram_ptr(0x800BA8E8+6),(uint16_t)-100);wr16(ram_ptr(0x800BA8E8+10),1);
 wr16(ram_ptr(0x8011000a),0xfe02);wr16(ram_ptr(0x8011000c),0xff01);wr16(ram_ptr(0x8011000e),0xfe00);
 n_msg_Update();check(sorted==0 && rd16(ram_ptr(0x800BA968))==1,"native layout still generates the glyph, only its sort is replaced");check(rd16(ram_ptr(0x800BA9FC))==4 && rd16(ram_ptr(0x800BA9D0))==2,"original wait and prompt are executed unchanged");
 size_t old=npc_bitmaps;npc_bitmaps=0;sorted=0;n_msg_Update();check(sorted==1 && rd16(ram_ptr(0x800BA9D0))==1,"without bitmap the same native font is sorted and wait advances");npc_bitmaps=old;
 n_msg_Cancel();uint32_t *out=calloc(320*240,4);modfs_pc_composite(out,320,240,320,240);check(out[20*320+10]==0,"native cancel ends bitmap display");free(out);
 }else if(!strcmp(argv[1],"native-choice")){
 n_msg_InitSprites();room();R[29]=0x801f0000;
 manifest("ROOM 4 0 0 16 image.bmp 0 0 4 4 10 20 8 14 alpha\n");wr16(ram_ptr(0x800BA8E8+4),(uint16_t)-150);wr16(ram_ptr(0x800BA8E8+6),(uint16_t)-100);wr16(ram_ptr(0x800BA8E8+10),1);
 wr16(ram_ptr(0x80110008),0xfd00);wr16(ram_ptr(0x8011000a),0xffff);wr16(ram_ptr(0x800A16C0),29);wr16(ram_ptr(0x800A16C2),0xfd02);
 n_msg_Update();check(sorted==1 && rd16(ram_ptr(0x800BA9FC))==3 && rd32(ram_ptr(0x800BA9C4))==1,"actual native choice keeps highlighted glyph and choice prompt");

 }else if(!strcmp(argv[1],"occlusion")){
 manifest("ROOM 4 0 0 4 image.bmp 0 0 4 4 10 20 8 8 alpha\n");
 const uint32_t flags[]={0x800A55A2,0x800BBD10,0x800BB1B0};
 uint32_t *out=calloc(320*240,4);
 for(unsigned i=0;i<3;i++){
  wr32(ram_ptr(flags[i]),i==0?5:1);
  check(!modfs_pc_message(0x80110008,10,20,8,8),"screen overlay keeps native glyph ordering");
  memset(out,0,320*240*4);modfs_pc_composite(out,320,240,320,240);
  check(!out[20*320+10],"bitmap must not appear above native fade/dimming/white overlay");wr32(ram_ptr(flags[i]),0);
 }
 check(modfs_pc_message(0x80110008,10,20,8,8),"bitmap resumes from native state after overlay ends");free(out);
 }else if(!strcmp(argv[1],"present")){
 manifest("ROOM 4 0 0 4 image.bmp 0 0 4 4 10 20 8 8 alpha\n");uint32_t *out=calloc(2560*1920,4);int w,h;
 gpu_mode=0;display_grab_bgrx(out,&w,&h);check(w==640 && h==480 && out[40*w+20]==0xff0000,"shared GUI presentation composites CPU output");
 gpu_mode=1;g_display_w=800;g_display_h=600;display_grab_bgrx(out,&w,&h);check(w==800 && h==600 && out[50*w+25]==0xff0000,"shared GUI presentation composites GPU output at window size");free(out);
 }else if(!strcmp(argv[1],"diary-manifest")){
 int si=disc_index("STAGPROG.BIN"),ci=disc_index("CONFFILE.BIN");uint8_t *module=calloc(1,49372);wr32(module,13);uint32_t tab=0x800ceda0u-0x800c3e48u,pg=0x800cf454u-0x800c3e48u;
 for(int i=41;i<120;i++)wr32(module+tab+4*i,1);module[pg]=1;module[pg+1]=module[pg+2]=255;
 uint32_t pn;uint8_t *pk=lz_pack(module,49372,&pn);size_t bytes=(0x21800+pn+2047)&~2047;uint8_t *stage=calloc(1,bytes);memcpy(stage+0x21800,pk,pn);free(pk);free(module);
 uint8_t tim[52]={0};wr32(tim,0x10);wr32(tim+4,2);wr32(tim+8,44);wr16(tim+16,4);wr16(tim+18,4);for(int i=0;i<16;i++)wr16(tim+20+i*2,0x7fff);
 uint32_t tn;pk=lz_pack(tim,52,&tn);uint8_t *conf=calloc(1,2048);wr32(conf,2);wr32(conf+4,12);wr32(conf+8,12+((tn+3)&~3u));memcpy(conf+12,pk,tn);memcpy(conf+rd32(conf+8),pk,tn);free(pk);
 touched[0]=(Touched){disc_files[si].lba,(uint32_t)(bytes/2048),stage,disc_files[si].name};touched[1]=(Touched){disc_files[ci].lba,1,conf,disc_files[ci].name};ntouched=2;
 bitmap("fixture/page.bmp",4,4,0);manifest("DIARY 0 1 page.bmp 1 1 2 2\n");uint32_t un;uint8_t *changed=lz_unpack(conf+rd32(conf+8),2048-rd32(conf+8),&un);check(nused==1 && changed && un==52 && rd16(changed+20)==0x3e0,"explicit cropped PC source adapted through native diary compressor");free(changed);
 uint8_t before[2048];memcpy(before,conf,2048);manifest("DIARY 0 2 page.bmp 1 1 2 2\n");check(nused==1 && !memcmp(before,conf,2048),"unavailable output diary page preserves original block atomically");
 }else if(!strcmp(argv[1],"signature")){
  mod_file_hash=14695981039346656037ull;manifest("ROOM 4 0 0 4 image.bmp 0 0 4 4 10 20 8 8 alpha\n");uint64_t a=mod_file_hash;bitmap("fixture/image.bmp",4,4,0);mod_file_hash=14695981039346656037ull;pc_bitmaps_load("test","fixture");check(a!=mod_file_hash,"referenced bitmap bytes participate in state identity");
 }else return 2;
 puts("PASS");return 0;
}
#else
int main(void){check(0,"PC bitmap import implementation is absent");return 1;}
#endif
