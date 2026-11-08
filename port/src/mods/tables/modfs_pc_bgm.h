/* The PC version's BGM WAVs that are recordings of a room sound set's sequence: PC name
 * (PSX_DATA's), NOISE.BIN block (0xE6 + set), music number; a sequence the disc has in
 * more than one set (the same bytes) has a row for each. alt 1: another PC room's WAV
 * of a sequence a WAV with alt 0 matches better (used when a mod has only this one).
 * Made by tools/mod_tables/mod_bgmnames.py from recordings of every sequence (tools/plugins/seqrec.c). */
static const struct { const char *pc; int block, music, alt; } modfs_pc_bgm[] = {
    {"BGM/000-BGM.WAV", 236, 1, 0},   /* score 0.81 */
    {"BGM/002-BGM1.WAV", 240, 1, 0},   /* score 0.45 */
    {"BGM/002-BGM2.WAV", 240, 2, 0},   /* score 0.50 */
    {"BGM/002-BGM2.WAV", 252, 1, 0},   /* score 0.50 */
    {"BGM/008-BGM-WITH-M.WAV", 230, 2, 0},   /* score 0.83 */
    {"BGM/008-BGM-WITH-M.WAV", 244, 2, 0},   /* score 0.83 */
    {"BGM/008-BGM-WITH-M.WAV", 253, 1, 0},   /* score 0.83 */
    {"BGM/008-BGM.WAV", 244, 1, 0},   /* score 0.68 */
    {"BGM/012-BGM.WAV", 233, 1, 0},   /* score 0.82 */
    {"BGM/015-BGM-WITH-M.WAV", 233, 2, 0},   /* score 0.91 */
    {"BGM/015-BGM-WITH-M.WAV", 254, 1, 0},   /* score 0.91 */
    {"BGM/017-BGM-WITH-M.WAV", 241, 1, 0},   /* score 0.75 */
    {"BGM/017-BGM.WAV", 241, 2, 0},   /* score 0.50 */
    {"BGM/019-BGM.WAV", 257, 1, 0},   /* score 0.51 */
    {"BGM/024-BGM-WITH-M.WAV", 231, 2, 0},   /* score 0.50 */
    {"BGM/024-BGM-WITH-M.WAV", 232, 1, 0},   /* score 0.50 */
    {"BGM/024-BGM.WAV", 231, 1, 0},   /* score 0.78 */
    {"BGM/030-BGM.WAV", 246, 1, 0},   /* score 0.61 */
    {"BGM/039-BGM.WAV", 237, 1, 0},   /* score 0.61 */
    {"BGM/044-BGM.WAV", 241, 2, 1},   /* score 0.50 */
    {"BGM/046-BGM.WAV", 255, 1, 0},   /* score 0.65 */
    {"BGM/048-BGM.WAV", 250, 1, 0},   /* score 0.65 */
    {"BGM/058-BGM-WITH-M1.WAV", 250, 2, 0},   /* score 0.69 */
    {"BGM/058-BGM-WITH-M2.WAV", 250, 2, 1},   /* score 0.63 */
    {"BGM/060-BGM.WAV", 243, 1, 0},   /* score 0.83 */
    {"BGM/066-BGM.WAV", 251, 1, 0},   /* score 0.63 */
    {"BGM/071-BGM-WITH-M.WAV", 249, 2, 0},   /* score 0.90 */
    {"BGM/071-BGM.WAV", 249, 1, 0},   /* score 0.95 */
    {"BGM/074-BGM.WAV", 245, 1, 0},   /* score 0.76 */
    {"BGM/077-BGM-WITH-M.WAV", 245, 2, 0},   /* score 0.75 */
    {"BGM/083-BGM.WAV", 259, 1, 0},   /* score 0.51 */
    {"BGM/087-BGM.WAV", 234, 1, 0},   /* score 0.63 */
    {"BGM/095-BGM.WAV", 260, 1, 0},   /* score 0.84 */
    {"BGM/102-BGM.WAV", 247, 1, 0},   /* score 0.79 */
    {"BGM/106-BGM.WAV", 247, 2, 0},   /* score 0.48 */
    {"BGM/106-BGM.WAV", 248, 1, 0},   /* score 0.48 */
    {"BGM/112-BGM.WAV", 264, 1, 0},   /* score 0.40 */
    {"BGM/114-BGM.WAV", 265, 1, 0},   /* score 0.64 */
    {"BGM/115-BGM.WAV", 261, 1, 0},   /* score 0.71 */
    {"BGM/120-BGM.WAV", 262, 1, 0},   /* score 0.79 */
    {"BGM/121-BGM.WAV", 263, 1, 0},   /* score 0.61 */
    {"BGM/121-REAL-MOVIE.WAV", 263, 2, 0},   /* score 0.64 */
};
