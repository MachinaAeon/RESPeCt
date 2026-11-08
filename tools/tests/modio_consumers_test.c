/* Native font/image consumer wiring; synthetic CD data only. */
#include "decomp.h"
#include "rt.h"
#include "modio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
Cpu g_cpu;uint8_t g_ram[RAM_SIZE],g_scratch[SCRATCH_SIZE];
int g_native_depth,g_ram_ext_used;int32_t g_rt_budget=10000000;uint32_t g_poll_pc;
uint8_t g_sxy_mark[RAM_GAME/4];uint32_t g_sxy_filter[8];
void sxy_load(uint32_t p,uint32_t v){(void)p;(void)v;abort();}
void sxy_store(uint32_t p,uint32_t v){(void)p;(void)v;abort();}
uint32_t mem_slow_r32(uint32_t a){(void)a;abort();}
void modfs_pc_pickup(uint32_t a){(void)a;abort();}
void modfs_pc_entry(uint32_t a){(void)a;abort();}
void modfs_pc_caption(uint32_t a,uint32_t b){(void)a;(void)b;abort();}
int modfs_pc_message(uint32_t a,int b,int c,int d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;abort();}
int modfs_pc_caption_message(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;abort();}
static unsigned reads,uploads;static int grown,expect_fatal;static jmp_buf escape;
static void check(int b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
void rt_fatal(const char *fmt,...){(void)fmt;if(expect_fatal)longjmp(escape,1);abort();}
void rt_log(const char *fmt,...){(void)fmt;}
void rt_poll(void){}
void n_font_LoadPage(void);void n_op_LoadImage(void);void n_cd_QueueRead(void);
void rt_call(uint32_t a){
 if(a==0x800115C0u){
  uint32_t dest=MEM_R32(R[29]+16);reads++;
  check(dest==(grown?0x80200000u:0x80110000u),"grown resource must read into its owned extended buffer");
  check(!grown || modio_busy(),"lease spans the original synchronous read");V0=0;
 }else if(a==0x8006F370u){uploads++;check(A0==(grown?0x80200000u:0x80110000u),"upload consumes the exact read buffer");check(!grown || modio_busy(),"lease held through original upload sync");V0=0;}
 else V0=0;
}
int main(int argc,char**argv){
 check(argc==2,"scenario required");R[29]=0x801FF000u;
 grown=strcmp(argv[1],"original")!=0;
 if(grown){check(modio_boot(0x80200000,131072),"configure staging");check(modio_register(0x800A195C,0x187,1000,32),"font metadata");check(modio_register(0x800A2B3C,1,2000,64),"image metadata");}
 if(!strcmp(argv[1],"queue")){
  MEM_W32(0x80062D74,0x80040000);MEM_W16(0x80062EFC,15);MEM_W16(0x80062EFE,0);
  MEM_W8(0x801F0000,0);MEM_W8(0x801F0001,0x15);MEM_W8(0x801F0002,0x25);
  A0=0x80110000;A1=32;A2=0x801F0000;expect_fatal=1;
  if(!setjmp(escape)){n_cd_QueueRead();check(0,"unowned grown resource must be refused before queue publication");}
  check(MEM_R16(0x80062EFE)==0 && MEM_R16(0x80040000)==0,"refusal preserves queue memory");
 }else{
  if(!strcmp(argv[1],"image")){
   MEM_W32(0x800B3D54,0x80110000);MEM_W32(0x80041000,0x80042000);MEM_W16(0x80042002,1);A0=0x80041000;n_op_LoadImage();
   check(MEM_R32(0x80041000)==0x80042004 && MEM_R32(0x800B3D54)==0x80110000,"script advances normally without rewriting shared pointer");
  }else{A0=0x80110000;A1=2;n_font_LoadPage();}
  check(reads==1 && uploads==1 && !modio_busy(),"one original read/upload, then release");
 }
 puts("PASS native resource consumer");return 0;
}
