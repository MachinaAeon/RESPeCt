/* Save states (see savestate.h).
 *
 * Saving: at a safe point (the end of a poll, on a game thread, not inside
 * an interrupt handler, the BIOS emulation or native decompiled code) every
 * module writes its state. The translated code keeps no state of its own on
 * the host stack between instructions (registers live in g_cpu), so the
 * only thing about "where the game is" that a file can't hold is the chain
 * of host function calls, and that is rebuilt on loading:
 *
 * Loading: the state is read back, then each game thread gets a fresh
 * coroutine that re-enters the translated function containing its saved
 * position (each function has a resume switch for its call return
 * addresses and loop polls, emitted by tools/recomp/recomp.py). When that
 * function returns, R[31] holds its caller's return address: re-enter the
 * caller there, and so on up the chain. */
#include "savestate.h"
#include "render.h"
#include "rt.h"
#include "modmem.h"
#include "modio.h"
#include "modfs_pc_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SS_MAGIC "RESURVSS"
#define SS_VERSION 1

int g_hle_depth, g_native_depth;
volatile int g_ss_pending;
int g_ss_skip;                  /* testing: let this many safe points pass first */
static int pending_load;
static char pending_path[1024];

typedef struct {
    const char *name;
    uint32_t size;
    const uint8_t *data;
} Chunk;

enum { SS_SAVE, SS_CHECK, SS_APPLY };

struct StateIO {
    int mode;
    FILE *f;
    char prefix[32];
    Chunk *chunks;
    int nchunks;
    int bad, missing;
    char err[256];
};

void ss_module(StateIO *io, const char *module) { snprintf(io->prefix, sizeof io->prefix, "%s", module); }

static const Chunk *find_chunk(StateIO *io, const char *full) {
    for (int i = 0; i < io->nchunks; i++)
        if (!strcmp(io->chunks[i].name, full)) return &io->chunks[i];
    return NULL;
}

static void write_chunk(StateIO *io, const char *full, const void *p, uint32_t n) {
    uint16_t len = (uint16_t)strlen(full);
    fwrite(&len, 2, 1, io->f);
    fwrite(full, 1, len, io->f);
    fwrite(&n, 4, 1, io->f);
    if (n) fwrite(p, 1, n, io->f);
}

int ss_saving(const StateIO *io) { return io->mode == SS_SAVE; }

void ss_field(StateIO *io, const char *name, void *p, size_t n) {
    char full[96];
    snprintf(full, sizeof full, "%s.%s", io->prefix, name);
    if (io->mode == SS_SAVE) { write_chunk(io, full, p, (uint32_t)n); return; }
    const Chunk *c = find_chunk(io, full);
    if (!c) {
        if (io->mode == SS_CHECK) { io->missing++; rt_log("savestate: %s not in the file (kept)", full); }
        return;
    }
    if (c->size != n) {
        if (io->mode == SS_CHECK && !io->bad)
            snprintf(io->err, sizeof io->err, "%s is %u bytes in the file, %u in this build", full, c->size, (unsigned)n);
        io->bad = 1;
        return;
    }
    if (io->mode == SS_APPLY) memcpy(p, c->data, n);
}

void ss_buffer(StateIO *io, const char *name, void **p, uint32_t *n, uint32_t *cap, size_t elem) {
    char full[96];
    snprintf(full, sizeof full, "%s.%s", io->prefix, name);
    if (io->mode == SS_SAVE) { write_chunk(io, full, *p, (uint32_t)(*n * elem)); return; }
    const Chunk *c = find_chunk(io, full);
    if (!c) { if (io->mode == SS_CHECK) io->missing++; return; }
    if (c->size % elem) { io->bad = 1; return; }
    if (io->mode != SS_APPLY) return;
    uint32_t count = c->size / (uint32_t)elem;
    if (count > *cap) {
        *p = realloc(*p, (size_t)count * elem);
        *cap = count;
    }
    if (count) memcpy(*p, c->data, (size_t)count * elem);
    *n = count;
}

/* Reject changed/removed mods before any running state is overwritten.
 * Original states (identity zero) keep the existing conditional resource
 * reapplication. Legacy states lack an identity: warn, never claim their
 * compatibility has been established. */
static void mods_state(StateIO *io) {
    extern uint64_t modfs_signature(void);
    uint64_t current = modfs_signature();
    uint8_t identity[8];
    for (int i = 0; i < 8; i++) identity[i] = (uint8_t)(current >> (i * 8));
    if (io->mode == SS_SAVE) { write_chunk(io, "mods.content", identity, sizeof identity); return; }
    if (io->mode != SS_CHECK) return;
    const Chunk *c = find_chunk(io, "mods.content");
    if (!c) { rt_log("savestate: legacy state has no mod identity; compatibility with changed mods is not verified"); return; }
    uint64_t saved = 0;
    if (c->size == 8) for (int i = 0; i < 8; i++) saved |= (uint64_t)c->data[i] << (i * 8);
    if (c->size != 8 || (saved && saved != current)) {
        if (!io->bad) snprintf(io->err, sizeof io->err, "made with other mods: restore them or load a card save");
        io->bad = 1;
    }
}

/* Arena metadata is mandatory whenever a state contains model pointers in
 * extended RAM. Validation finishes before render_sync / SS_APPLY. */
static uint32_t saved_word(const Chunk *ram, uint32_t address) {
    uint32_t value = 0;
    if (ram && ram->size == RAM_GAME && (address & 0x1FFFFFFFu) <= RAM_GAME-4)
        memcpy(&value, ram->data + (address & 0x1FFFFFFFu), 4);
    return value;
}

/* A legacy state can resume an interpreted caller even after that caller
 * became native. The fixed-buffer synchronous model loader must never be
 * re-entered halfway through after enabling larger split resources. */
static int saved_module_matches(const Chunk *ram,const RtModule *m) {
    uint32_t off=m->vram&0x1FFFFFFFu;
    if (!ram || ram->size!=RAM_GAME || off>RAM_GAME || m->sig_len>RAM_GAME-off) return 0;
    const uint8_t *p=ram->data+off;
    if (m->sig) return !memcmp(p,m->sig,m->sig_len);
    uint64_t hash=UINT64_C(14695981039346656037);
    for (uint32_t i=0;i<m->sig_len;i++) hash=(hash^p[i])*UINT64_C(1099511628211);
    return m->sig_len && hash==m->sig_hash;
}
static const RtFunc *saved_frame_function(const Chunk *ram,uint32_t pc) {
    for (int i=0;i<rt_module_count;i++) {
        const RtModule *m=&rt_modules[i];
        if ((strcmp(m->name,"kernel") && strcmp(m->name,"test")) || pc<m->code_lo || pc>=m->code_hi ||
            !saved_module_matches(ram,m)) continue;
        uint32_t lo=0,hi=m->nfuncs;
        while(lo<hi) { uint32_t mid=lo+(hi-lo)/2; if(m->funcs[mid].addr<=pc) lo=mid+1; else hi=mid; }
        if (!lo) return NULL;
        const RtFunc *f=&m->funcs[lo-1];
        if (f->meta && m->rng) {
            const uint32_t *range=m->rng+2*f->meta->r_off;
            for (uint32_t k=0;k<f->meta->r_n;k++)
                if (pc>=range[2*k] && pc<range[2*k+1]) return f;
            return NULL;
        }
        return f;
    }
    return NULL;
}
static int saved_sync_chain(const Chunk *ram,uint32_t pc,const Cpu *cpu) {
    uint32_t sp=cpu->r[29], live_ra=cpu->r[31];
    for (unsigned depth=0;depth<24;depth++) {
        pc&=~3u;
        if (pc>=0x8007428Cu && pc<0x80074AD0u) return 1;
        const RtFunc *f=saved_frame_function(ram,pc);
        if (!f || pc<f->addr || pc-f->addr>65536 || sp<0x80000000u || sp>0x80200000u || (sp&3)) return 0;
        uint32_t frame=0,slot=UINT32_MAX;
        int linear_prefix=1;
        /* Recognise a single fixed SP frame and its actual RA save. Stop on
         * other SP writes; never search arbitrary stack words for a caller. */
        for (uint32_t at=f->addr;at<pc;at+=4) {
            uint32_t w=saved_word(ram,at),op=w>>26,rt=(w>>16)&31,rd=(w>>11)&31;
            if ((w>>16)==0x27BDu) {
                int32_t imm=(int16_t)w;
                if (frame || at-f->addr>=96 || !linear_prefix || imm>=0 || (imm&3)) return 0;
                frame=(uint32_t)-imm;
            } else if ((!op && rd==29 && (w&63)!=8 && (w&63)!=9) ||
                       ((op>=8 && op<=15) && rt==29) || ((op>=32 && op<=38) && rt==29)) return 0;
            if (frame && at-f->addr<96 && (w>>16)==0xAFBFu) slot=w&65535u;
            if (!frame && ((op>=1 && op<=7) || (!op && ((w&63)==8 || (w&63)==9)))) linear_prefix=0;
        }
        uint32_t ra=live_ra;
        if (slot!=UINT32_MAX) {
            if (frame<4 || (slot&3) || slot>frame-4 || (uint64_t)sp+slot+4>0x80200000u) return 0;
            ra=saved_word(ram,sp+slot);
        } else if (depth) return 0; /* caller registers are not reconstructed */
        if (ra<0x80000008u || ra>=0x80200000u || (ra&3) || (uint64_t)sp+frame>0x80200000u) return 0;
        uint32_t call=saved_word(ram,ra-8);
        if ((call>>26)!=3 || ((ra&0xF0000000u)|((call&0x03FFFFFFu)<<2))!=f->addr) return 0;
        sp+=frame;pc=ra;live_ra=0;
    }
    return 0;
}
static int saved_sync_resume(StateIO *io,const Chunk *ram) {
    if (!modmem_enabled() || !modfs_dynamic_models()) return 0;
    int test_loaded=0;
    for(int i=0;i<rt_module_count;i++)
        if (!strcmp(rt_modules[i].name,"test") && saved_module_matches(ram,&rt_modules[i])) test_loaded=1;
    if (!test_loaded) return 0;
    /* Startup can have returned from Sync while still allocating shadows and
     * other work for its original, bound graph. Do not apply such a legacy
     * state into a different owner and fail only on its next allocation.
     * The original flow handler 0 means stage startup (gameflow.c). */
    uint32_t models=saved_word(ram,0x800B4D7C), top=saved_word(ram,0x800B4D78);
    if (!find_chunk(io,"mods.arena") && !saved_word(ram,0x800BB19C) &&
        models && models<=MODMEM_MODELS && top>=0x80150000u && top<0x80200000u) return 1;
    /* A protected read still targeting a PS1 fixed buffer is unsafe even
     * when its enclosing frame uses an unsupported unwind pattern. */
    uint32_t req=saved_word(ram,0x800B3E50),table=saved_word(ram,0x800B3E48),index=saved_word(ram,0x800B3E4C);
    if (saved_word(ram,0x800B3E40) && modmem_split_identity(table,index) &&
        (req==0x80108000u || req==0x800F8000u)) return 1;
    const Chunk *c=find_chunk(io,"rt.g_cpu"),*poll=find_chunk(io,"rt.g_poll_pc");
    Cpu cpu;uint32_t pc;
    if (c && c->size==sizeof cpu && poll && poll->size==4) {
        memcpy(&cpu,c->data,sizeof cpu);memcpy(&pc,poll->data,4);
        if(saved_sync_chain(ram,pc,&cpu)) return 1;
    }
    const Chunk *current=find_chunk(io,"thread.current");
    uint32_t active;
    if (!current || current->size!=4) return 0;
    memcpy(&active,current->data,4);if(active>=8) return 0;
    for(unsigned i=0;i<8;i++) {
        if(i==active) continue;
        char key[40];uint32_t used=0,started=0;
        snprintf(key,sizeof key,"thread.t%u.used",i);c=find_chunk(io,key);if(c && c->size==4) memcpy(&used,c->data,4);
        snprintf(key,sizeof key,"thread.t%u.started",i);c=find_chunk(io,key);if(c && c->size==4) memcpy(&started,c->data,4);
        snprintf(key,sizeof key,"thread.t%u.cpu",i);c=find_chunk(io,key);
        if(used && started && c && c->size==sizeof cpu) {
            memcpy(&cpu,c->data,sizeof cpu);
            if(saved_sync_chain(ram,cpu.r[31],&cpu)) return 1;
        }
    }
    return 0;
}

/* The rooms' window, when a mod moved it to extended RAM (modfs.c): the
 * room's data, not the model arena's. */
extern uint32_t g_room_win_base, g_room_win_end;
static int in_room_window(uint32_t p, uint64_t bytes) {
    return g_room_win_base >= 0x80200000u && p >= g_room_win_base && (uint64_t)p + bytes <= g_room_win_end;
}
static int extended_model_pointer(uint32_t p) { return p >= 0x80200000u && p < 0x80800000u && !in_room_window(p, 0); }
static int saved_arena_range(const ModMemoryState *s,uint32_t address,uint64_t bytes) {
    uint64_t end=(uint64_t)address+bytes;
    return (address>=s->base && end<=s->cursor) ||
        (s->stage_base && address>=s->stage_base && end<=(uint64_t)s->stage_base+s->stage_bytes);
}
/* CD destinations may use any RAM alias. Validate the complete queued span,
 * including READs not yet adopted by the sector callback. */
static int saved_cd_range(const ModMemoryState *s,uint32_t address,uint64_t bytes) {
    uint32_t physical=address&0x1FFFFFFFu;
    if (physical<RAM_GAME) return bytes<=(uint64_t)RAM_GAME-physical;
    if (physical<RAM_SIZE && in_room_window(0x80000000u+physical,bytes)) return 1;
    return s && physical<RAM_SIZE && saved_arena_range(s,0x80000000u+physical,bytes);
}
static int saved_cd_queue_valid(const Chunk *ram,const ModMemoryState *s) {
    uint32_t queue=saved_word(ram,0x80062D74), head=saved_word(ram,0x80062F02)&0xFFFFu;
    uint32_t tail=saved_word(ram,0x80062EFE)&0xFFFFu, count=(saved_word(ram,0x80062EFC)&0xFFFFu)+1;
    if (!queue) return head==0 && tail==0;
    uint32_t physical=queue&0x1FFFFFFFu;
    if ((queue&3) || physical>=RAM_GAME || (uint64_t)count*16>RAM_GAME-physical || head>=count || tail>=count) return 0;
    /* Completed entries are -1; inspect the complete bounded ring so a full
     * queue (head==tail) and commands pending adoption are covered too. */
    for (uint32_t i=0;i<count;i++) {
        uint32_t entry=0x80000000u+physical+i*16;
        if ((saved_word(ram,entry)&0xFFFFu)!=2) continue;
        uint32_t sectors=saved_word(ram,entry+4), dest=saved_word(ram,entry+12);
        if (!sectors || !saved_cd_range(s,dest,(uint64_t)sectors*2048)) return 0;
    }
    return 1;
}
static int saved_dynamic_pointers(const Chunk *ram) {
    if (!saved_cd_queue_valid(ram,NULL)) return 1;
    if (saved_word(ram,0x800B3E40) && !saved_cd_range(NULL,saved_word(ram,0x800B3E50),saved_word(ram,0x800B3E58))) return 1;
    static const uint32_t roots[] = {0x800B4D78,0x800B4DC0,0x800B4DC4,0x800B4DC8,0x800B3E50};
    for (unsigned i=0; i<sizeof roots/sizeof roots[0]; i++)
        if (extended_model_pointer(saved_word(ram,roots[i]))) return 1;
    if (saved_word(ram,0x80062F18) && !saved_cd_range(NULL,saved_word(ram,0x80062F1C),(uint64_t)saved_word(ram,0x80062F18)*2048)) return 1;
    uint32_t models = saved_word(ram,0x800B4D7C);
    if (models > MODMEM_MODELS) models=0; /* The TEST overlay may not be resident in an original boot state. */
    for (uint32_t i=0; i<models; i++)
        for (uint32_t j=4; j<=20; j+=4)
            if (extended_model_pointer(saved_word(ram,0x800B3F10+i*0x1C+j))) return 1;
    uint32_t n=saved_word(ram,0x800AF530), objects=saved_word(ram,0x800AB510);
    if (n > 512) n=0;
    for (uint32_t i=0; i<n; i++) {
        uint64_t e=(uint64_t)objects+i*0x8C4;
        if (e < 0x80000000u || e+0x8C4 > 0x80200000u) break;
        if (extended_model_pointer(saved_word(ram,(uint32_t)e+0x890)) ||
            extended_model_pointer(saved_word(ram,(uint32_t)e+0x16C)) ||
            extended_model_pointer(saved_word(ram,(uint32_t)e+0x8BC))) return 1;
    }
    return 0;
}
static int saved_stage_valid(const ModMemoryState *s,const Chunk *ram) {
    uint32_t cd=saved_word(ram,0x800B3E40), step=saved_word(ram,0x800A0CBC);
    uint32_t req=saved_word(ram,0x800B3E50), bytes=saved_word(ram,0x800B3E58);
    uint32_t left=saved_word(ram,0x80062F18), dest=saved_word(ram,0x80062F1C);
    if (!saved_cd_queue_valid(ram,s)) return 0;
    if (cd && (cd>4 || !saved_cd_range(s,req,bytes))) return 0;
    if (left && !saved_cd_range(s,dest,(uint64_t)left*2048)) return 0;
    if (!s->stage_base) return 1;
    uint32_t type=saved_word(ram,0x800B4DB4);
    uint32_t table=type==0x1E ? 0x800A195Cu : 0x800A0CD8u, index=type==0x1E ? 0x196 : type;
    if (s->owner!=saved_word(ram,0x800BBB88) || cd>4 ||
        saved_word(ram,0x800B3E48)!=s->stage_table || saved_word(ram,0x800B3E4C)!=s->stage_index ||
        req!=s->stage_base || bytes!=s->stage_bytes) return 0;
    uint32_t first=saved_word(ram,s->stage_table+s->stage_index*4);
    uint32_t last=saved_word(ram,s->stage_table+(s->stage_index+1)*4);
    if (first>UINT32_MAX/2048u || last<first || (uint64_t)(last-first)*2048!=bytes ||
        saved_word(ram,0x800B3E54)!=first*2048u) return 0;
    if (s->stage_cancelled) { if (cd) return 0; }
    else if (step<3 || step>6 || (step>3 && cd) || table!=s->stage_table || index!=s->stage_index) return 0;
    if (left && (s->stage_cancelled || cd>=3)) {
        uint64_t end=(uint64_t)dest+(uint64_t)left*2048;
        if (dest<s->stage_base || end>(uint64_t)s->stage_base+s->stage_bytes) return 0;
    }
    return 1;
}
static void arena_state(StateIO *io) {
    ModMemoryState state = {0};
    if (io->mode == SS_SAVE) {
        modmem_export(&state);
        if (state.active) write_chunk(io,"mods.arena",&state,sizeof state);
        return;
    }
    const Chunk *c=find_chunk(io,"mods.arena"), *ram=find_chunk(io,"rt.g_ram");
    if (c && c->size==sizeof state) memcpy(&state,c->data,sizeof state);
    if (io->mode==SS_CHECK) {
        const Chunk *ext=find_chunk(io,"rt.g_ram_ext");
        const Chunk *identity=find_chunk(io,"mods.content");
        uint64_t content=0;
        if (identity && identity->size==8)
            for (unsigned i=0;i<8;i++) content |= (uint64_t)identity->data[i] << (i*8);
        extern uint64_t modfs_signature(void);
        int sync_legacy=saved_sync_resume(io,ram);
        int valid = !sync_legacy && (c ? c->size==sizeof state && state.active==1 && ram && ram->size==RAM_GAME &&
            ext && ext->size==RAM_SIZE-RAM_GAME && content && content==modfs_signature() && modmem_validate(&state,saved_word(ram,0x800B4D78)) && saved_stage_valid(&state,ram) :
            ram && ram->size == RAM_GAME && !saved_dynamic_pointers(ram));
        if (!valid) {
            if (!io->bad) snprintf(io->err,sizeof io->err,sync_legacy ?
                "legacy model initialization cannot resume with dynamic model mods; start a new game or use a gameplay checkpoint" :
                "dynamic model arena metadata/RAM is missing or incompatible; restore its option and mods");
            io->bad=1;
        }
        return;
    }
    modmem_restore(&state); /* Original states reset host ownership, never retain stale arena pointers. */
}
static void pc_bitmap_state(StateIO *io) {
    ModPcBitmapState state = {1,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    if (io->mode==SS_SAVE) {
        if (modfs_pc_has_bitmaps()) {
            modfs_pc_get_state(&state);
            write_chunk(io,"mods.pc_bitmaps",&state,sizeof state);
        }
        return;
    }
    const Chunk *c=find_chunk(io,"mods.pc_bitmaps");
    if (c && c->size==sizeof state) memcpy(&state,c->data,sizeof state);
    if (io->mode==SS_CHECK) {
        /* Missing metadata in an original/legacy state resets visible host
         * context. The native message pointer is re-evaluated at draw time. */
        const Chunk *identity=find_chunk(io,"mods.content");
        int identified=0;
        if (identity && identity->size==8)
            for (unsigned i=0;i<8;i++) identified |= identity->data[i] != 0;
        if ((!c && modfs_pc_has_bitmaps() && identified) ||
            (c && (c->size!=sizeof state || !modfs_pc_has_bitmaps() || !modfs_pc_valid_state(&state)))) {
            if (!io->bad) snprintf(io->err,sizeof io->err,"PC bitmap display state is incompatible with the active bitmap manifest");
            io->bad=1;
        }
        return;
    }
    modfs_pc_set_state(&state);
}


static void pc_diary_saved_state(StateIO *io) {
    ModPcDiaryState state={1,UINT32_MAX,UINT32_MAX};
    if(io->mode==SS_SAVE) {
        if(modfs_pc_has_diaries()){modfs_pc_diary_get_state(&state);write_chunk(io,"mods.pc_diary",&state,sizeof state);}return;
    }
    const Chunk *c=find_chunk(io,"mods.pc_diary"),*ram=find_chunk(io,"rt.g_ram");
    if(c && c->size==sizeof state)memcpy(&state,c->data,sizeof state);
    if(io->mode==SS_CHECK) {
        if((c && (c->size!=sizeof state || !modfs_pc_has_diaries())) ||
           !ram || !modfs_pc_diary_check_state(c?&state:NULL,ram->data,ram->size)) {
            if(!io->bad)snprintf(io->err,sizeof io->err,"PC diary presentation is incompatible; reopen the document from a gameplay checkpoint with matching mods");io->bad=1;
        }return;
    }
    modfs_pc_diary_set_state(&state);
}

/* Native synchronous staging owns C locals until its read/upload returns.
 * Its exact boot layout and mutable extended RAM are mandatory on resume. */
static void staging_state(StateIO *io) {
    ModIoState state={0};modio_state_get(&state);
    if(io->mode==SS_SAVE) {if(modio_active())write_chunk(io,"mods.staging",&state,sizeof state);return;}
    if(io->mode!=SS_CHECK)return;
    const Chunk *c=find_chunk(io,"mods.staging"),*ext=find_chunk(io,"rt.g_ram_ext");
    ModIoState saved={0};if(c && c->size==sizeof saved)memcpy(&saved,c->data,sizeof saved);
    if((c && c->size!=sizeof saved) || !modio_state_valid(c?&saved:NULL,ext?ext->size:0)) {
        if(!io->bad)snprintf(io->err,sizeof io->err,"synchronous image/font staging layout is missing or incompatible; restore its option/mods or start fresh");
        io->bad=1;
    }
}
/* The rooms' memory (modfs.c): a state whose room is where this run
 * doesn't keep it (made before a mod moved it, or with it moved) can't go on. */
static void room_window_state(StateIO *io) {
    if (io->mode != SS_CHECK) return;
    extern int modfs_room_window_state_ok(uint32_t obj_end);
    const Chunk *ram = find_chunk(io, "rt.g_ram");
    int in_game = 0;
    for (int i = 0; i < rt_module_count; i++)
        if (!strcmp(rt_modules[i].name, "test") && saved_module_matches(ram, &rt_modules[i])) in_game = 1;
    if (in_game && !modfs_room_window_state_ok(saved_word(ram, 0x800B3D54u))) {
        if (!io->bad) snprintf(io->err, sizeof io->err, "made before or after a big-room mod: load a memory card save");
        io->bad = 1;
    }
}
static void visit_all(StateIO *io) {
    mods_state(io);
    room_window_state(io);
    staging_state(io);
    arena_state(io);
    pc_bitmap_state(io);
    pc_diary_saved_state(io);
    rt_state(io);
    hw_state(io);
    timers_state(io);
    sio_state(io);
    gpu_state(io);
    gte_state(io);
    spu_state(io);
    cd_state(io);
    mdec_state(io);
    bios_state(io);
    card_state(io);
    thread_state(io);
    decomp_state(io);
    { void texpack_state(StateIO *io); texpack_state(io); }
}

/* ------------------------------------------------------------ requests */

void ss_request_save(const char *path) {
    snprintf(pending_path, sizeof pending_path, "%s", path);
    pending_load = 0;
    g_ss_pending = 1;
}

void ss_request_load(const char *path) {
    snprintf(pending_path, sizeof pending_path, "%s", path);
    pending_load = 1;
    g_ss_pending = 1;
}

static int save_now(const char *path, char *msg, size_t msgn) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    StateIO io = {0};
    io.mode = SS_SAVE;
    io.f = fopen(tmp, "wb");
    if (!io.f) { snprintf(msg, msgn, "Couldn't write %s", path); return 0; }
    uint32_t ver = SS_VERSION;
    fwrite(SS_MAGIC, 1, 8, io.f);
    fwrite(&ver, 4, 1, io.f);
    render_sync();
    visit_all(&io);
    int ok = !ferror(io.f);
    ok &= fclose(io.f) == 0;
    if (!ok) { remove(tmp); snprintf(msg, msgn, "Couldn't write %s", path); return 0; }
    remove(path);
    if (rename(tmp, path) != 0) { snprintf(msg, msgn, "Couldn't write %s", path); return 0; }
    { extern int thread_current(void);
      rt_log("savestate: saved %s at frame %u (resume %08X, thread %d, sp %08X, ra %08X)", path, g_frame, g_poll_pc | 1, thread_current(), R[29], R[31]); }
    snprintf(msg, msgn, "Saved");
    return 1;
}

static uint8_t *read_file(const char *path, long *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n + 1);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

void thread_resume_all(void) __attribute__((noreturn));
void render_reload(void);
void gte_reset_caches(void);
void spu_reset_output(void);
void rt_reset_call_cache(void);
void rt_split_reset(void);

/* Returns only if the file can't be loaded (msg says why). */
static void load_now(const char *path, char *msg, size_t msgn) {
    long n = 0;
    uint8_t *b = read_file(path, &n);
    if (!b) { snprintf(msg, msgn, "No save in this slot"); return; }
    if (n < 12 || memcmp(b, SS_MAGIC, 8)) { free(b); snprintf(msg, msgn, "Not a save file"); return; }
    uint32_t ver;
    memcpy(&ver, b + 8, 4);
    if (ver != SS_VERSION) { free(b); snprintf(msg, msgn, "Save from an incompatible version"); return; }
    /* index the chunks; names are made NUL-terminated copies */
    StateIO io = {0};
    int cap = 64;
    io.chunks = malloc(sizeof(Chunk) * (size_t)cap);
    long p = 12;
    while (p + 6 <= n) {
        uint16_t len;
        uint32_t size;
        memcpy(&len, b + p, 2);
        if (p + 2 + len + 4 > n) break;
        char *name = malloc((size_t)len + 1);
        memcpy(name, b + p + 2, len);
        name[len] = 0;
        memcpy(&size, b + p + 2 + len, 4);
        const uint8_t *data = b + p + 2 + len + 4;
        if (p + 2 + len + 4 + (long)size > n) { free(name); break; }
        if (io.nchunks == cap) io.chunks = realloc(io.chunks, sizeof(Chunk) * (size_t)(cap *= 2));
        io.chunks[io.nchunks++] = (Chunk){name, size, data};
        p += 2 + len + 4 + size;
    }
    io.mode = SS_CHECK;
    visit_all(&io);
    if (io.bad) {
        rt_log("savestate: can't load %s: %s", path, io.err);
        if (io.err[0] && !strstr(io.err, " in this build")) {   /* a reason the player can act on (mods) */
            snprintf(msg, msgn, "%s", io.err);
            if (msg[0] >= 'a' && msg[0] <= 'z') msg[0] = (char)(msg[0] - 'a' + 'A');
        } else snprintf(msg, msgn, "This save doesn't fit this version of the game");
        for (int i = 0; i < io.nchunks; i++) free((void *)io.chunks[i].name);
        free(io.chunks);
        free(b);
        return;
    }
    /* From here on the running game is replaced. */
    render_sync();
    io.mode = SS_APPLY;
    visit_all(&io);
    { void modfs_state_loaded(void); modfs_state_loaded(); }   /* what the mods change, if the save was made without them */
    for (int i = 0; i < io.nchunks; i++) free((void *)io.chunks[i].name);
    free(io.chunks);
    free(b);
    render_reload();
    gte_reset_caches();
    spu_reset_output();
    { void spu_hd_state_loaded(void); spu_hd_state_loaded(); }   /* (a mod's sounds where the save has the originals) */
    { void cd_reapply_volume(void); cd_reapply_volume(); }
    rt_reset_call_cache();
    rt_split_reset();
    { extern int thread_current(void);
      rt_log("savestate: loaded %s, frame %u, continuing at %08X (thread %d, sp %08X, ra %08X)", path, g_frame, g_poll_pc | 1, thread_current(), R[29], R[31]); }
    plat_ss_done(1, 1, "Loaded");
    thread_resume_all();
}

int rt_in_interrupt(void);

void ss_service(void) {
    if (!g_ss_pending) return;
    /* only where the saved position can be continued from */
    if ((g_poll_pc & 3) || rt_in_interrupt() || g_hle_depth || g_native_depth || modmem_sync_busy() || modio_busy()) {
        static uint32_t last;
        if (getenv("RT_SS_DEBUG") && last != g_frame) {
            last = g_frame;
            rt_log("savestate: waiting (poll %08X irq %d hle %d native %d)", g_poll_pc, rt_in_interrupt(), g_hle_depth, g_native_depth);
        }
        return;
    }
    if (g_ss_skip > 0) { g_ss_skip--; return; }
    g_ss_pending = 0;
    char msg[200];
    if (pending_load) {
        load_now(pending_path, msg, sizeof msg);
        plat_ss_done(0, 1, msg);          /* only reached on failure */
    } else {
        int ok = save_now(pending_path, msg, sizeof msg);
        plat_ss_done(ok, 0, msg);
    }
}

/* ------------------------------------------------------------ resuming */

int rt_resume_candidates(uint32_t pc, const RtModule **mod, const RtFunc **out, int max);

static void resume_one(uint32_t pc) {
    const RtFunc *cand[16];
    const RtModule *m = NULL;
    int n = rt_resume_candidates(pc, &m, cand, 16);
    for (int i = 0; i < n; i++) {
        g_resume_pc = pc;
        g_resume_miss = 0;
        interp_resume(m, cand[i]);
        if (!g_resume_miss) return;     /* it continued there and has now returned */
    }
    g_resume_pc = 0;
    rt_fatal("can't continue the saved game at %08X", pc);
}

void ss_resume_chain(uint32_t pc) {
    for (;;) {
        if (!pc) rt_fatal("a resumed game thread returned to nothing");
        if (getenv("RT_SS_DEBUG")) rt_log("savestate: continuing at %08X (sp %08X)", pc, R[29]);
        resume_one(pc);
        pc = R[31];
    }
}
