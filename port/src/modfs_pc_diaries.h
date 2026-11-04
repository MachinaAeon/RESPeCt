/* PC document identities/counts verified in the owned original consumers.
 * Metadata only; no diary text or artwork. PC suffixes are hexadecimal.
 * PC 00587E40/00589030 and table005E9E48; see PC_DIARIES.md. */
#ifndef MODFS_PC_DIARIES_H
#define MODFS_PC_DIARIES_H
#include <stdint.h>
typedef struct { unsigned pc, ps1, foregrounds, backgrounds; } PcDiaryProfile;
static const PcDiaryProfile modfs_pc_diaries[] = {
    {0u, 1u, 3u, 0u},
    {1u, 0u, 4u, 0u},
    {2u, 2u, 5u, 1u},
    {3u, 4u, 5u, 1u},
    {4u, 7u, 5u, 1u},
    {5u, 8u, 3u, 1u},
    {6u, 5u, 8u, 1u},
    {7u, 9u, 6u, 1u},
    {8u, 3u, 9u, 1u},
    {9u, 10u, 4u, 1u},
    {10u, 11u, 4u, 1u},
    {11u, 6u, 3u, 1u},
    {12u, 12u, 6u, 3u},
};
#endif
