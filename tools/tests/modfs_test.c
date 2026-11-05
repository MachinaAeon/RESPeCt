/* Diagnostic only: production grow_all with synthetic archive blocks.
 * Copies into tracked allocations are checked before any unsafe write. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
static struct { void *p; size_t n; } allocations[64];
static int na, fail_next;
static void *tracked_malloc(size_t n) { if(fail_next){fail_next=0;return NULL;} void *p=malloc(n); if(na<64)allocations[na++]=(typeof(allocations[0])){p,n}; return p; }
static void *tracked_copy(void *dst,const void *src,size_t n) {
    uintptr_t d=(uintptr_t)dst;
    for(int i=0;i<na;i++) {
        uintptr_t a=(uintptr_t)allocations[i].p;
        if(d>=a&&d<a+allocations[i].n&&n>allocations[i].n-(d-a)) {
            printf("FAIL: production grow_all attempts %zu-byte copy with only %zu bytes available before rejecting oversized block\n",n,allocations[i].n-(d-a));
            fflush(stdout);exit(42);
        }
    }
    return memcpy(dst,src,n);
}
static int tracked_compare(const void *left,const void *right,size_t n) {
 const void *v[2]={left,right};
 for(int j=0;j<2;j++)for(int i=0;i<na;i++){
  uintptr_t p=(uintptr_t)v[j],a=(uintptr_t)allocations[i].p;
  if(p>=a&&p<a+allocations[i].n&&n>allocations[i].n-(p-a)){
   printf("FAIL: production comparison reads %zu bytes from a %zu-byte remaining backup\n",n,allocations[i].n-(p-a));fflush(stdout);exit(43);
  }
 }
 return memcmp(left,right,n);
}
static void tracked_free(void *p) {
 for(int i=0;i<na;i++) if(allocations[i].p==p){allocations[i]=allocations[--na];break;}
 free(p);
}
#define free tracked_free
#define malloc tracked_malloc
#define memcpy tracked_copy
#define memcmp tracked_compare
#include "modfs.c"
#include "modio.h"
#undef free
#undef malloc
#undef memcpy
#undef memcmp
uint8_t g_ram[RAM_SIZE];
uint16_t g_vram[512][1024];
int g_ram_ext_used;
static int dynamic_buffers;
int modmem_enabled(void) { return dynamic_buffers; }
size_t g_hd_budget=(size_t)1536<<20;
void rt_log(const char *fmt,...) {va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);fputc('\n',stderr);va_end(a);}
void rt_fatal(const char *fmt,...){(void)fmt;abort();}
/* modplug.c: no plugins in these tests */
int modplug_add(const char *mod, const char *mod_dir, const char *up, const char *path) { (void)mod; (void)mod_dir; (void)up; (void)path; return 0; }
int modplug_enabled(void) { return 0; }
/* spu.c: mods' sounds played whole (not in these tests) */
void spu_hd_add(uint32_t crc, uint32_t len, const int16_t *pcm, uint32_t frames, uint32_t wav_rate, uint32_t rate, int64_t loop, uint32_t lead) { (void)crc; (void)len; (void)pcm; (void)frames; (void)wav_rate; (void)rate; (void)loop; (void)lead; }
/* modvideo.c: no videos in these tests */
struct Video *modvideo_open(const char *path, const char **why) { (void)path; *why = "no videos in tests"; return NULL; }
int modvideo_frames(struct Video *v) { (void)v; return 0; }
const uint32_t *modvideo_frame(struct Video *v, int n, int *w, int *h) { (void)v; (void)n; (void)w; (void)h; return NULL; }
uint32_t disc_image_sectors(void){return DISC_SECTORS;}
static int fixture_iso;
static const char *fixture_iso_name="OBJ.DAT";
int cd_read_sector_raw(uint32_t lba,uint8_t out[2352]) {
 memset(out,0,2352);if(!fixture_iso)return 0;
 if(lba==16){memcpy(out+25,"CD001",5);wr32(out+24+158,20);wr32(out+24+166,2048);return 1;}
 if(!strncmp(fixture_iso_name,"B/",2)) {
  if(lba==20){out[24]=48;wr32(out+24+2,21);wr32(out+24+10,2048);out[24+25]=2;out[24+32]=1;out[24+33]='B';return 1;}
  if(lba==21){out[24]=48;out[24+32]=(uint8_t)strlen(fixture_iso_name+2);memcpy(out+24+33,fixture_iso_name+2,strlen(fixture_iso_name+2));return 1;}
 }
 if(lba==20){out[24]=48;out[24+32]=(uint8_t)strlen(fixture_iso_name);memcpy(out+24+33,fixture_iso_name,strlen(fixture_iso_name));return 1;}
 return 0;
}
static void check(int yes, const char *why) { if(!yes){fprintf(stderr,"FAIL: %s\n",why);exit(1);} }
static int hd_calls, hd_w, hd_h, upload_calls;
static uint32_t hd_first[16],hd_second[16],hd_palette[16];
static uint8_t *fake_tim(void) {
 uint8_t *tim=calloc(1,52);wr32(tim,0x10);wr32(tim+4,2);wr32(tim+8,44);wr16(tim+16,4);wr16(tim+18,4);
 for(int i=0;i<16;i++) wr16(tim+20+i*2,0x7fff);
 return tim;
}
#include "modfs_pc_noise.h"
static const char *pc_noise_replace(uint8_t *,size_t,const uint8_t *,size_t,const PcNoisePart *,size_t,size_t,unsigned *);
static uint8_t *noise_fixture(int value) {
 uint8_t *b=calloc(1,4096);wr32(b,2);wr32(b+4,12);wr32(b+8,3120);memcpy(b+12,"pBAV",4);wr32(b+16,7);
 wr16(b+12+18,1);wr16(b+12+22,1);wr16(b+12+32+2048+512+2,4);memset(b+3120,value,32);return b;
}
int main(int argc,char **argv){
 check(argc==2,"name a scenario");

  if(!strncmp(argv[1],"scenery-",8)) {
    int stage=strstr(argv[1],"stage5")?5:strstr(argv[1],"stage6")?6:1;
    int edge=strstr(argv[1],"edge")!=NULL,whole=strstr(argv[1],"whole")!=NULL;
    int custom=strstr(argv[1],"custom")!=NULL,outside=strstr(argv[1],"outside")!=NULL;
    int first=strstr(argv[1],"first")!=NULL;
    uint32_t block=stage==5?2:stage==6?0:outside?17:first?0:12;
    uint32_t sectors=stage==5?12:stage==6?23:20;
    uint32_t len=(stage==5?5:4)*2048,want=(sectors+!edge)*2048;
    fixture_iso_name=stage==5?"B/STGBG5.BIN":stage==6?"B/STGBG6.BIN":"B/STGBG1.BIN";
    int ti=disc_index("TEST.BIN"),fi=disc_index(fixture_iso_name),ki=disc_index("KERNEL.BIN");
    uint8_t *test=calloc(1,0x60000),*original=calloc(1,len),*replacement=calloc(1,want),*kern=calloc(1,0x50000);
    uint32_t tab=0x800A55C8u-TEST_BASE,loc=0x80058D30u;
    strcpy((char *)test+0x50000,fixture_iso_name);
    wr32(test+STAGE_NAMES+stage*4-TEST_BASE,TEST_BASE+0x50000);
    wr32(test+STAGE_SECS+stage*4-TEST_BASE,TEST_BASE+tab);
    wr32(test+STAGE_LOCS+stage*4-TEST_BASE,loc);
    wr32(test+tab+(block+1)*4,len/2048);
    wr32(original,2);wr32(original+4,12);wr32(original+8,20);
    memset(original+12,0x33,len-12);memset(replacement,0x5a,want);
    wr32(kern+0x18,0x80010000u);bcd_msf(disc_files[fi].lba,kern+loc-0x80010000u+0x800);
    touched[0]=(Touched){disc_files[ti].lba,0x60000/2048,test,disc_files[ti].name};
    touched[1]=(Touched){disc_files[fi].lba,len/2048,original,disc_files[fi].name};
    touched[2]=(Touched){disc_files[ki].lba,0x50000/2048,kern,disc_files[ki].name};ntouched=3;fixture_iso=1;
    uint8_t *backup=malloc(len);memcpy(backup,original,len);
    grows[0]=(Grow){fi,whole?0:12,whole?want:want-len+8,whole?len:8,custom?OBJ_ROOM:0,replacement,"resident scenery"};ngrows=nused=1;
    grow_all();
    int accept=edge||outside;
    check(nused==accept,"resident stride rejects overlap, including whole-block explicit capacities");
    check(touched[1].nsec==(accept?want:len)/2048,"resident block extent respects its actual slot");
    if(!accept) {
      check(!memcmp(touched[1].data,backup,len),"rejected resident scenery preserves original bytes");
      check(rd32(test+tab+(block+1)*4)==len/2048,"rejected resident scenery preserves sector table");
    }
    puts("PASS");return 0;
  }
  if(!strncmp(argv[1],"sync-tim-",9)) {
    int image=strstr(argv[1],"image")!=NULL;
    dynamic_buffers=strstr(argv[1],"off")==NULL;
    fixture_iso_name=image?"AEX.DAT":"EV_OBJ.DAT";
    int ti=disc_index("TEST.BIN"),fi=disc_index(fixture_iso_name),ki=disc_index("KERNEL.BIN");
    uint32_t block=image?1:0x187,table=image?0x800A2B3Cu:0x800A195Cu,tab=table-TEST_BASE;
    uint32_t old=image?77824:30720,want=131072;
    uint8_t *test=calloc(1,0x60000),*original=calloc(1,old),*replacement=calloc(1,want),*kern=calloc(1,0x50000);
    for(uint32_t i=block+1;i<=0x190;i++)wr32(test+tab+4*i,old/2048);
    wr32(replacement,0x10);wr32(replacement+4,2);wr32(replacement+8,20);wr32(replacement+16,0x00020002);
    memcpy(original,replacement,32);
    if(strstr(argv[1],"malformed"))wr32(replacement+8,UINT32_MAX);
    touched[0]=(Touched){disc_files[ti].lba,0x60000/2048,test,disc_files[ti].name};
    touched[1]=(Touched){disc_files[fi].lba,old/2048,original,disc_files[fi].name};
    wr32(kern+0x18,0x80010000);bcd_msf(disc_files[fi].lba,kern+(image?0x48D38:0x48D34));
    touched[2]=(Touched){disc_files[ki].lba,0x50000/2048,kern,disc_files[ki].name};ntouched=3;fixture_iso=1;
    grows[0]=(Grow){fi,0,want,old,strstr(argv[1],"custom")?4u*1024*1024:0,replacement,"raw TIM staging"};ngrows=1;nused=1;
    if(strstr(argv[1],"no-space"))ext_used=RAM_SIZE-RAM_GAME-8;
#ifdef MODIO_BOOT_STAGING
    sync_buffers_reserve();
#endif
    grow_all();
#ifdef MODIO_BOOT_STAGING
    sync_buffers_index();
#endif
    int accept=dynamic_buffers && !strstr(argv[1],"malformed") && !strstr(argv[1],"no-space");
    check(touched[1].nsec==(accept?want/2048:old/2048),"only enabled valid synchronous TIM growth gets expanded storage");
    check(nused==(accept?1:0),"unsafe/disabled synchronous growth is not counted as applied");
    check(modio_active()==accept,"accepted growth has a real boot-owned layout");
    if(accept){uint32_t dest;check(modio_acquire(table,block,0x80110000,&dest)==2 && dest>=0x80200000u,"consumer sees boot-owned span");check(modio_check_read(touched[1].lba,want/2048,dest),"final relocated LBA and byte count match owner");check(modio_release(table,block),"release test owner");}
    puts("PASS");return 0;
  }
  if(!strncmp(argv[1],"split-staging-",14)) {
    dynamic_buffers=strcmp(argv[1],"split-staging-off")!=0;
    int ev=!strcmp(argv[1],"split-staging-ev"),excluded=!strcmp(argv[1],"split-staging-excluded"),raw=!strcmp(argv[1],"split-staging-raw");
    fixture_iso_name=ev?"EV_OBJ.DAT":"OBJ.DAT";
    int ti=disc_index("TEST.BIN"),fi=disc_index(fixture_iso_name);
    uint8_t *test=calloc(1,0x60000),*original=calloc(1,2048),*replacement=calloc(1,400u*2048);
    uint32_t block=ev?0x196:excluded?31:0,table=ev?0x800A195Cu:0x800A0CD8u,tab=table-TEST_BASE;
    wr32(test+tab+(block+1)*4,1);
    wr32(original,3);wr32(original+4,16);wr32(original+8,32);wr32(original+12,48);
    memcpy(replacement,original,64);wr32(replacement+16,16);wr32(replacement+32,16);wr32(replacement+48,16);
    if(!strcmp(argv[1],"split-staging-malformed"))wr32(replacement+8,15);
    if(raw)wr32(replacement,1);
    touched[0]=(Touched){disc_files[ti].lba,0x60000/2048,test,disc_files[ti].name};
    touched[1]=(Touched){disc_files[fi].lba,1,original,disc_files[fi].name};
    int ki=disc_index("KERNEL.BIN");uint8_t *kern=calloc(1,0x50000);wr32(kern+0x18,0x80010000u);
    bcd_msf(disc_files[fi].lba,kern+(ev?0x48D34:0x48D30));
    touched[2]=(Touched){disc_files[ki].lba,0x50000/2048,kern,disc_files[ki].name};ntouched=3;fixture_iso=1;
    Packed p={0};p.fi=fi;p.block=block;p.split=!raw;nused=1;
    packed_note(&p,0,"texture-only");check(modfs_dynamic_models()==(!excluded&&!raw),"only supported texture-only split activates room ownership");
    grows[0]=(Grow){fi,0,400u*2048,2048,0,replacement,"split staging"};ngrows=1;
    grow_all();
    int accept=dynamic_buffers && !excluded && !raw && strcmp(argv[1],"split-staging-malformed");
    check(touched[1].nsec==(accept?400:1),"only enabled valid split identity can exceed original compressed cap");
    check(nused==(accept?1:0),"rejected split growth is not applied");
    if(accept){
        memory_prepare();uint32_t packed,parts[3];
        check(modfs_model_request_size(table,(int)block,&packed,parts) && packed==819200 && parts[0]==16 && parts[1]==16 && parts[2]==16,
              "runtime index uses final relocated named archive and sector table");
    }
    puts("PASS");return 0;
  }

 if(!strncmp(argv[1],"wav-",4)) {
   uint8_t valid[48]={0};memcpy(valid,"RIFF",4);wr32(valid+4,40);memcpy(valid+8,"WAVEfmt ",8);wr32(valid+16,16);
   wr16(valid+20,1);wr16(valid+22,1);wr32(valid+24,22050);wr32(valid+28,44100);wr16(valid+32,2);wr16(valid+34,16);memcpy(valid+36,"data",4);wr32(valid+40,4);
   uint32_t frames=0;const char *why="";int16_t *pcm=wav_load(valid,48,22050,&frames,&why);
   check(pcm && frames==2,"valid PCM preserved");tracked_free(pcm);
   if(!strcmp(argv[1],"wav-rate-range")) {
     uint32_t n=75000;uint8_t *large=calloc(1,44+2*n);memcpy(large,valid,44);wr32(large+4,36+2*n);wr32(large+24,4000000000u);wr32(large+40,2*n);
     for(uint32_t i=0;i<n;i++)wr16(large+44+2*i,(uint16_t)(i%1009));
     pcm=wav_load(large,44+2*n,4000000000u,&frames,&why);check(pcm&&frames==n,"full-width rate frame count");
     for(uint32_t i=0;i<n;i++)check(pcm[2*i]==(int16_t)(i%1009),"resampling position must not overflow for valid uint32 rates");
     tracked_free(pcm);tracked_free(large);
   } else if(!strcmp(argv[1],"wav-loop-truncated")) {
     uint32_t rate=77;wr32(valid+40,400);int64_t loop=wav_loop(valid,48,&rate);
     check(loop==-1 && rate==0,"loop metadata traversal rejects truncated chunks");
   } else if(!strcmp(argv[1],"wav-format-bounds")) {
     uint8_t malformed[80]={0};memcpy(malformed,"RIFF",4);memcpy(malformed+8,"WAVEdata",8);wr32(malformed+16,16);
     memcpy(malformed+36,"fmt ",4);wr16(malformed+44,1);wr16(malformed+46,1);wr32(malformed+48,22050);wr16(malformed+58,16);
     pcm=wav_load(malformed,44,22050,&frames,&why);
     check(!pcm,"fmt payload beyond declared input must not be read");
   } else if(!strcmp(argv[1],"wav-truncated")) {
     wr32(valid+40,20);pcm=wav_load(valid,48,22050,&frames,&why);check(!pcm,"truncated data is not silently clamped");
   } else if(!strcmp(argv[1],"wav-empty")) {
     wr32(valid+40,0);pcm=wav_load(valid,44,22050,&frames,&why);check(!pcm,"empty audio rejected");
   }
   puts("PASS");return 0;
 }
 if(!strncmp(argv[1],"pc-noise-",9)) {
   uint8_t *native=calloc(1,12288),*pc=calloc(1,8192),snapshot[12288];
   for(int i=0;i<3;i++){uint8_t *b=noise_fixture(i+1);memcpy(native+i*4096,b,4096);tracked_free(b);}
   memcpy(pc,native,4096);memcpy(pc+4096,native+8192,4096);
   PcNoisePart parts[2]={0};
   if(!strcmp(argv[1],"pc-noise-reference-variant"))pc[4096+3120]=4;
   for(int i=0;i<2;i++)parts[i]=(PcNoisePart){.block=(uint32_t)(i?2:0),.native_offset=(uint32_t)(i*8192),.pc_offset=(uint32_t)(i*4096),.size=4096,
     .crc=(uint32_t)mz_crc32(0,pc+i*4096,4096),.writable=1,.header_crc=(uint32_t)mz_crc32(mz_crc32(0,pc+i*4096,12),pc+i*4096+12,24),.body=3120,.programs=1,.waves=1,.spu_bytes=32};
   memcpy(snapshot,native,sizeof snapshot);size_t size=8192;int reject=0;
   if(!strcmp(argv[1],"pc-noise-change"))pc[4096+3120]=9;
   if(!strcmp(argv[1],"pc-noise-invalid")){pc[3120]=9;wr32(pc+4096+8,3124);reject=1;}
   if(!strcmp(argv[1],"pc-noise-readonly")){pc[3120]=9;pc[4096+3120]=8;parts[1].writable=0;reject=1;}
   if(!strcmp(argv[1],"pc-noise-budget")){wr16(pc+12+32+2048+512+2,8);reject=1;}
   if(!strcmp(argv[1],"pc-noise-reserved")){wr16(pc+12+32+2048+512,65535);reject=1;}
   if(!strcmp(argv[1],"pc-noise-shift")){wr16(pc+12+32+2048+512,1);wr16(pc+12+32+2048+512+2,3);reject=1;}
   if(!strcmp(argv[1],"pc-noise-version")){wr32(pc+16,4);reject=1;}
   if(!strcmp(argv[1],"pc-noise-header-size")){wr32(pc+24,65535);reject=1;}
   if(!strcmp(argv[1],"pc-noise-size")){size--;reject=1;}
   if(!strcmp(argv[1],"pc-noise-overlap")){parts[1].native_offset=0;reject=1;}
   if(!strcmp(argv[1],"pc-noise-reference-variant"))parts[1].writable=0;
   unsigned changed=123;const char *why=pc_noise_replace(native,12288,pc,size,parts,2,8192,&changed);
   check(!!why==reject,"ordered audio preflight result");
   if(reject)check(changed==0 && !memcmp(native,snapshot,sizeof snapshot),"reject all source changes before any native write");
   else if(!strcmp(argv[1],"pc-noise-change")) {
     check(changed==1 && native[8192+3120]==9,"modified bank at its ordered target");snapshot[8192+3120]=9;
     check(!memcmp(native,snapshot,sizeof snapshot),"omitted bank and all other bytes retained");
   } else check(changed==0 && !memcmp(native,snapshot,sizeof snapshot),"original PC banks retain original native bytes");
   puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"pc-audio-families")) {
   FILE *f=fopen("cue.wav","wb");check(f!=NULL,"audio fixture");fwrite("RIFF",1,4,f);fclose(f);
   apply("test","PSX_DATA/WAV/ENEMY/CHO2/SND-000.WAV","cue.wav");
   check(nsfx==0,"a PC sound the PS1 has no bank sound for registers nothing");
   const struct {const char *name;unsigned block,vag;} rows[]={{"DOG/SND-005",23,6},{"CROW/SND-004",22,5},   /* the same recordings */
     {"DOG/SND-006",23,7},{"CROW/SND-002",22,3},{"CROW/SND-003",22,4},{"CROW/SND-006",22,7}};   /* by their event: the bank's sound n */
   for(unsigned i=0;i<sizeof rows/sizeof rows[0];i++){
     char rel[256];snprintf(rel,sizeof rel,"PSX_DATA/WAV/ENEMY/%s.WAV",rows[i].name);apply("test",rel,"cue.wav");
     check(nsfx==(int)i+1 && sfxs[i].block==rows[i].block && sfxs[i].vag==rows[i].vag,"a PC sound reaches the bank sound that plays for its event");
   }
   puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"pc-audio-route")) {
   FILE *f=fopen("dog.wav","wb");check(f!=NULL,"audio route fixture");fwrite("RIFF",1,4,f);fclose(f);
   apply("test","PSX_DATA/WAV/ENEMY/DOG/SND-001.WAV","dog.wav");
   check(nsfx==1 && sfxs[0].block==23 && sfxs[0].vag==2,"verified dog event reaches native bank23 waveform2");
   check(strstr(sfxs[0].what,"PSX_DATA/WAV/ENEMY/DOG/SND-001.WAV")!=NULL,"audio source diagnostic preserved");
   puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"pc-package")) {
   FILE *f=fopen("pc-ui.txt","wb");check(f!=NULL,"PC UI fixture");fputs("@RETURN\nTEST\n",f);fclose(f);
   apply("test","pSx_DaTa/TeXt/UI.txt","pc-ui.txt");
   check(ntextfiles==1 && textfiles[0].kind==4,"PSX_DATA wrapper reaches existing text consumer");
   check(strstr(textfiles[0].what,"pSx_DaTa/TeXt/UI.txt")!=NULL,"diagnostic retains supplied PC path");
   int skipped=nskipped;
   apply("test","PSX_DATA/../TEXT/UI.txt","pc-ui.txt");
   check(ntextfiles==1 && nskipped==skipped+1,"parent traversal refused before dispatch");
   char longpath[700];memset(longpath,'x',sizeof longpath);longpath[sizeof longpath-1]=0;
   apply("test",longpath,"pc-ui.txt");check(ntextfiles==1 && nskipped==skipped+2,"long path refused without truncation");
   pc_bitmap_source_add("test","PSX_DATA/Message/0002-01.bmp");skipped=nskipped;
   apply("test","PSX_DATA/Message/0002-01.bmp","pc-ui.txt");
   check(nskipped==skipped,"authored manifest sources excluded before wrapper normalization");
   puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"raw-models")) {
   const char *names[]={"OBJ.DAT","EV_OBJ.DAT","ITEMDAT.DAT","B/MAPTIM.BIN"};
   FILE *f=fopen("archive.stub","wb");check(f!=NULL,"archive fixture");uint8_t block[2048]={0};fwrite(block,1,sizeof block,f);fclose(f);
   for(unsigned i=0;i<4;i++) {
     int fi=disc_index(names[i]);uint8_t *data=calloc(1,2048);
     touched[0]=(Touched){disc_files[fi].lba,1,data,disc_files[fi].name};ntouched=1;nused=0;mod_memory_models=0;
     apply("test",names[i],"archive.stub");
     check(nused==1,"whole archive accepted");
     check(modfs_dynamic_models()==(i<3),"all whole model archive entry points activate metadata, pictures do not");
     tracked_free(data);ntouched=0;
   }
   int fi=disc_index("OBJ.DAT");uint8_t *data=calloc(1,2048);
   touched[0]=(Touched){disc_files[fi].lba,1,data,disc_files[fi].name};ntouched=1;nused=0;mod_memory_models=0;
   f=fopen("archive.stub","ab");fputc(0,f);fclose(f);apply("test","OBJ.DAT","archive.stub");
   check(!nused&&!modfs_dynamic_models(),"rejected whole archive does not activate arena");tracked_free(data);
   puts("PASS");return 0;
 }

 if(!strcmp(argv[1],"diary")) {
   int si=disc_index("STAGPROG.BIN"),ci=disc_index("CONFFILE.BIN");
   uint8_t *module=calloc(1,49372);wr32(module,13);
   uint32_t tab=0x800ceda0u-0x800c3e48u,pg=0x800cf454u-0x800c3e48u;
   for(int i=41;i<120;i++)wr32(module+tab+4*i,1);
   module[pg]=1;module[pg+1]=module[pg+2]=255;
   uint32_t pn;uint8_t *pk=lz_pack(module,49372,&pn);size_t bytes=(0x21800+pn+2047)&~2047;
   uint8_t *stage=calloc(1,bytes);memcpy(stage+0x21800,pk,pn);tracked_free(pk);tracked_free(module);
   uint8_t *tim=fake_tim();wr16(tim+20,0x001f);uint32_t tn;pk=lz_pack(tim,52,&tn);tracked_free(tim);
   uint8_t *conf=calloc(1,2048);wr32(conf,2);wr32(conf+4,12);wr32(conf+8,12+((tn+3)&~3u));
   memcpy(conf+12,pk,tn);memcpy(conf+rd32(conf+8),pk,tn);tracked_free(pk);
   touched[0]=(Touched){disc_files[si].lba,(uint32_t)(bytes/2048),stage,disc_files[si].name};
   touched[1]=(Touched){disc_files[ci].lba,1,conf,disc_files[ci].name};ntouched=2;
   FILE *f=fopen("image.stub","wb");check(f!=NULL,"diary fixture opened");fputc(1,f);fclose(f);
   apply("test","diary/00/01.png","image.stub");
   check(nused==1 && hd_calls==1,"diary page accepted and HD registered");
   uint32_t un;uint8_t *changed=lz_unpack(conf+rd32(conf+8),2048-rd32(conf+8),&un);
   check(changed && un==52 && rd16(changed+20)==0x7fff,"diary page decodes correctly");tracked_free(changed);
   apply("test","diary/00/02.png","image.stub");check(nused==1 && nskipped==1,"missing diary page rejected");
   puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"long-name")) {
   int ti=disc_index("TEST.BIN");uint8_t *test=calloc(1,0x60000);
   memset(test+GAME_TEXTS,0xff,GAME_TEXTS_END-GAME_TEXTS);
   for(int i=0;i<60;i++){wr16(test+GAME_TEXTS+4*i,29);wr16(test+GAME_TEXTS+4*i+2,0xffff);}
   touched[0]=(Touched){disc_files[ti].lba,0x60000/2048,test,disc_files[ti].name};ntouched=1;
   char input[]="@1\nAAAAAAAAAAAAAAAAAAAAAAAAAAAA";TextFile *file=calloc(1,sizeof *file);*file=(TextFile){1,(uint8_t*)strdup(input),sizeof input-1,"test/text/GAME.txt"};
   textfiles=file;ntextfiles=1;uint8_t original[GAME_TEXTS_END-GAME_TEXTS];memcpy(original,test+GAME_TEXTS,sizeof original);
   texts_done();check(!memcmp(original,test+GAME_TEXTS,sizeof original),"inventory row overflow rejected before modifying text");
   check(nskipped==1,"unsafe name diagnosed");puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"lz-invalid")) {
   uint8_t bad[]={3,0,0,0,0,0,0};uint32_t n;
   check(lz_unpack(bad,sizeof bad,&n)==NULL,"zero-distance compressed reference rejected");puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"hd-font-banks")) {
   int ti=disc_index("TEST.BIN"),fi=disc_index("EV_OBJ.DAT");uint8_t *test=calloc(1,0x60000),*font=calloc(1,4096),img[64];memset(img,255,sizeof img);
   uint8_t *tim=font;wr32(tim,0x10);wr32(tim+4,8);wr32(tim+8,76);wr16(tim+12,368);wr16(tim+14,483);wr16(tim+16,16);wr16(tim+18,2);
   wr16(tim+22,0x7fff);wr16(tim+54,0x03e0);wr32(tim+84,14);wr16(tim+88,320);wr16(tim+90,256);wr16(tim+92,1);wr16(tim+94,1);tim[96]=tim[97]=0x11;
   memcpy(font+2048,font,98);wr16(font+2048+22,0x7c00);wr16(font+2048+54,0x03ff);
   wr32(test+EVOBJ_TABLE-TEST_BASE+4*0x186,0);wr32(test+EVOBJ_TABLE-TEST_BASE+4*0x187,1);wr32(test+EVOBJ_TABLE-TEST_BASE+4*0x188,0);
   touched[0]=(Touched){disc_files[ti].lba,0x60000/2048,test,disc_files[ti].name};touched[1]=(Touched){disc_files[fi].lba,2,font,disc_files[fi].name};ntouched=2;
   FILE *f=fopen("font.stub","wb");check(f!=NULL,"font fixture");fputs("x",f);fclose(f);
   check(own_picture_apply("test","FONT/FONT188.png","FONT/FONT188.PNG","font.stub"),"font recognized");
   check(hd_calls==4,"font accepts palette banks shared by other font sheets");puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"packed-growth")) {
   int fi=disc_index("OBJ.DAT");uint8_t *blk=calloc(1,2048);
   touched[0]=(Touched){disc_files[fi].lba,1,blk,disc_files[fi].name};ntouched=1;
   Packed *p=&packs[0];p->fi=fi;p->len=2048;p->off[1]=2048;p->changed=1;p->plen[0]=32;
   p->part[0]=tracked_malloc(32);p->was[0]=tracked_malloc(16);memset(p->part[0],'X',32);memset(p->was[0],'X',16);p->was_len[0]=16;npacks=1;
   packs_done();check(ngrows==1,"grown block queued");uint32_t n;uint8_t *decoded=lz_unpack(grows[0].data,grows[0].size,&n);
   check(decoded && n==32 && decoded[31]=='X',"grown payload repacked without reading past backup");tracked_free(decoded);puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"hd-palettes") || !strcmp(argv[1],"hd-transparency") || !strcmp(argv[1],"hd-black") || !strcmp(argv[1],"hd-duplicate")) {
   uint8_t tim[98]={0},img[64];memset(img,255,sizeof img);img[0]=img[1]=img[2]=0;wr32(tim,0x10);wr32(tim+4,8);
   wr32(tim+8,76);wr16(tim+12,368);wr16(tim+14,483);wr16(tim+16,16);wr16(tim+18,2);
   wr16(tim+22,0x7fff);wr16(tim+54,0x03e0);
   wr32(tim+84,14);wr16(tim+88,320);wr16(tim+90,256);wr16(tim+92,1);wr16(tim+94,1);tim[96]=0x10;tim[97]=0x11;
   if(!strcmp(argv[1],"hd-transparency")) {
     wr16(tim+20,0x001f);wr16(tim+22,0);wr16(tim+24,0x7fff);
     wr16(tim+52,0x7c00);wr16(tim+54,0);wr16(tim+56,0x03e0);
     tim[96]=0x21;tim[97]=0x22;
   }
   if(!strcmp(argv[1],"hd-black")) {
     wr16(tim+20,0x001f);wr16(tim+22,0x8000);wr16(tim+24,0x7fff);
     wr16(tim+52,0x7c00);wr16(tim+54,0x001f);wr16(tim+56,0x03e0);
     tim[96]=0x21;tim[97]=0x22;
   }
   if(!strcmp(argv[1],"hd-duplicate")) {
     memset(img,255,sizeof img);
     wr16(tim+24,0x7fff);wr16(tim+56,0x001f);
     tim[96]=0x12;tim[97]=0x11;
   }
   check(modfs_hd_add(tim,img,8,2)==1,"multipalette TIM promoted");
   check(hd_calls==2 && hd_palette[0]!=hd_palette[1],"each palette has a separate replacement key");
   check((hd_second[0]&0xffffff)==0xffffff && (hd_second[1]&0xffffff)==0x00ff00,"HD glyphs retain white and green palette variants");
   if(!strcmp(argv[1],"hd-black")) {
     check(hd_first[0]==0xff000000 && hd_first[1]==0xff0000ff,"opaque STP black keeps its original palette index");
   } else if(!strcmp(argv[1],"hd-duplicate")) {
     check(hd_first[0]==0xffffffff && hd_first[1]==0xff0000ff,"equal source colours keep distinct indices in other palettes");
   } else check((hd_first[0]>>24)==0 && (hd_first[1]>>24)==0,"black background preserves the actual transparent source palette index");
   puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"identity-order")) {
   FILE *f=fopen("a.stub","wb");check(f!=NULL,"hash fixture A");fputs("A",f);fclose(f);
   f=fopen("b.stub","wb");check(f!=NULL,"hash fixture B");fputs("B",f);fclose(f);
   mod_file_hash=0;mod_file_count=0;mod_hash_file("MAP/X.BMP","a.stub");mod_hash_file("MAP/X.PNG","b.stub");
   uint64_t first=mod_file_hash;
   mod_file_hash=0;mod_file_count=0;mod_hash_file("MAP/X.PNG","b.stub");mod_hash_file("MAP/X.BMP","a.stub");
   check(first!=mod_file_hash,"different file application order must change the state identity");puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"list")) {
   int ti=disc_index("TEST.BIN");uint8_t *test=calloc(1,0x60000);memset(test+GAME_TEXTS,0xff,GAME_TEXTS_END-GAME_TEXTS);
   for(int i=0;i<60;i++){wr16(test+GAME_TEXTS+4*i,29);wr16(test+GAME_TEXTS+4*i+2,0xffff);}
   touched[0]=(Touched){disc_files[ti].lba,0x60000/2048,test,disc_files[ti].name};ntouched=1;
   FILE *f=fopen("list.stub","wb");check(f!=NULL,"list fixture opened");fputs("@1\nShort\n@2\nThis name is too long\n@3\nBad{noend}\n@4\nBad{wait 1}",f);fclose(f);
   apply("test","text/LIST.txt","list.stub");texts_done();
   check(ext_used>0 && rd16(g_ram+RAM_GAME)==47,"compact inventory name allocated without changing full text");
   check(rd16(test+GAME_TEXTS+4)==29,"original full item name preserved");
   check(modfs_list_text(TEST_BASE+GAME_TEXTS+4)==0x80200000u,"list consumer receives compact text");
   check(modfs_list_text(0x80012345u)==0x80012345u,"unmodified text keeps its address");
   list_texts[1].current=0x80004000u;list_texts[2].current=list_texts[1].original;
   check(modfs_list_text(list_texts[2].current)==list_texts[2].current,"repacked current pointer must not match another item's old address");
   check(modfs_list_text(list_texts[1].current)==0x80200000u,"repacked item keeps its own compact name");
   check(nskipped==3,"oversized, unterminated and control-bearing compact names rejected");puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"ui")) {
   int ti=disc_index("TEST.BIN");uint8_t *test=calloc(1,0x60000);
   memset(test+GAME_TEXTS,0xff,GAME_TEXTS_END-GAME_TEXTS);
   touched[0]=(Touched){disc_files[ti].lba,0x60000/2048,test,disc_files[ti].name};ntouched=1;
   FILE *f=fopen("ui.stub","wb");check(f!=NULL,"UI fixture opened");fputs("@RETURN\nBack\n@NO_ITEM\nEmpty",f);fclose(f);
   apply("test","text/UI.txt","ui.stub");texts_done();
   check(ext_used>0,"UI labels allocated outside the original fixed strings");
   check(rd16(g_ram+RAM_GAME)==30,"translated Return stored as glyphs");puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"chars")) {
   CharMap maps[]={{"a",128},{"ab",129},{"ab",130}};charmap=maps;ncharmap=3;
   uint16_t out[8];char why[160];int n=text_encode("ab",out,8,why,sizeof why);
   check(n==2 && out[0]==129 && out[1]==0xffff,"longest custom token, latest mod wins equal length");
   puts("PASS");return 0;
 }
 if(!strcmp(argv[1],"hd") || !strcmp(argv[1],"hd-zero") || !strcmp(argv[1],"hd-state")) {
   uint8_t *tim=fake_tim();
   if(!strcmp(argv[1],"hd-zero")) {
     uint8_t img[256]={0};wr16(tim+16,0);modfs_hd_add(tim,img,8,8);check(hd_calls==0,"empty TIM rejected");
   } else {
     int fi=disc_index("B/MAPTIM.BIN");uint8_t *data=calloc(1,0x24000);memcpy(data+71680,tim,52);
     touched[0]=(Touched){disc_files[fi].lba,0x24000/2048,data,disc_files[fi].name};ntouched=1;
     FILE *f=fopen("image.stub","wb");check(f!=NULL,"fixture file opened");fputc(1,f);fclose(f);
     check(bmp_apply("test","map/MAP00_1.png","MAP/MAP00_1.PNG","image.stub"),"mapped picture recognized");
     check(hd_calls==1 && hd_w==8 && hd_h==8,"mapped PC picture registered at HD size");
     if(!strcmp(argv[1],"hd-state")) {
       for(int y=0;y<4;y++) for(int x=0;x<4;x++)g_vram[y][x]=0x7fff;
       modfs_state_loaded();check(upload_calls==1,"HD tracking restored from resident native pixels");
     }
   }
   puts("PASS");return 0;
 }
 int ti=disc_index("TEST.BIN"),fi=disc_index("ROOMENV.DAT");
 uint8_t *test=calloc(1,0x60000),*blk=calloc(1,2048),*replacement=calloc(1,65536);
 memset(replacement,0x5a,65536);
 wr32(test+ROOMENV_TABLE-TEST_BASE,0);wr32(test+ROOMENV_TABLE-TEST_BASE+4,1);
 wr32(blk,2);wr32(blk+4,12);wr32(blk+8,20);
 memset(blk+12,0x33,8);
 uint8_t original[2048];memcpy(original,blk,2048);
 touched[0]=(Touched){disc_files[ti].lba,0x60000/2048,test,disc_files[ti].name};
 touched[1]=(Touched){disc_files[fi].lba,1,blk,disc_files[fi].name};ntouched=2;
 uint32_t size=!strcmp(argv[1],"valid")?100:!strcmp(argv[1],"overflow")?UINT32_MAX:65536;
 grows[0]=(Grow){fi,20,size,2028,0,replacement,"synthetic replacement"};ngrows=1;nused=1;
 if(!strcmp(argv[1],"allocation")) fail_next=1;
 if(!strcmp(argv[1],"whole")) grows[0]=(Grow){fi,0,6145,2048,8192,replacement,"whole custom capacity"};
 grow_all();
 if(!strcmp(argv[1],"valid")) {
   check(nused==1,"valid replacement counted");check(touched[1].nsec==1,"size unchanged");
   check(!memcmp(touched[1].data+12,original+12,8),"first part preserved");
   check(touched[1].data[20]==0x5a && touched[1].data[119]==0x5a,"replacement copied");
   for(int i=120;i<2048;i++) check(touched[1].data[i]==0,"padding zeroed");
 } else {check(!memcmp(touched[1].data,original,2048),"rejected block preserved");check(nused==0,"rejected growth not counted as applied");}
 puts("PASS");return 0;
}
/* Boundary substitutes: deterministic RGBA input and an HD registration sink.
 * These test loader semantics, not a PNG decoder or the renderer. Unused
 * hardware interfaces abort if reached. */
uint32_t g_frame,g_mdec_frame;
void gpu_display(GpuDisplay *d){abort();}
void display_grab_rgb(uint8_t *rgb,int *w,int *h){abort();}
int stbi_info_from_memory(const unsigned char *d,int n,int *x,int *y,int *c){*x=*y=8;*c=4;return 1;}
unsigned char *stbi_load_from_memory(const unsigned char *d,int n,int *x,int *y,int *c,int want){
 *x=*y=8;*c=4;unsigned char *p=malloc(256);memset(p,255,256);return p;
}
void stbi_image_free(void *p){tracked_free(p);}
unsigned long mz_crc32(unsigned long c,const unsigned char *p,size_t n){
 uint32_t crc=(uint32_t)c^0xffffffffu;for(size_t i=0;i<n;i++){crc^=p[i];for(int b=0;b<8;b++)crc=crc>>1^((0u-(crc&1))&0xedb88320u);}return crc^0xffffffffu;
}
void texpack_add_image(uint32_t h,uint32_t p,int w,int z,uint32_t *d){if(hd_calls<16){hd_first[hd_calls]=d[0];hd_second[hd_calls]=d[2];hd_palette[hd_calls]=p;}hd_calls++;hd_w=w;hd_h=z;tracked_free(d);}
void spu_xa_pcm(const int16_t *lr,int frames,int rate){abort();}

void texpack_upload(int x,int y,int w,int h){upload_calls++;}
