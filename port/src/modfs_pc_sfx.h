/* The PC version's loose sound effects that are the PS1's own recordings, and
 * the bank sound each one is (written by tools/mod_sfxnames.py from the PC's
 * PSX_DATA and the extracted banks: the same recording, cross-correlation at
 * least 0.94, no other within 0.08, lengths within 0.80-1.25). A replacement
 * goes to that sound and its copies in the other banks. DOG/SND-001 is also
 * proven by its event: PC 0043D630 locomotion -> 00471260(11,1); PS1
 * cerberus_StepSound -> mono1/spatial12/13 -> NOISE23 waveform 2. No sound data. */
static const struct { const char *pc; uint32_t block, vag; } modfs_pc_sfx[] = {
    {"WAV/ENEMY/CROCODILE/SND-000.WAV", 25u, 1u},   /* 0.981 */
    {"WAV/ENEMY/CROCODILE/SND-003.WAV", 25u, 4u},   /* 0.954 */
    {"WAV/ENEMY/CROCODILE/SND-007.WAV", 25u, 1u},   /* 0.984 */
    {"WAV/ENEMY/CROW/SND-004.WAV", 22u, 5u},   /* 0.999 */
    {"WAV/ENEMY/CROW/SND-008.WAV", 22u, 1u},   /* 0.945 */
    {"WAV/ENEMY/DOG/SND-000.WAV", 23u, 1u},   /* 0.972 */
    {"WAV/ENEMY/DOG/SND-001.WAV", 23u, 2u},   /* 0.959 */
    {"WAV/ENEMY/DOG/SND-002.WAV", 23u, 3u},   /* 0.958 */
    {"WAV/ENEMY/DOG/SND-003.WAV", 23u, 4u},   /* 0.947 */
    {"WAV/ENEMY/DOG/SND-004.WAV", 23u, 5u},   /* 0.989 */
    {"WAV/ENEMY/DOG/SND-005.WAV", 23u, 6u},   /* 0.957 */
    {"WAV/ENEMY/DOG/SND-008.WAV", 23u, 9u},   /* 0.977 */
    {"WAV/ENEMY/DOG/SND-009.WAV", 23u, 10u},   /* 0.957 */
    {"WAV/ENEMY/DOG/SND-011.WAV", 23u, 12u},   /* 0.980 */
    {"WAV/ENEMY/DOG/SND-015.WAV", 23u, 9u},   /* 0.992 */
    {"WAV/ENEMY/IVY/SND-005.WAV", 40u, 4u},   /* 0.979 */
    {"WAV/ENEMY/IVY/SND-006.WAV", 40u, 5u},   /* 0.984 */
    {"WAV/ENEMY/IVY/SND-007.WAV", 40u, 6u},   /* 0.940 */
    {"WAV/ENEMY/RICKER/SND-002.WAV", 21u, 3u},   /* 0.969 */
    {"WAV/ENEMY/SPIDER/SND-003.WAV", 37u, 3u},   /* 0.994 */
    {"WAV/ENEMY/ZOMBIE0/SND-000.WAV", 30u, 1u},   /* 0.991 */
    {"WAV/ENEMY/ZOMBIE0/SND-001.WAV", 30u, 2u},   /* 0.984 */
    {"WAV/ENEMY/ZOMBIE0/SND-002.WAV", 30u, 3u},   /* 0.996 */
    {"WAV/ENEMY/ZOMBIE0/SND-003.WAV", 30u, 4u},   /* 0.984 */
    {"WAV/ENEMY/ZOMBIE0/SND-005.WAV", 30u, 1u},   /* 0.993 */
    {"WAV/ENEMY/ZOMBIE0/SND-007.WAV", 30u, 2u},   /* 0.993 */
    {"WAV/ENEMY/ZOMBIE0/SND-009.WAV", 30u, 3u},   /* 0.996 */
    {"WAV/ENEMY/ZOMBIE0/SND-011.WAV", 30u, 4u},   /* 0.984 */
    {"WAV/ENEMY/ZOMBIE1/SND-000.WAV", 33u, 1u},   /* 0.993 */
    {"WAV/ENEMY/ZOMBIE1/SND-001.WAV", 33u, 2u},   /* 0.997 */
    {"WAV/ENEMY/ZOMBIE1/SND-002.WAV", 33u, 3u},   /* 0.996 */
    {"WAV/ENEMY/ZOMBIE1/SND-003.WAV", 33u, 4u},   /* 0.994 */
    {"WAV/ENEMY/ZOMBIE1/SND-005.WAV", 33u, 1u},   /* 0.993 */
    {"WAV/ENEMY/ZOMBIE1/SND-007.WAV", 33u, 2u},   /* 0.993 */
    {"WAV/ENEMY/ZOMBIE1/SND-009.WAV", 33u, 3u},   /* 0.996 */
    {"WAV/ENEMY/ZOMBIE1/SND-011.WAV", 33u, 4u},   /* 0.997 */
    {"WAV/EVENT/080-E0.WAV", 365u, 2u},   /* 0.973 */
    {"WAV/KEY1/KEY-000.WAV", 0u, 1u},   /* 0.942 */
    {"WAV/KEY1/KEY-001.WAV", 0u, 2u},   /* 0.951 */
    {"WAV/KEY1/KEY-002.WAV", 0u, 3u},   /* 0.962 */
    {"WAV/KEY1/KEY-003.WAV", 0u, 4u},   /* 0.972 */
    {"WAV/KEY1/KEY-004.WAV", 0u, 5u},   /* 0.964 */
    {"WAV/KEY1/KEY-008.WAV", 0u, 9u},   /* 0.990 */
    {"WAV/KEY1/KEY-009.WAV", 0u, 11u},   /* 0.962 */
    {"WAV/KEY1/KEY-010.WAV", 0u, 10u},   /* 0.984 */
};
