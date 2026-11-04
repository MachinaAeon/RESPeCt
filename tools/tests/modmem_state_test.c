/* State validation invokes the production SS_CHECK/SS_APPLY visitor. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../port/src/savestate.c"
#include "../../port/src/modmem.h"
#include "../../port/src/config.h"
Cpu g_cpu;
uint8_t g_ram[RAM_SIZE],g_scratch[SCRATCH_SIZE];
int g_ram_ext_used;
Config g_cfg;
uint32_t g_frame,g_poll_pc,g_resume_pc;
int g_resume_miss;
uint64_t modfs_signature(void) { return 1; }
uint32_t modfs_static_end(void) { return 0x80200130; }
static int dynamic_models=1;
int modfs_dynamic_models(void) { return dynamic_models; }
int modfs_model_request_size(uint32_t table,int index,uint32_t *packed,uint32_t parts[3]) {
    (void)table;(void)index;*packed=2048;parts[0]=parts[1]=parts[2]=16;return 1;
}
void rt_log(const char *s,...) { (void)s; }
void rt_fatal(const char *s,...) { (void)s; abort(); }
#define EMPTY(n) void n(StateIO *io) { (void)io; }
EMPTY(rt_state) EMPTY(hw_state) EMPTY(timers_state) EMPTY(sio_state)
EMPTY(gpu_state) EMPTY(gte_state) EMPTY(spu_state) EMPTY(cd_state)
EMPTY(mdec_state) EMPTY(bios_state) EMPTY(card_state) EMPTY(thread_state)
EMPTY(decomp_state) EMPTY(texpack_state)
int rt_in_interrupt(void) { return 0; }
int thread_current(void) { return 0; }
void render_sync(void) {} void render_reload(void) {} void gte_reset_caches(void) {}
void spu_reset_output(void) {} void cd_reapply_volume(void) {} void rt_reset_call_cache(void) {}
void modfs_state_loaded(void) {} void thread_resume_all(void) { abort(); }
static int ss_done;
void plat_ss_done(int a,int b,const char *c) { (void)a;(void)b;(void)c; ss_done++; }
int rt_resume_candidates(uint32_t pc,const RtModule **m,const RtFunc **f,int max) { return 0; }
void interp_resume(const RtModule *m,const RtFunc *f) {}
int modfs_pc_has_diaries(void) { return 0; }
void modfs_pc_diary_get_state(ModPcDiaryState *s) { *s=(ModPcDiaryState){1,UINT32_MAX,UINT32_MAX}; }
void modfs_pc_diary_set_state(const ModPcDiaryState *s) { (void)s; }
int modfs_pc_diary_check_state(const ModPcDiaryState *s,const void *ram,size_t bytes) { return !s && ram && bytes==RAM_GAME; }
static int bitmaps;
static ModPcBitmapState bitmap_state;
int modfs_pc_has_bitmaps(void) { return bitmaps; }
void modfs_pc_get_state(ModPcBitmapState *s) { *s=bitmap_state; }
int modfs_pc_valid_state(const ModPcBitmapState *s) { return s->version==1; }
void modfs_pc_set_state(const ModPcBitmapState *s) { bitmap_state=*s; }

/* Synthetic MIPS frames, not game assets. The saved image, not live RAM,
 * supplies prologues, return slots and JAL edges for compatibility checks. */
static const uint8_t test_signature[32];
static const RtFunc guard_kernel[]={{.addr=0x800115C0},{.addr=0x800116A4},{.addr=0x80014B14},{.addr=0x80014BBC},{.addr=0x80016F78},{.addr=0x800170B8},{.addr=0x800255F8},{.addr=0x80025770}};
static const RtFunc guard_test[]={{.addr=0x8007428C},{.addr=0x80074AD0}};
const RtModule rt_modules[]={
 {.name="kernel",.vram=0x80010000,.code_lo=0x80010000,.code_hi=0x80060000,.sig=test_signature,.sig_len=32,.funcs=guard_kernel,.nfuncs=8},
 {.name="test",.vram=0x8006C118,.code_lo=0x8006C118,.code_hi=0x800C3E48,.sig=test_signature,.sig_len=32,.funcs=guard_test,.nfuncs=2}
};
const int rt_module_count=2;
static void guard_word(uint32_t a,uint32_t v){memcpy(g_ram+(a&0x1fffffff),&v,4);}
static void guard_jal(uint32_t pc,uint32_t callee){guard_word(pc,0x0C000000u|((callee>>2)&0x03ffffffu));}
static void legacy_sync_guard_tests(void){
 _putenv_s("RT_MOD_ARENA","1");
 memset(g_ram,0,sizeof g_ram);Cpu cpu={0};cpu.r[29]=0x801FF000;
 uint32_t poll=0x80074700,current=1,used=1,started=1;Cpu other={0};
 Chunk chunks[]={{"rt.g_ram",RAM_GAME,g_ram},{"rt.g_cpu",sizeof cpu,(const uint8_t*)&cpu},
  {"rt.g_poll_pc",4,(const uint8_t*)&poll},{"thread.current",4,(const uint8_t*)&current},
  {"thread.t0.used",4,(const uint8_t*)&used},{"thread.t0.started",4,(const uint8_t*)&started},
  {"thread.t0.cpu",sizeof other,(const uint8_t*)&other}};
 StateIO io={0};io.mode=SS_CHECK;io.chunks=chunks;io.nchunks=7;
 visit_all(&io);assert(io.bad && "legacy direct synchronous resume must be rejected before apply");
 _putenv_s("RT_MOD_ARENA","0");io.bad=0;visit_all(&io);assert(!io.bad);
 _putenv_s("RT_MOD_ARENA","1");dynamic_models=0;io.bad=0;visit_all(&io);assert(!io.bad);dynamic_models=1;
 poll=0x80017010;cpu.r[31]=0x8007470C;guard_jal(0x80074704,0x80016F78);
 io.bad=0;visit_all(&io);assert(io.bad && "LZ leaf return must witness the synchronous caller");
 poll=0x80014B30;guard_word(0x80014B14,0x27BDFFE8);guard_word(0x80014B18,0xAFBF0014);
 guard_word(0x800115C0,0x27BDFFD8);guard_word(0x800115C4,0xAFBF0024);
 guard_jal(0x80011648,0x80014B14);guard_jal(0x8007468C,0x800115C0);
 guard_word(cpu.r[29]+20,0x80011650);guard_word(cpu.r[29]+24+36,0x80074694);
 io.bad=0;visit_all(&io);assert(io.bad && "CD nested frame chain must witness the synchronous caller");
 /* Delayed fixed prologues are the real CD wait/load-file layout. */
 poll=0x800256D0;cpu.r[29]=0x801FF000;
 guard_word(0x800255F8,0x3C028006);guard_word(0x800255FC,0x8C42A1C4);
 guard_word(0x80025600,0x3C058006);guard_word(0x80025604,0x8CA5A1C8);
 guard_word(0x80025608,0x27BDFFD8);guard_word(0x8002560C,0xAFBF0020);
 guard_word(0x800115C4,0xAFBF0020);guard_jal(0x80011648,0x800255F8);
 guard_word(cpu.r[29]+32,0x80011650);guard_word(cpu.r[29]+40+32,0x80074694);
 io.bad=0;visit_all(&io);assert(io.bad && "real delayed CD prologue must reach synchronous caller");
 /* Preserve the earlier nested-wait fixture for the exclusion cases. */
 poll=0x80014B30;guard_word(0x800115C4,0xAFBF0024);guard_jal(0x80011648,0x80014B14);
 /* A stale-looking RA without a matching call edge is not evidence. */
 guard_word(cpu.r[29]+24+36,0x800746A4);guard_word(cpu.r[29]+100,0x80074694);
 io.bad=0;visit_all(&io);assert(!io.bad && "unrelated words on stack cannot identify the loader");
 /* A suspended noncurrent thread resumes from its own r31 and sp. */
 poll=0x80011690;cpu.r[29]=0x801FE000;cpu.r[31]=0;
 other.r[29]=0x801FF000;other.r[31]=0x80014B30;guard_word(other.r[29]+24+36,0x80074694);
 io.bad=0;visit_all(&io);assert(io.bad && "suspended thread synchronous chain must be rejected");
 started=0;io.bad=0;visit_all(&io);assert(!io.bad && "unstarted thread enters the new native normally");started=1;
 used=0;io.bad=0;visit_all(&io);assert(!io.bad);
 poll=0x80074700;g_ram[0x6C118]=1;io.bad=0;visit_all(&io);assert(!io.bad && "same address in another overlay is not TEST");g_ram[0x6C118]=0;
 /* Even if unwind fails, an original fixed-buffer protected read is unsafe. */
 poll=0x80011690;uint32_t req[7]={3,0,0x800A0CD8,0,0x80108000,0,2048};memcpy(g_ram+0xB3E40,req,sizeof req);
 io.bad=0;visit_all(&io);assert(io.bad && "active legacy protected split read must be rejected");memset(g_ram+0xB3E40,0,sizeof req);
 /* Existing original models during startup still need allocations before
  * a fresh-room boundary; reject before APPLY, rather than failing later. */
 poll=0x80011690;used=0;guard_word(0x800B4D78,0x80190000);guard_word(0x800B4D7C,1);guard_word(0x800BB19C,0);
 io.bad=0;visit_all(&io);assert(io.bad && "legacy initialized model graph cannot finish startup under a different arena owner");
 guard_word(0x800BB19C,1);io.bad=0;visit_all(&io);assert(!io.bad && "ordinary legacy gameplay checkpoint may wait for a fresh room");
 memset(g_ram,0,sizeof g_ram);modmem_clear();
 puts("PASS legacy synchronous direct, LZ/CD exact chains, suspended threads, fixed reads and safe exclusions");
 /* Service remains pending across a fiber switch; no save/load takes place. */
 g_native_depth=g_hle_depth=0;g_poll_pc=0x80014B30;g_ss_pending=1;pending_load=1;ss_done=0;
 snprintf(pending_path,sizeof pending_path,"missing-sync-guard-state");modmem_sync_begin();ss_service();
 assert(g_ss_pending==1 && !ss_done && "cross-thread synchronous busy flag must defer save/load");
 modmem_sync_end();ss_service();assert(!g_ss_pending && ss_done==1);
 puts("PASS synchronous cross-thread barrier defers service, asynchronous service remains available");
}

int main(void) {
    legacy_sync_guard_tests();
    _putenv_s("RT_MOD_ARENA","1");
    modmem_begin(0x80040000);
    uint32_t top=0x80200130,owner=0x80040000;
    memcpy(g_ram+0xB4D78,&top,4); memcpy(g_ram+0xBBB88,&owner,4);
    ModMemoryState state; modmem_export(&state);
    Chunk chunks[] = {{"mods.arena",sizeof state,(const uint8_t *)&state},{"rt.g_ram",RAM_GAME,g_ram}};
    StateIO io={0}; io.mode=SS_CHECK; io.chunks=chunks; io.nchunks=2;
    visit_all(&io);
    assert(io.bad && "dynamic arena state must require extended RAM before SS_APPLY");
    puts("PASS missing extended RAM rejected before apply");
    uint8_t *extension=calloc(RAM_SIZE-RAM_GAME,1);
    uint64_t identity=1;
    Chunk complete[]={{"mods.arena",sizeof state,(const uint8_t *)&state},{"rt.g_ram",RAM_GAME,g_ram},
                      {"rt.g_ram_ext",RAM_SIZE-RAM_GAME,extension},{"mods.content",8,(const uint8_t*)&identity}};
    io=(StateIO){0}; io.mode=SS_CHECK; io.chunks=complete; io.nchunks=4;
    visit_all(&io); assert(!io.bad);
    puts("PASS matching arena and RAM accepted");
    modmem_stage_acquire(0x800A0CD8u,0,2048);modmem_export(&state);
    uint32_t queue=0x80050000,queue_meta=0x00000003,tail=1;
    memcpy(g_ram+0x62D74,&queue,4);memcpy(g_ram+0x62EFC,&queue_meta,4);memcpy(g_ram+0x62EFE,&tail,2);
    uint32_t command=2,sectors=1,bad_queue_dest=0x807FFFFC;
    memcpy(g_ram+0x50000,&command,4);memcpy(g_ram+0x50004,&sectors,4);memcpy(g_ram+0x5000C,&bad_queue_dest,4);

    uint32_t req[7]={1,0,0x800A0CD8u,0,state.stage_base,0,2048};
    memcpy(g_ram+0xB3E40,req,sizeof req);
    uint32_t sector_end=1;memcpy(g_ram+0xA0CDC,&sector_end,4);
    uint32_t phase=3,type=0;memcpy(g_ram+0xA0CBC,&phase,4);memcpy(g_ram+0xB4DB4,&type,4);
    io.bad=0;visit_all(&io);assert(io.bad && "queued CD READ destination must be owned before it becomes k_readDest");
    memcpy(g_ram+0x5000C,&state.stage_base,4);
    uint32_t invalid_head=4;memcpy(g_ram+0x62F02,&invalid_head,2);io.bad=0;visit_all(&io);assert(io.bad);
    invalid_head=0;memcpy(g_ram+0x62F02,&invalid_head,2);
    uint32_t invalid_ring=0x801FFFF0;memcpy(g_ram+0x62D74,&invalid_ring,4);io.bad=0;visit_all(&io);assert(io.bad);
    memcpy(g_ram+0x62D74,&queue,4);

    for(uint32_t cd=0;cd<=4;cd++) {memcpy(g_ram+0xB3E40,&cd,4);io.bad=0;visit_all(&io);assert(!io.bad);}
    uint32_t idle=0;memcpy(g_ram+0xB3E40,&idle,4);
    for(phase=4;phase<=6;phase++){memcpy(g_ram+0xA0CBC,&phase,4);io.bad=0;visit_all(&io);assert(!io.bad);}
    phase=3;memcpy(g_ram+0xA0CBC,&phase,4);
    uint32_t wrong_offset=2048;memcpy(g_ram+0xB3E54,&wrong_offset,4);
    io.bad=0;visit_all(&io);assert(io.bad && "staged CD sector offset must match its indexed block");
    wrong_offset=0;memcpy(g_ram+0xB3E54,&wrong_offset,4);
    uint32_t bad_dest=state.stage_base+4;memcpy(g_ram+0xB3E50,&bad_dest,4);io.bad=0;visit_all(&io);assert(io.bad);
    memcpy(g_ram+0xB3E50,&state.stage_base,4);
    uint32_t left=1,kdest=0x807FFFFCu;memcpy(g_ram+0x62F18,&left,4);memcpy(g_ram+0x62F1C,&kdest,4);
    io.bad=0;visit_all(&io);assert(io.bad);kdest=state.stage_base;memcpy(g_ram+0x62F1C,&kdest,4);
    io.bad=0;visit_all(&io);assert(!io.bad);
    left=0;memcpy(g_ram+0x62F18,&left,4);memcpy(g_ram+0x62F1C,&left,4);
    state.scratch_floor=state.stage_base+16;io.bad=0;visit_all(&io);assert(io.bad);state.scratch_floor=state.stage_base;
    state.abi=1;io.bad=0;visit_all(&io);assert(io.bad);state.abi=MODMEM_ABI;
    io.mode=SS_APPLY;visit_all(&io);ModMemoryState staged;modmem_export(&staged);assert(!memcmp(&staged,&state,sizeof staged));
    io.mode=SS_CHECK;modmem_stage_release();modmem_export(&state);
    memset(g_ram+0x62D74,0,4);memset(g_ram+0x62EFC,0,8);
    memset(g_ram+0xB3E40,0,sizeof req);
    uint32_t unowned=0x807C0000,busy=3;
    memcpy(g_ram+0xB3E50,&unowned,4);memcpy(g_ram+0xB3E40,&busy,4);
    io.bad=0;visit_all(&io);assert(io.bad && "active extended CD write needs an owned heap or staging range");
    memset(g_ram+0xB3E40,0,sizeof req);

    puts("PASS staging state phases, owned CD destinations, ABI and disjoint ranges validated before apply");

    state.abi++; io.bad=0; visit_all(&io); assert(io.bad); state.abi--;
    state.cursor+=4; io.bad=0; visit_all(&io); assert(io.bad); state.cursor-=4;
    state.scratch_floor=state.base-16; io.bad=0; visit_all(&io); assert(io.bad); state.scratch_floor=state.stage_base?state.stage_base:state.limit;
    state.models=1; state.checkpoints[0]=state.cursor+4; io.bad=0; visit_all(&io); assert(io.bad);
    state.models=0; state.checkpoints[0]=0;
    _putenv_s("RT_MOD_ARENA","0"); io.bad=0; visit_all(&io); assert(io.bad);
    _putenv_s("RT_MOD_ARENA","1");
    puts("PASS incompatible ABI/cursor/scratch/checkpoint/disabled option rejected");
    modmem_clear(); io.mode=SS_APPLY; visit_all(&io); assert(modmem_active());
    ModMemoryState restored; modmem_export(&restored); assert(!memcmp(&restored,&state,sizeof state));
    puts("PASS arena metadata state round trip");
    io=(StateIO){0}; io.mode=SS_CHECK; io.chunks=complete+1; io.nchunks=2;
    visit_all(&io); assert(io.bad);
    puts("PASS stripped arena metadata rejected when dynamic heap pointer remains");
    ModMemoryState before_missing; modmem_export(&before_missing);
    io=(StateIO){0}; io.mode=SS_CHECK;
    visit_all(&io); assert(io.bad && "state without arena and base RAM must not clear live ownership");
    ModMemoryState after_missing; modmem_export(&after_missing);
    assert(!memcmp(&before_missing,&after_missing,sizeof before_missing));
    puts("PASS missing arena and base RAM rejected without changing live ownership");
    io=(StateIO){0}; io.mode=SS_CHECK; io.chunks=complete+1; io.nchunks=2;
    uint32_t original=0x80150000; memcpy(g_ram+0xB4D78,&original,4);
    io.bad=0; visit_all(&io); assert(!io.bad);
    io.mode=SS_APPLY; visit_all(&io); assert(!modmem_active());
    puts("PASS original state clears arena ownership without relocation");
    queue=0x80050000;queue_meta=3;tail=1;bad_queue_dest=0xA07FFFFC;
    memcpy(g_ram+0x62D74,&queue,4);memcpy(g_ram+0x62EFC,&queue_meta,4);memcpy(g_ram+0x62EFE,&tail,2);
    memcpy(g_ram+0x50000,&command,4);memcpy(g_ram+0x50004,&sectors,4);memcpy(g_ram+0x5000C,&bad_queue_dest,4);
    io.mode=SS_CHECK;io.bad=0;visit_all(&io);assert(io.bad && "stripped arena metadata must reject queued extended CD aliases");
    memset(g_ram+0x62D74,0,4);memset(g_ram+0x62EFC,0,8);

    uint32_t cd_dest=0xA07C0000, read_left=1;
    memcpy(g_ram+0xB3E50,&cd_dest,4); memcpy(g_ram+0xB3E40,&read_left,4); io.mode=SS_CHECK; io.bad=0; visit_all(&io);
    assert(io.bad && "stripped metadata must reject extended CD request destination");
    cd_dest=0; memcpy(g_ram+0xB3E50,&cd_dest,4); memcpy(g_ram+0xB3E40,&cd_dest,4); cd_dest=0x807C0000;
    memcpy(g_ram+0x62F1C,&cd_dest,4); memcpy(g_ram+0x62F18,&read_left,4);
    io.bad=0; visit_all(&io); assert(io.bad && "stripped metadata must reject active extended kernel CD destination");
    cd_dest=read_left=0; memcpy(g_ram+0x62F1C,&cd_dest,4); memcpy(g_ram+0x62F18,&read_left,4);
    puts("PASS stripped staging CD roots rejected before apply");

    bitmaps=1;
    Chunk bitmap_chunks[]={{"mods.content",8,(const uint8_t*)&identity},{"rt.g_ram",RAM_GAME,g_ram}};
    io=(StateIO){0}; io.mode=SS_CHECK; io.chunks=bitmap_chunks; io.nchunks=2;
    visit_all(&io); assert(io.bad && "identified bitmap mod state requires display metadata");
    puts("PASS identified bitmap state requires metadata");
    identity=0; io.bad=0; visit_all(&io); assert(!io.bad);
    bitmap_state=(ModPcBitmapState){1,4,2,3,4}; io.mode=SS_APPLY; visit_all(&io);
    assert(bitmap_state.pickup==UINT32_MAX && bitmap_state.caption_block==UINT32_MAX);
    puts("PASS original bitmap-less state resets stale host context");
    free(extension);
    return 0;
}
