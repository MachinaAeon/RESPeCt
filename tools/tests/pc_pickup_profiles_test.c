/* Asset-free automatic pickup tests using the existing production harness. */
#define main bitmap_existing_main
#include "pc_bitmap_test.c"
#undef main
#include "modfs_pc_pickups.h"

static const uint32_t pickup_root = 0x800A88C8u;
static PcBitmap *pickup_setup(unsigned id, int width, int blank) {
    const PcPickupProfile *profile = NULL;
    for (size_t i = 0; i < sizeof modfs_pc_pickups / sizeof modfs_pc_pickups[0]; i++)
        if (modfs_pc_pickups[i].id == id) profile = &modfs_pc_pickups[i];
    check(profile != NULL, "test uses a verified pickup identity");
    mkdir("fixture"); bitmap("fixture/image.bmp",4,4,1); mkdir("fixture/PSX_DATA"); mkdir("fixture/PSX_DATA/item"); mkdir("fixture/PSX_DATA/item/PIC");
    char path[128]; snprintf(path, sizeof path, "fixture/PSX_DATA/%s", profile->pc);
    bitmap(path, width, width, blank ? 2 : 1);
    pc_bitmaps_load("test", "fixture");
    if (width != 512 || blank) {
        check(!nused && !modfs_pc_has_bitmaps(), "invalid pickup canvas/black artwork retains native text");
        return NULL;
    }
    check(nused == 1 && npc_bitmaps == 1, "verified pickup path loads automatically without a manifest");
    check(pc_bitmaps_source("test", path + 8), "automatic pickup source excluded from ordinary dispatch");
    PcBitmap *p = &pc_bitmaps[0];
    check(p->kind == 1 && p->id == id && !p->block && !p->begin && p->end == profile->words &&
        p->native_words == profile->words && p->native_crc == profile->crc,
        "production uses the complete measured pickup metadata");
    check(p->x == 0 && p->y == 0 && p->w == 320 && p->h == 240 && p->iw == 512 && p->ih == 512,
        "full PC canvas retains the verified presentation policy");
    n_msg_InitSprites(); R[29] = 0x801f0000;
    for (uint32_t i = 0; i < p->native_words; i++) wr16(ram_ptr(pickup_root + 2*i), 29);
    wr16(ram_ptr(pickup_root + 2*15), 0xfffe);
    wr16(ram_ptr(pickup_root + 2*16), 0xfc01);
    wr16(ram_ptr(pickup_root + 2*(p->native_words-4)), 0xfc00);
    wr16(ram_ptr(pickup_root + 2*(p->native_words-3)), 0xfe10);
    wr16(ram_ptr(pickup_root + 2*(p->native_words-2)), 0xff01);
    wr16(ram_ptr(pickup_root + 2*(p->native_words-1)), 0xfe00);
    wr32(ram_ptr(0x800BA8E8), pickup_root); wr16(ram_ptr(0x800BA8E8+4), (uint16_t)-56);
    wr16(ram_ptr(0x800BA8E8+6), 48); wr16(ram_ptr(0x800BA8E8+10), 1); wr32(ram_ptr(0x800BA9F4), 2);
    modfs_pc_pickup(id); modfs_pc_entry(pickup_root);
    check(!modfs_pc_message(pickup_root, 100, 160, 8, 14), "different original pickup stream does not inherit a reference picture");
    /* Generated glyphs, never copyrighted original text. Override the test's
     * loaded guard only after proving reference mismatch. */
    p->native_crc = (uint32_t)mz_crc32(0, ram_ptr(pickup_root), p->native_words*2);
    check(modfs_pc_message(pickup_root, 100, 160, 8, 14), "matching pickup scratch stream passes the active CRC guard");
    return p;
}
int main(int argc, char **argv) {
    check(argc == 3, "scenario and item ID required"); unsigned id = (unsigned)strtoul(argv[2], NULL, 10);
    if (!strcmp(argv[1], "unverified")) {
        mkdir("fixture"); mkdir("fixture/item"); mkdir("fixture/item/PIC");
        static const unsigned excluded[] = {6u, 7u, 9u, 10u, 16u, 17u, 18u, 19u, 35u, 40u, 42u, 49u, 50u, 51u, 70u};
        for (size_t i=0;i<sizeof excluded/sizeof excluded[0];i++) {
            char path[96]; snprintf(path,sizeof path,"fixture/item/PIC/%02X.BMP",excluded[i]);
            bitmap(path,512,512,1);
        }
        pc_bitmaps_load("test","fixture");
        check(!nused && !modfs_pc_has_bitmaps(), "all fifteen placeholder/empty-identity/mismatched pictures remain unsupported");
        puts("PASS"); return 0;
    }
    if (!strcmp(argv[1], "invalid")) { pickup_setup(id, 4, 0); puts("PASS"); return 0; }
    if (!strcmp(argv[1], "empty")) { pickup_setup(id, 512, 1); puts("PASS"); return 0; }
    PcBitmap *p = pickup_setup(id, 512, 0);
    if (!strcmp(argv[1], "guards")) {
        modfs_pc_pickup(2); check(!modfs_pc_message(pickup_root,100,160,8,14), "other item in the shared scratch buffer falls back");
        modfs_pc_pickup(id); modfs_pc_entry(pickup_root);
        wr16(ram_ptr(pickup_root), 30); check(!modfs_pc_message(pickup_root,100,160,8,14), "changed prefix/name cannot use measured original ranges"); wr16(ram_ptr(pickup_root),29);
        check(!modfs_pc_message(pickup_root+2,100,160,8,14), "a later page cannot reveal the complete pickup picture");
        wr16(ram_ptr(0x800BA8E8+10),0); check(!modfs_pc_message(pickup_root,100,160,8,14), "actual one-line box cannot use the two-line reference"); wr16(ram_ptr(0x800BA8E8+10),1);
        wr32(ram_ptr(0x800BA9C4),1); check(!modfs_pc_message(pickup_root,100,160,8,14), "choice keeps native glyphs"); wr32(ram_ptr(0x800BA9C4),0);
        modfs_pc_entry(pickup_root); check(!modfs_pc_message(pickup_root,100,160,8,14), "door reuse cannot inherit pickup ID");
    } else if (!strcmp(argv[1], "wait")) {
        n_msg_Update(); check(rd16(ram_ptr(0x800BA9D0))==16 && rd16(ram_ptr(0x800BA9FC))==4 && !sorted, "native pickup wait starts without sorting replaced glyphs");
        for (int i=0; i<16; i++) n_msg_Update();
        check(rd16(ram_ptr(0x800BA9FC))==104, "native wait completes before its own continuation"); n_msg_Update();
        uint32_t after=pickup_root+2*(p->native_words-2);
        check(rd16(ram_ptr(0x800BA9FC))==7 && rd16(ram_ptr(after-2))==0xff01 &&
            rd16(ram_ptr(0x800BA9D4))==0xfe10 && rd32(ram_ptr(0x800BA9D8))==after,
            "original layout performs the measured FE10 to FF01 substitution");
        check((uint32_t)mz_crc32(0,ram_ptr(pickup_root),p->native_words*2)!=p->native_crc, "native mutation really changes raw CRC");
        uint32_t before=(uint32_t)mz_crc32(0,g_ram,RAM_SIZE);
        check(modfs_pc_message(pickup_root,100,160,8,14), "same complete picture survives the native prompt mutation");
        check(before==(uint32_t)mz_crc32(0,g_ram,RAM_SIZE), "CRC normalization never writes game RAM");
        wr16(ram_ptr(0x800BA9D4),0xfe11); check(!modfs_pc_message(pickup_root,100,160,8,14), "wrong saved wait does not bypass the CRC"); wr16(ram_ptr(0x800BA9D4),0xfe10);
        wr32(ram_ptr(0x800BA9D8),after+2); check(!modfs_pc_message(pickup_root,100,160,8,14), "wrong saved address does not bypass the CRC"); wr32(ram_ptr(0x800BA9D8),after);
        wr16(ram_ptr(pickup_root),30); check(!modfs_pc_message(pickup_root,100,160,8,14), "other changed text still fails during the native prompt"); wr16(ram_ptr(pickup_root),29);
        wr16(ram_ptr(0x800BA9FC),107); n_msg_Update();
        check(rd16(ram_ptr(after-2))==0xfe10 && !rd16(ram_ptr(0x800BA9D4)) && !rd32(ram_ptr(0x800BA9D8)), "native confirmation restores the original wait halfword");
        check(!rd32(ram_ptr(0x800BA8E8)) && !modfs_pc_message(pickup_root,100,160,8,14), "native end closes the pickup bitmap");
    } else if (!strcmp(argv[1], "priority")) {
        manifest("PICKUP 1 0 31 image.bmp 0 0 4 4 10 20 8 8 alpha\n");
        check(pc_bitmap_find(pickup_root)->native_words==0, "authored pickup row wins over automatic profile");
    } else if (strcmp(argv[1], "profile")) return 2;
    puts("PASS"); return 0;
}
