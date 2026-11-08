#define main prior_diary_test_main
#include "pc_diary_test.c"
#undef main

static uint32_t panel(unsigned i,uint32_t callback,unsigned owner,int x) {
 uint32_t p=0x800C09D8+i*0x44;
 wr32(ram_ptr(p+0x3c),callback);wr16(ram_ptr(p+0x1c),0x804);
 wr16(ram_ptr(p+0x24),owner);wr16(ram_ptr(p+0x10),(uint16_t)x);return p;
}
int main(int argc,char **argv) {
#ifndef MODFS_PC_DIARY_KEYS
 check(0,"KEY-AB adapter is absent");
#else
 const char *mode=argv[1];mkdir("fixture");mkdir("fixture/diary");
 bitmap("fixture/diary/KEY-AB.bmp",!strcmp(mode,"invalid")?4:320,240,1);
 if(!strcmp(mode,"budget"))pc_bitmap_bytes=(size_t)MODFS_PC_PICTURE_BUDGET;
 pc_bitmaps_load("keys","fixture");view(0,4);
 uint32_t l=panel(1,0x800C71B8,0,-0x940),r=panel(2,0x800C71B8,0,0x900),e=panel(3,0x800C7334,0,0);
 if(!strcmp(mode,"invalid") || !strcmp(mode,"budget")) {
  check(!modfs_pc_has_bitmaps()&&!modfs_pc_diary_panel(r),"invalid or over-budget atlas leaves native presentation");
 }else {
  check(modfs_pc_has_bitmaps()&&modfs_pc_diary_panel(l)&&modfs_pc_diary_panel(r)&&modfs_pc_diary_panel(e),"standalone atlas owns only its verified reader panels");
  check(!modfs_pc_diary_panel(0x800C09D8),"page content remains native");
  uint8_t *before=malloc(RAM_SIZE);memcpy(before,g_ram,RAM_SIZE);
  uint32_t *p=paint();check(p[84*320+297]==0xff0000&&p[84*320+3]==0x555555,"first page draws only right crop");free(p);
  check(!memcmp(before,g_ram,RAM_SIZE),"query and composition cannot mutate native state");free(before);
  wr32(ram_ptr(0x800CF990),4);wr16(ram_ptr(r+0x1c),0x800);
  p=paint();check(p[84*320+297]==0xff0000,"last page draws EXIT instead of hidden right arrow");free(p);
  wr32(ram_ptr(0x800CF990),5);p=paint();check(p[84*320+297]==0xff0000,"native sentinel keeps selected EXIT");free(p);
  if(!strcmp(mode,"ambiguous")){panel(4,0x800C71B8,0,0x900);check(!modfs_pc_diary_panel(r),"duplicate owner falls back atomically");}
  if(!strcmp(mode,"slide")){wr16(ram_ptr(0x800C09D8+0x22),1);p=paint();check(p[84*320+297]==0x555555&&modfs_pc_diary_panel(r),"slide hides PC keys while callbacks stay owned");free(p);}
  if(!strcmp(mode,"overlay")){wr16(ram_ptr(0x800A55A2),1);check(!modfs_pc_diary_panel(r),"host overlay restores native primitives");p=paint();check(p[84*320+297]==0x555555,"host overlay prevents compositing");free(p);}
  if(!strcmp(mode,"crops")) {
   const int xs[]={1,22,1,22,1,1},ys[]={1,1,12,12,23,36};
   const uint32_t colors[]={0xffff00,0x00ff00,0xff0000,0x0000ff,0xff00ff,0x00ffff};
   memset(pc_diary_keys,0,320*240*4);
   for(int k=0;k<6;k++)for(int y=ys[k];y<ys[k]+(k<4?10:12);y++)for(int x=xs[k];x<xs[k]+20;x++) {
    uint8_t *q=pc_diary_keys+((size_t)y*320+x)*4;q[0]=colors[k]>>16;q[1]=colors[k]>>8;q[2]=colors[k];q[3]=255;
   }
   wr32(ram_ptr(0x800CF990),1);wr16(ram_ptr(r+0x1c),0x804);wr16(ram_ptr(r+0x26),0);
   p=paint();check(p[84*320+297]==0x00ff00,"right normal crop");check(p[84*320+296]==0x555555,"atlas never paints outside its destination");free(p);
   wr16(ram_ptr(r+0x26),8);p=paint();check(p[84*320+297]==0x0000ff,"native counter bit3 selects alternate crop");free(p);
   wr32(ram_ptr(0x800CF990),4);p=paint();check(p[84*320+297]==0x00ffff,"normal EXIT uses row36");free(p);
   wr32(ram_ptr(0x800CF990),5);p=paint();check(p[84*320+297]==0xff00ff,"sentinel EXIT uses row23");free(p);
   wr32(ram_ptr(0x800CF990),2);wr16(ram_ptr(l+0x26),0);p=paint();check(p[84*320+3]==0xffff00,"left crop has independent source");free(p);
  }
  if(!strcmp(mode,"priority")) {
   size_t bytes=pc_diary_bytes;mkdir("later");mkdir("later/diary");bitmap("later/diary/KEY-AB.bmp",320,240,0);pc_bitmaps_load("later","later");
   p=paint();check(p[84*320+297]==0x00ff00&&pc_diary_bytes==bytes,"later atlas replaces atomically without charging twice");free(p);
   bitmap("later/diary/KEY-AB.bmp",4,4,1);pc_bitmaps_load("invalid","later");p=paint();check(p[84*320+297]==0x00ff00&&pc_diary_bytes==bytes,"invalid later atlas retains previous keys");free(p);
  }
  check(!modfs_pc_diary_panel(0)&&!modfs_pc_diary_panel(0x80800000),"foreign addresses never resolve as HUD owners");
  if(!strcmp(mode,"selector")){
   memset(g_ram,0,RAM_SIZE);wr32(ram_ptr(0x800C3E48),13);wr32(ram_ptr(0x800CF8C0),6);wr16(ram_ptr(0x800C09D4),0x40);
   wr16(ram_ptr(0x800CF98A),1);wr16(ram_ptr(0x800CF98C),1);
   const uint32_t slots[]={0x800CF9A8,0x800CF9AA,0x800CF99C,0x800CF99E};
   for(unsigned i=0;i<4;i++){uint32_t p=panel(i, i<2?0x800CBCFC:0x800CBE20,i&1,0);wr16(ram_ptr(p+0x1c),0x44);wr16(ram_ptr(slots[i]),i);}
   for(unsigned i=0;i<4;i++)check(modfs_pc_diary_panel(0x800C09D8+i*0x44),"selector validates original slot and side");
   p=paint();check(p[74*320+69]==0xff0000&&p[74*320+242]==0xff0000&&p[84*320+69]==0xff0000&&p[84*320+242]==0xff0000,"selector crops and both end EXITs are independent");free(p);
   wr16(ram_ptr(0x800CF99E),179);check(!modfs_pc_diary_panel(0x800C09D8),"incomplete selector allocation falls back as a group");
  }
 }
 puts("PASS");
#endif
 return 0;
}
