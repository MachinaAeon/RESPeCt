#include "modfs_memory.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void check(int b,const char *s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static void put(uint8_t *p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
int main(void){
 uint8_t file[4096]={0},table[16]={0};uint32_t packed=99,parts[3]={9,9,9};
 put(table+4,1);put(table+8,2);put(file,3);put(file+4,16);put(file+8,24);put(file+12,32);
 put(file+16,64);put(file+24,80);put(file+32,96);
 modfs_memory_reset();
 check(!modfs_model_request_size(0x800a0cd8,0,&packed,parts)&&packed==0&&!parts[0],"uncached query clears outputs");
 check(modfs_memory_index(0x800a0cd8,file,sizeof file,table,sizeof table),"valid archive indexed");
 check(modfs_model_request_size(0x800a0cd8,0,&packed,parts)&&packed==2048&&parts[0]==64&&parts[1]==80&&parts[2]==96,"split sizes come from final payload headers");
 check(modfs_model_request_size(0x800a0cd8,1,&packed,parts)&&packed==2048&&!parts[0]&&!parts[1]&&!parts[2],"raw block keeps complete byte size");
 check(!modfs_model_request_size(0x800a0cd8,-1,&packed,parts),"negative index rejected");
 check(!modfs_model_request_size(0x800a0cd8,2,&packed,parts),"past-end index rejected");
 check(!modfs_model_request_size(0x800a3edc,0,&packed,parts),"uncached archive rejected");
 check(!modfs_memory_index(123,file,sizeof file,table,sizeof table),"unknown table rejected");
 put(table+4,3);check(!modfs_memory_index(0x800a0cd8,file,sizeof file,table,sizeof table),"table past payload rejected");
 check(modfs_model_request_size(0x800a0cd8,0,&packed,parts)&&parts[2]==96,"failed reindex preserves previous valid index");
 put(table+4,1);put(file+8,15);check(!modfs_memory_index(0x800a0cd8,file,sizeof file,table,sizeof table),"overlapping compressed parts rejected");
 put(file+8,24);put(file+32,0xffffffff);check(!modfs_memory_index(0x800a0cd8,file,sizeof file,table,sizeof table),"impossible decompressed size rejected");
 put(file+32,96);check(!modfs_memory_index(0x800a0cd8,file,sizeof file,table,4),"truncated table rejected");
 modfs_memory_reset();check(!modfs_model_request_size(0x800a0cd8,0,&packed,parts),"reset discards stale metadata");
 puts("PASS: 14 production metadata checks");return 0;
}
