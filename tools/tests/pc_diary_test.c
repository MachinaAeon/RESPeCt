/* Real image decoding and production manifest/mapping/compositing, no game assets. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#define STB_IMAGE_IMPLEMENTATION
#include "modfs.c"
#include "message.c"
#ifdef MODFS_PC_DIARIES
#include "file_viewer.c"
#endif
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
void rt_call(uint32_t a){if(a==0x800354F8u || a==0x80035650u || a==0x80035374u){}else if(a==0x8007D034u)n_msg_Layout();else if(a==0x8002C4B8u)sorted++;else if(a==0x8007CF08u || a==0x8007CEB8u){}else abort();}
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

static void bitmap(const char *path,int w,int h,int red){
 FILE *f=fopen(path,"wb");check(f!=NULL,"create authored bitmap");int stride=(w*3+3)&~3,n=54+stride*h;uint8_t *b=calloc(1,n);
 b[0]='B';b[1]='M';wr32(b+2,n);wr32(b+10,54);wr32(b+14,40);wr32(b+18,w);wr32(b+22,h);wr16(b+26,1);wr16(b+28,24);
 for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(red<2)b[54+y*stride+x*3+(red?2:1)]=255;
 fwrite(b,1,n,f);fclose(f);free(b);
}
static void manifest(const char *text){FILE *f=fopen("fixture/pc-bitmaps.txt","wb");check(f!=NULL,"open manifest");fputs(text,f);fclose(f);pc_bitmaps_load("test","fixture");}
static void room(void){wr32(ram_ptr(0x800B4BA0),0);wr32(ram_ptr(0x800B3E20),4);wr32(ram_ptr(0x800BBB7C),0x80110000);wr16(ram_ptr(0x80110000),1);wr16(ram_ptr(0x80110004),8);wr16(ram_ptr(0x80110008),29);wr16(ram_ptr(0x8011000a),0xffff);wr32(ram_ptr(0x800BA9F4),2);wr32(ram_ptr(0x800BA8E8),0x80110008);}

#ifdef MODFS_PC_DIARIES
static void foreground(const char *path,int green){
 bitmap(path,512,512,2);FILE *f=fopen(path,"r+b");check(f!=NULL,"open generated foreground");
 int stride=512*3;for(int y=0;y<40;y++)for(int x=0;x<40;x++){uint8_t c[3]={0,green?255:0,green?0:255};fseek(f,54+(511-y)*stride+x*3,SEEK_SET);fwrite(c,1,3,f);}fclose(f);
}
static void doc01(const char *root,int skip){
 char p[256];snprintf(p,sizeof p,"%s/diary",root);mkdir(p);snprintf(p,sizeof p,"%s/diary/file01",root);mkdir(p);
 for(int i=0;i<4;i++){if(i==skip)continue;snprintf(p,sizeof p,"%s/diary/file01/File01_%x.bmp",root,i);foreground(p,i==1);}
}
static void native_archive(void){
 int si=disc_index("STAGPROG.BIN"),ci=disc_index("CONFFILE.BIN");uint8_t *module=calloc(1,49372);wr32(module,13);uint32_t tab=0x800ceda0u-0x800c3e48u,pg=0x800cf454u-0x800c3e48u;
 for(int i=41;i<120;i++)wr32(module+tab+4*i,1);module[pg]=1;module[pg+1]=module[pg+2]=255;
 uint32_t pn;uint8_t *pk=lz_pack(module,49372,&pn);size_t bytes=(0x21800+pn+2047)&~2047;uint8_t *stage=calloc(1,bytes);memcpy(stage+0x21800,pk,pn);free(pk);free(module);
 uint8_t tim[52]={0};wr32(tim,0x10);wr32(tim+4,2);wr32(tim+8,44);wr16(tim+16,4);wr16(tim+18,4);for(int i=0;i<16;i++)wr16(tim+20+i*2,0x7fff);
 uint32_t tn;pk=lz_pack(tim,52,&tn);uint8_t *conf=calloc(1,2048);wr32(conf,2);wr32(conf+4,12);wr32(conf+8,12+((tn+3)&~3u));memcpy(conf+12,pk,tn);memcpy(conf+rd32(conf+8),pk,tn);free(pk);
 touched[0]=(Touched){disc_files[si].lba,(uint32_t)(bytes/2048),stage,disc_files[si].name};touched[1]=(Touched){disc_files[ci].lba,1,conf,disc_files[ci].name};ntouched=2;

}
static void view(unsigned doc,unsigned pages){
 memset(g_ram,0,RAM_SIZE);R[29]=0x801f0000;
 wr32(ram_ptr(0x800C3E48),13);wr32(ram_ptr(0x800CF9C0),doc+1);wr32(ram_ptr(0x800CF8C0),8);
 wr16(ram_ptr(0x800C09D4),0x800);wr32(ram_ptr(0x800CF994),pages);wr32(ram_ptr(0x800CF990),1);
 uint32_t p=0x800C09D8;wr32(ram_ptr(p+0x3c),0x800C6C40);wr16(ram_ptr(p+0x1c),0x804);
 wr16(ram_ptr(p+0x10),(uint16_t)-0x800);wr16(ram_ptr(p+0x12),(uint16_t)-0x700);wr32(ram_ptr(0x800C39A8),p);
}
static uint32_t *paint(void){uint32_t *p=malloc(320*240*4);for(int i=0;i<320*240;i++)p[i]=0x555555;modfs_pc_composite(p,320,240,320,240);return p;}
#endif
int main(int argc,char **argv){
 mkdir("fixture");
#ifndef MODFS_PC_DIARIES
 check(0,"automatic complete PC diary adapter is absent");
#else
 const char *which=argv[1];
 if(!strcmp(which,"picture-budget-arithmetic")){
  size_t limit=(size_t)MODFS_PC_PICTURE_BUDGET;
  check(pc_picture_fits(0,limit) && !pc_picture_fits(0,(uint64_t)limit+1),"exact shared limit is admitted, excess refused");
  check(!pc_picture_fits(1,0) && !pc_picture_fits(0,UINT64_MAX),"invalid prior allocation and huge request refused");
  pc_bitmap_bytes=limit-16;pc_diary_bytes=16;
  check(!pc_picture_fits(0,1) && pc_picture_fits(16,16) && !pc_picture_fits(16,17),"replacement accounts for old bytes exactly");
  pc_bitmap_bytes=SIZE_MAX;pc_diary_bytes=1;
  check(!pc_picture_fits(0,1),"bitmap counter overflow cannot admit an allocation");
  pc_bitmap_bytes=1;pc_diary_bytes=SIZE_MAX;
  check(!pc_picture_fits(0,1),"diary counter overflow cannot admit an allocation");
  puts("PASS");return 0;
 }
 if(!strncmp(which,"title-",6)){
  mkdir("fixture/diary");bitmap("fixture/diary/D-NAME00.bmp",640,480,1);
  if(!strcmp(which,"title-invalid"))bitmap("fixture/diary/D-NAME00.bmp",4,4,1);
  if(!strcmp(which,"title-order"))bitmap("fixture/diary/D-NAME02.bmp",640,480,0);
  pc_bitmaps_load("names","fixture");view(1,0);wr16(ram_ptr(0x800C09D4),0x40);wr32(ram_ptr(0x800CF8C0),6);wr32(ram_ptr(0x800CF9C0),0);
  wr16(ram_ptr(0x800CF98A),1);wr16(ram_ptr(0x800CF98C),2);*ram_ptr(0x800B3E68)=1;*ram_ptr(0x800B3E69)=2;
  for(unsigned i=0;i<13;i++){uint32_t t=0x80110000+i*512;wr32(ram_ptr(0x800CF420+i*4),t);wr16(ram_ptr(t),0x20+i);wr16(ram_ptr(t+2),0x30);wr16(ram_ptr(t+4),0xffff);}
  memcpy(ram_ptr(0x800EFD70),ram_ptr(0x80110200),6);wr32(ram_ptr(0x800BA8E8),0x800EFD70);wr32(ram_ptr(0x800BA9F4),2);
  if(!strcmp(which,"title-invalid")){check(!modfs_pc_has_bitmaps()&&!modfs_pc_message(0x800EFD70,100,162,8,12),"invalid title retains native glyphs");puts("PASS");return 0;}
  check(modfs_pc_has_bitmaps(),"standalone D-NAME title activates presentation without a page mod");
  check(modfs_pc_message(0x800EFD70,100,162,8,12),"only the emitted native name's glyphs are replaced");
  uint32_t *p=paint();check(p[170*320+120]==0xff0000,"decimal PC00 title follows native document1 identity");free(p);
  if(!strcmp(which,"title-sparse-work")){
   clock_t begin=clock();volatile uint32_t sum=0;
   for(int i=0;i<24;i++){p=paint();sum+=p[170*320+120];free(p);}clock_t dense=clock()-begin;
   bitmap("fixture/diary/D-NAME00.bmp",640,480,2);FILE *f=fopen("fixture/diary/D-NAME00.bmp","r+b");check(f!=NULL,"open sparse title");
   for(int y=340;y<356;y++)for(int x=240;x<256;x++){uint8_t c[3]={0,255,0};fseek(f,54+(479-y)*640*3+x*3,SEEK_SET);fwrite(c,1,3,f);}fclose(f);
   pc_bitmaps_load("sparse","fixture");begin=clock();
   for(int i=0;i<24;i++){p=paint();sum+=p[170*320+120];check(p[170*320+120]==0x00ff00 && p[0]==0x555555,"sparse art and transparent canvas preserved");free(p);}clock_t sparse=clock()-begin;
   printf("title composition CPU clocks: dense=%ld sparse=%ld checksum=%u\n",(long)dense,(long)sparse,(unsigned)sum);
   check(dense>0 && sparse*2<dense,"transparent title canvas cannot require full-canvas sampling work");
  }else if(!strcmp(which,"title-priority")){
   mkdir("later");mkdir("later/diary");bitmap("later/diary/D-NAME00.bmp",640,480,0);pc_bitmaps_load("later","later");
   p=paint();check(p[170*320+120]==0x00ff00,"later valid title overrides independently of page content");free(p);
   bitmap("later/diary/D-NAME00.bmp",4,4,1);pc_bitmaps_load("invalid","later");p=paint();check(p[170*320+120]==0x00ff00,"invalid later title retains previous image atomically");free(p);
  }else if(!strcmp(which,"title-shared-capacity")){
   size_t before=pc_diary_bytes;pc_bitmap_bytes=(128u<<20)-before;
   mkdir("later");mkdir("later/diary");bitmap("later/diary/D-NAME02.bmp",640,480,0);pc_bitmaps_load("combined","later");
   check(pc_diary_titles[2] && pc_diary_bytes==before+640u*480*4,"verified diary and pickup families must coexist beyond128MiB");
  }else if(!strcmp(which,"title-budget")){
   size_t before=pc_diary_bytes;pc_bitmap_bytes=(size_t)MODFS_PC_PICTURE_BUDGET-before;mkdir("later");mkdir("later/diary");bitmap("later/diary/D-NAME02.bmp",640,480,0);pc_bitmaps_load("full","later");
   check(pc_diary_bytes==before&&!pc_diary_titles[2],"labels share the bounded picture budget without publishing failed allocation");
   p=paint();check(p[170*320+120]==0xff0000,"budget rejection preserves the original title");free(p);
  }else if(!strcmp(which,"title-state")){
   ModPcDiaryState state;modfs_pc_diary_get_state(&state);check(state.doc==UINT32_MAX&&state.page==UINT32_MAX,"title has no independent host selection state");
   wr32(ram_ptr(0x800CF8C0),8);check(modfs_pc_diary_check_state(NULL,g_ram,RAM_GAME),"title-only mods do not reject old native reader pagination");
  }else if(!strcmp(which,"title-order")){
   wr16(ram_ptr(0x800CF98A),2);p=paint();check(p[170*320+120]==0xff0000,"cursor advance cannot anticipate the actual emitted name");free(p);
   memcpy(ram_ptr(0x800EFD70),ram_ptr(0x80110400),6);p=paint();check(p[170*320+120]==0x00ff00,"title changes only when the native entry contains the next name");free(p);
  }else if(!strcmp(which,"title-ambiguous")){
   wr32(ram_ptr(0x800CF420+2*4),0x80110200);check(!modfs_pc_message(0x800EFD70,100,162,8,12),"duplicate native names keep native glyphs rather than guess identity");p=paint();check(p[170*320+120]==0x555555,"ambiguous entry cannot paint a title");free(p);
  }else if(!strcmp(which,"title-overlay")){
   wr16(ram_ptr(0x800A55A2),1);check(!modfs_pc_message(0x800EFD70,100,162,8,12),"original overlay retains native glyphs");p=paint();check(p[170*320+120]==0x555555,"original overlay is not overpainted");free(p);
  }else if(!strcmp(which,"title-modes")){
   wr32(ram_ptr(0x800CF8C0),8);check(!modfs_pc_message(0x800EFD70,100,162,8,12),"title does not replace reading text");wr32(ram_ptr(0x800CF8C0),6);
   wr16(ram_ptr(0x800C09D4),0x800);check(!modfs_pc_message(0x800EFD70,100,162,8,12),"reader panel mode cannot replace a selector title");
  }else if(!strcmp(which,"title-wide")){
   int w=813,h=613,nw=426;uint32_t *out=malloc((size_t)w*h*4);for(int i=0;i<w*h;i++)out[i]=0x555555;
   modfs_pc_composite(out,w,h,nw,240);int l=(int)(((int64_t)((nw-320)/2)*w+nw-1)/nw),r=(int)((int64_t)((nw-320)/2+320)*w/nw);
   check(out[300*w+l-1]==0x555555 && out[300*w+r-2]==0xff0000,"whole640-pixel title is fitted without cropping and preserves widescreen sides");free(out);
  }
  puts("PASS");return 0;
 }else if(!strcmp(which,"background")){
  mkdir("fixture/diary");mkdir("fixture/diary/file0c");
  for(int i=0;i<6;i++){char p[128];snprintf(p,sizeof p,"fixture/diary/file0c/file0c_%x.bmp",i);foreground(p,0);}
  for(int i=0;i<3;i++){char p[128];snprintf(p,sizeof p,"fixture/diary/file0c/file0c_b%x.bmp",i);bitmap(p,512,448,i==1?0:i==0?1:2);}
  pc_bitmaps_load("book","fixture");check(modfs_pc_diary_pages(12)==6,"verified PC0c document identity/count");view(12,6);
  for(unsigned page=1;page<=6;page++){check(modfs_pc_diary_load(12,page),"complete foreground and background selected");uint32_t *p=paint();uint32_t expected=page==1?0xff0000:page==2?0x00ff00:0;check(p[220*320+270]==expected,"background selection clamps at the final measured background");free(p);}
 }else{
  doc01("fixture",!strcmp(which,"missing")?2:-1);
  if(!strcmp(which,"invalid"))bitmap("fixture/diary/file01/File01_2.bmp",4,4,1);
  pc_bitmaps_load("book","fixture");
  if(!strcmp(which,"missing")||!strcmp(which,"invalid")){check(!modfs_pc_diary_pages(0)&&!modfs_pc_has_diaries(),"incomplete or incompatible document retains native fallback atomically");puts("PASS");return 0;}
  check(modfs_pc_diary_pages(0)==4 && modfs_pc_diary_pages(1)==0,"complete PC01 maps to PS1doc0 with all four foregrounds");
  check(modfs_pc_has_diaries()&&modfs_pc_has_bitmaps(),"shared CPU/GPU presentation sees diary resources");
  check(pc_bitmaps_source("book","DIARY/FILE01/FILE01_0.BMP"),"automatic files excluded from unrelated native dispatch");
  view(0,4);check(modfs_pc_diary_load(0,1),"cover loaded as first presentation page");
  if(!strcmp(which,"hd-detail")||!strcmp(which,"area-alpha")){
   uint8_t *f=pc_diaries[0].foreground[0];int hd=!strcmp(which,"hd-detail");
   for(int y=0;y<512;y++)for(int x=0;x<512;x++){
    uint8_t *p=f+((size_t)y*512+x)*4;p[0]=hd?(x&1?0:255):255;p[1]=hd?(x&1?255:0):255;p[2]=hd?0:255;p[3]=hd?255:(x&1?255:0);
    if(!hd && (x&1)){p[0]=255;p[1]=p[2]=0;}
   }
   int w=hd?640:320,h=hd?480:240;uint32_t *p=calloc((size_t)w*h,4);modfs_pc_composite(p,w,h,320,240);
   if(hd)check(p[20*w+64]==0xff0000 && p[20*w+65]==0x00ff00,"2x presentation preserves odd source columns instead of repeating native pixels");
   else check(p[10*w+34]==0x800000,"downsampling averages premultiplied alpha without transparent RGB fringes");
   free(p);
  }else if(!strcmp(which,"complete")){
   uint32_t *p=paint();check(p[10*320+34]==0xff0000&&p[200*320+200]==0&&p[10]==0x555555,"cover replaces only the book viewport, preserving native arrows");free(p);
   check(modfs_pc_diary_load(0,2),"second foreground load");p=paint();check(p[10*320+34]==0xff00,"all authored foreground pages selectable");free(p);
   check(!modfs_pc_diary_load(0,0)&&!modfs_pc_diary_load(0,5),"out-of-range page cannot enter adapter");
  }else if(!strcmp(which,"native-priority")||!strcmp(which,"manifest-priority")||!strcmp(which,"whole-priority")){
   native_archive();bitmap("fixture/native.bmp",4,4,0);
   if(!strcmp(which,"whole-priority")){
    FILE *f=fopen("fixture/whole.bin","wb");check(f!=NULL,"whole synthetic archive");fwrite(touched[1].data,1,2048,f);fclose(f);
    apply("later","CONFFILE.BIN","fixture/whole.bin");
   }else if(!strcmp(which,"manifest-priority"))manifest("DIARY 0 1 native.bmp 0 0 4 4\n");
   else {
    apply("later","diary/00/02.png","fixture/native.bmp");check(modfs_pc_diary_pages(0)==4,"rejected native page preserves the previous complete PC book");
    apply("later","diary/00/01.png","fixture/native.bmp");
   }
   check(modfs_pc_diary_pages(0)==0,"accepted later native page or whole archive wins over PC presentation");
   check(pc_diary_bytes==0,"disabling native override releases complete book memory ownership");
  }else if(!strcmp(which,"priority")){
   mkdir("later");mkdir("later/diary");mkdir("later/diary/file01");foreground("later/diary/file01/file01_0.bmp",1);
   pc_bitmaps_load("later","later");check(modfs_pc_diary_load(0,1),"partial later mod can use an already complete baseline");uint32_t *p=paint();check(p[10*320+34]==0xff00,"later mod wins for one page without dropping companions");free(p);
   bitmap("later/diary/file01/file01_2.bmp",4,4,1);foreground("later/diary/file01/file01_0.bmp",0);pc_bitmaps_load("bad","later");p=paint();check(p[10*320+34]==0xff00,"invalid later group preserves the previous complete document atomically");free(p);
  }else if(!strcmp(which,"state")){
   ModPcDiaryState s;modfs_pc_diary_get_state(&s);check(modfs_pc_diary_check_state(&s,g_ram,RAM_GAME),"matching presentation state and native page count accepted");
   wr32(ram_ptr(0x800CF990),2);check(modfs_pc_diary_check_state(&s,g_ram,RAM_GAME),"in-flight target page may differ from loaded page");
   check(modfs_pc_diary_load(0,2),"change displayed page");modfs_pc_diary_set_state(&s);uint32_t *p=paint();check(p[10*320+34]==0xff0000,"saved actual loaded page restored while native slide targets another");free(p);
   s.page=5;check(!modfs_pc_diary_check_state(&s,g_ram,RAM_GAME),"invalid loaded page rejected");
   check(!modfs_pc_diary_check_state(NULL,g_ram,RAM_GAME),"legacy active document cannot silently inherit changed pagination");
   wr32(ram_ptr(0x800CF8C0),0);check(modfs_pc_diary_check_state(NULL,g_ram,RAM_GAME),"legacy gameplay/non-reader state remains usable");
  }else if(!strcmp(which,"cancel-fade")){
   wr32(ram_ptr(0x800CF8C0),10);uint32_t *p=paint();check(p[10*320+34]==0x555555,"closing removes the host document");free(p);wr32(ram_ptr(0x800CF8C0),8);
   wr16(ram_ptr(0x800A55A2),1);p=paint();check(p[10*320+34]==0x555555,"native fade is not overpainted");free(p);wr16(ram_ptr(0x800A55A2),0);
   wr32(ram_ptr(0x800CF9C0),2);p=paint();check(p[10*320+34]==0x555555,"another original document never inherits the old picture");free(p);
  }else if(!strcmp(which,"slide")){
   wr16(ram_ptr(0x800C09D8+0x10),(uint16_t)0x1A00);uint32_t *p=paint();check(p[10*320+34]==0,"offscreen native slide does not show the new foreground early");free(p);
  }else if(!strcmp(which,"fractional-present")){
   int w=813,h=613,nw=426;uint32_t *p=malloc((size_t)w*h*4);for(int i=0;i<w*h;i++)p[i]=0x555555;
   modfs_pc_composite(p,w,h,nw,240);int left=(nw-320)/2+32;
   check(p[(8*h/240)*w+left*w/nw]==0x555555,"fractional viewport rounding cannot draw before its source rectangle");free(p);
  }else if(!strcmp(which,"native-load")){
   wr16(ram_ptr(0x800CF36C+8),256);wr16(ram_ptr(0x800CF36C+0xa),224);A0=2;A1=0;n_files_LoadPage();
   check(GV(u16,0x800CF9C8)==256 && GV(u16,0x800CF9CA)==224,"native page loader uses bounded external presentation dimensions");
   ModPcDiaryState s;modfs_pc_diary_get_state(&s);check(s.doc==0&&s.page==2,"native page loader publishes the actually loaded page");
   check(GV(u8,0x800CF454)==0,"original PS1 page table is never patched");
  }else return 2;
 }
 puts("PASS");
#endif
 return 0;
}
