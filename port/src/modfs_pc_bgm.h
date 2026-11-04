/* The PC version's BGM WAVs that are recordings of a room sound set's sequence: PC name
 * (PSX_DATA's), NOISE.BIN block (0xE6 + set), music number. Made by tools/mod_bgmnames.py
 * from recordings of every sequence (tools/plugins/seqrec.c). */
static const struct { const char *pc; int block, music; } modfs_pc_bgm[] = {
    {"BGM/002-BGM1.WAV", 244, 1},   /* score 0.53 */
    {"BGM/002-BGM2.WAV", 240, 1},   /* score 0.68 */
    {"BGM/008-BGM-WITH-M.WAV", 230, 2},   /* score 0.77 */
    {"BGM/008-BGM.WAV", 244, 1},   /* score 0.57 */
    {"BGM/012-BGM.WAV", 233, 1},   /* score 0.91 */
    {"BGM/015-BGM-WITH-M.WAV", 233, 2},   /* score 0.63 */
    {"BGM/017-BGM-WITH-M.WAV", 241, 1},   /* score 0.60 */
    {"BGM/019-BGM.WAV", 257, 1},   /* score 0.60 */
    {"BGM/024-BGM-WITH-M.WAV", 231, 2},   /* score 0.63 */
    {"BGM/024-BGM.WAV", 231, 1},   /* score 0.40 */
    {"BGM/058-BGM-WITH-M1.WAV", 250, 2},   /* score 0.89 */
    {"BGM/058-BGM-WITH-M2.WAV", 258, 2},   /* score 0.80 */
    {"BGM/071-BGM-WITH-M.WAV", 249, 2},   /* score 0.52 */
    {"BGM/071-BGM.WAV", 249, 1},   /* score 0.80 */
    {"BGM/074-BGM.WAV", 245, 1},   /* score 0.77 */
    {"BGM/087-BGM.WAV", 234, 1},   /* score 0.87 */
    {"BGM/095-BGM.WAV", 260, 1},   /* score 0.51 */
    {"BGM/102-BGM.WAV", 247, 1},   /* score 0.63 */
};
