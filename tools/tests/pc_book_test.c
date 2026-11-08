#define main previous_diary_test
#include "pc_diary_test.c"
#undef main
int main(int argc,char **argv) {
 (void)argc;(void)argv;
#ifndef MODFS_PC_BOOK
 check(0,"BOOK.FLC presentation adapter absent");
#else
 mkdir("fixture");mkdir("fixture/diary");
 size_t n=128+18*(16+6+196*82)+6+2+2+18*3;uint8_t *d=calloc(1,n),*p=d+128;
 wr32(d,n);wr16(d+4,0xAF12);wr16(d+6,18);wr16(d+8,196);wr16(d+10,82);wr16(d+12,8);
 for(unsigned frame=0;frame<18;frame++) {
  uint8_t *begin=p;p+=16;wr16(begin+4,0xf1fa);wr16(begin+6,frame?1:2);
  if(!frame){wr32(p,64);wr16(p+4,4);wr16(p+6,1);p[8]=0;p[9]=18;for(unsigned i=0;i<18;i++)p[10+i*3]=i+1;p+=64;}
  wr32(p,6+196*82);wr16(p+4,16);memset(p+6,frame,196*82);p+=6+196*82;wr32(begin,p-begin);
 }
 check((size_t)(p-d)==n,"fixture layout");FILE *f=fopen("fixture/diary/BOOK.FLC","wb");fwrite(d,1,n,f);fclose(f);free(d);
 pc_bitmaps_load("book","fixture");check(modfs_pc_has_bitmaps(),"book loads without page/title/key companions");
 memset(g_ram,0,RAM_SIZE);wr32(ram_ptr(0x800C3E48),13);wr32(ram_ptr(0x800CF8C0),6);wr16(ram_ptr(0x800C09D4),0x40);
 wr32(ram_ptr(0x800CF8BC),1);*ram_ptr(0x800CF7C8)=5;wr32(ram_ptr(0x800CF878),3);wr16(ram_ptr(0x800CF98C),1);wr16(ram_ptr(0x800CF98A),1);
 for(unsigned i=0;i<3;i++)wr32(ram_ptr(0x800CF87C+i*4),0x80110000+i*32);
 for(unsigned i=0;i<2;i++){uint32_t ot=i?0x800B4BB8:0x800B3D70;wr32(ram_ptr(ot),1);wr32(ram_ptr(ot+4),0x80120000+i*32);}
 check(pc_book_frame()==13 && modfs_pc_book_object(0) && modfs_pc_book_object(4) && !modfs_pc_book_object(5),"only validated native file objects replaced");
 uint32_t *image=paint();check(image[49*320+67]==0x0e0000,"selected frame is composited at original PC destination");free(image);
 uint8_t *ram=malloc(RAM_SIZE);memcpy(ram,g_ram,RAM_SIZE);pc_book_frame();image=paint();free(image);check(!memcmp(ram,g_ram,RAM_SIZE),"presentation cannot mutate guest RAM");free(ram);
 *ram_ptr(0x800CF66B)=1;check(pc_book_frame()==-1 && !modfs_pc_book_object(0),"old ordering-table transition preserves complete native presentation");*ram_ptr(0x800CF66B)=0;
 ModPcBookDraw draw;wr32(ram_ptr(0x80120000),0x123456);check(modfs_pc_book_begin(&draw),"bounded draw snapshot starts");
 wr32(ram_ptr(0x80120000),0xabcdef);wr16(ram_ptr(0x80110020+10),9);modfs_pc_book_end(&draw);
 check(rd32(ram_ptr(0x80120000))==0x123456 && rd16(ram_ptr(0x80110020+10))==9,"only primitive links restored; native animation update survives");
 wr32(ram_ptr(0x800B3D70),12);check(pc_book_frame()==-1,"unsupported ordering table restores original draw");wr32(ram_ptr(0x800B3D70),1);
 wr32(ram_ptr(0x800CF8C0),1);wr32(ram_ptr(0x800CF9C4),0);check(pc_book_frame()==0,"book selected");wr32(ram_ptr(0x800CF9C4),1);check(pc_book_frame()==1,"exit selected");
 wr32(ram_ptr(0x800CF8C0),5);wr16(ram_ptr(0x80110020+10),31);check(pc_book_frame()==2,"native open phase begins");wr16(ram_ptr(0x80110020+10),1);check(pc_book_frame()==13,"native open phase ends");
 wr32(ram_ptr(0x800CF8C0),12);wr16(ram_ptr(0x80110020+10),30);check(pc_book_frame()==13,"close begins open");wr16(ram_ptr(0x80110020+10),1);check(pc_book_frame()==2,"close ends closed");
 wr32(ram_ptr(0x800CF8C0),6);wr16(ram_ptr(0x80110040+10),20);*ram_ptr(0x80110040+13)=0x10;check(pc_book_frame()==14,"forward page begins");wr16(ram_ptr(0x80110040+10),1);check(pc_book_frame()==17,"forward page ends");*ram_ptr(0x80110040+13)=0xf0;check(pc_book_frame()==14,"reverse page ends");
 wr16(ram_ptr(0x80110040+10),21);check(pc_book_frame()==-1 && !modfs_pc_book_object(0),"unknown native phase falls back as a group");wr16(ram_ptr(0x80110040+10),0);
 wr32(ram_ptr(0x800CF9C0),1);check(pc_book_frame()==-1,"direct reading has no selector override");wr32(ram_ptr(0x800CF9C0),0);
 wr32(ram_ptr(0x800CF878),2);check(pc_book_frame()==-1,"ambiguous object ownership restores native graphics");wr32(ram_ptr(0x800CF878),3);
 f=fopen("fixture/diary/BOOK.FLC","wb");fputs("invalid",f);fclose(f);size_t bytes=pc_diary_bytes;pc_bitmaps_load("invalid","fixture");check(pc_book_frame()==13 && pc_diary_bytes==bytes,"invalid later replacement retains complete prior animation");
 puts("PASS");
#endif
 return 0;
}
