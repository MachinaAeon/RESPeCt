/* Asset-free synchronous raw-TIM staging ownership tests. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#if __has_include("modio.h")
#include "modio.h"
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
static void put(uint8_t*p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
int main(void){
 uint32_t p=0;ModIoState state;uint8_t tim[32]={0};
 check(modio_original_size(0x800A195C,0x187)==30720 && modio_original_size(0x800A195C,0x188)==59392,"measured font spans");
 check(modio_original_size(0x800A2B3C,1)==77824 && !modio_original_size(0x800A2B3C,10),"only measured AEX identities");
 put(tim,0x10);put(tim+4,2);put(tim+8,20);put(tim+12,0);put(tim+16,0x00020002);
 check(modio_tim_valid(tim,sizeof tim),"bounded direct16-bit TIM");put(tim+8,200);check(!modio_tim_valid(tim,sizeof tim),"truncated TIM refused");put(tim+8,20);
 put(tim+12,1023);check(!modio_tim_valid(tim,sizeof tim),"VRAM edge overflow refused");put(tim+12,0);
 check(modio_boot(0,0) && modio_state_valid(NULL,0),"disabled layout has no new state requirement");
 check(modio_acquire(0x800A195C,0x187,0x80110000,&p)==1 && p==0x80110000 && !modio_busy(),"no override delegates unchanged");
 check(!modio_boot(0x80100000,65536) && !modio_boot(0x807FF000,65536),"owned extended-RAM bounds required");
 check(modio_boot(0x80200000,65536),"bounded static staging configured");
 check(!modio_register(0x800A195C,0x187,1000,15) && !modio_register(123,0,1000,32),"no growth/unknown identity refused");
 check(modio_register(0x800A195C,0x187,1000,32),"final grown font span registered");
 check(!modio_register(0x800A195C,0x187,2000,32),"conflicting duplicate owner refused");
 check(!modio_check_read(1000,32,0x80110000),"unadapted fixed-destination reader refused before publication");
 check(modio_acquire(0x800A195C,0x187,0x80110000,&p)==2 && p==0x80200000 && modio_busy(),"grown font obtains exclusive source");
 check(!modio_acquire(0x800A195C,0x187,0x80120000,&p),"concurrent reuse refused");
 check(modio_check_read(1000,32,0x80200000) && modio_check_read(1000,32,0xA0200000),"exact read and its RAM alias retain protocol");
 check(!modio_check_read(1000,31,0x80200000) && !modio_check_read(1001,31,0x80200800),"unknown partial reads refused");
 check(!modio_check_read(2000,32,0x80200000) && !modio_check_read(999,33,0x80110000),"wrong LBA and crossing reads refused");
 check(modio_check_read(500,1,0x80110000),"unrelated original read unchanged");
 modio_state_get(&state);check(!modio_state_valid(&state,0x600000),"in-flight lease cannot be saved/resumed");
 check(modio_release(0x800A195C,0x187) && !modio_busy(),"release after original upload synchronization");
 check(modio_state_valid(&state,0x600000) && !modio_state_valid(NULL,0x600000) && !modio_state_valid(&state,0),"exact layout and complete RAM mandatory before APPLY");
 ModIoState wrong=state;wrong.capacity++;check(!modio_state_valid(&wrong,0x600000),"changed layout refused");
 uint8_t target[65552],source[65552];memset(target,0xA5,sizeof target);memset(source,0x5A,sizeof source);
 check(modio_static_copy(target,source,sizeof target) && target[0]==0xA5 && target[65535]==0xA5 && target[65536]==0x5A,"mutable staging preserved, following static bytes reapplied");
 check(!modio_static_copy(target,source,65535),"truncated prefix cannot silently overwrite staging");
 check(modio_boot(0,0) && !modio_state_valid(&state,0x600000),"disabling the layout refuses its old states");
 puts("PASS: synchronous staging identity, TIM bounds, ownership, CD and state checks");return 0;
}
#else
int main(void){fputs("FAIL: synchronous owned image/font staging is not implemented\n",stderr);return 1;}
#endif
