/* Verified USA ROOM identities, complete native halfword count and CRC32.
 * Metadata only: no message text or image data. PC canvas is 512 x 512.
 * See docs/PC_BITMAPS.md. */
#ifndef MODFS_PC_MESSAGES_H
#define MODFS_PC_MESSAGES_H
#define MODFS_PC_AUTOMATIC 1
typedef struct { const char *pc; uint32_t block, id, words, crc; } PcMessageProfile;
static const PcMessageProfile modfs_pc_messages[] = {
    {"MESSAGE/0002-01.BMP", 0u, 0u, 65u, 0xB96BD672u},
    {"MESSAGE/0003-00.BMP", 4u, 2u, 68u, 0x40A88FCDu},
    {"MESSAGE/0003-02.BMP", 4u, 3u, 31u, 0x5748FB91u},
    {"MESSAGE/0003-03.BMP", 4u, 1u, 29u, 0x4C3E0EEAu},
    {"MESSAGE/0003-04.BMP", 4u, 0u, 46u, 0x4562B35Eu},
    {"MESSAGE/0004-00.BMP", 2u, 2u, 34u, 0xD2B3E39Du},
    {"EVENTMSG/RE-005.BMP", 210u, 8u, 41u, 0xBC0F3D9Fu},
};
#endif
