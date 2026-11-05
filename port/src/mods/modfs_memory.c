#include "modfs_memory.h"
#include "recomp.h"
#include <string.h>
#include <limits.h>

#define MAX_BLOCKS 1024
static const uint32_t tables[] = {0x800A0CD8u, 0x800A195Cu, 0x800A3EDCu};
typedef struct { uint32_t packed, part[3]; } ModelFileSize;
static ModelFileSize sizes[3][MAX_BLOCKS];
static uint32_t counts[3];
static uint32_t u32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int slot(uint32_t table) {
    for (int i = 0; i < 3; i++) if (tables[i] == table) return i;
    return -1;
}
void modfs_memory_reset(void) {
    memset(counts, 0, sizeof counts);
    memset(sizes, 0, sizeof sizes);
}
/* Index the final archive/table only. A failed reindex is atomic; it cannot
 * publish half a table or invalidate an earlier complete index. No reads,
 * decompression, allocation or hardware state changes happen in queries. */
int modfs_memory_index(uint32_t table, const uint8_t *file, size_t bytes,
                       const uint8_t *sectors, size_t table_bytes) {
    int s = slot(table);
    if (s < 0 || !file || !sectors || !bytes || bytes > UINT32_MAX ||
        bytes % 2048 || table_bytes < 8 || u32(sectors) != 0) return 0;
    ModelFileSize next[MAX_BLOCKS];
    uint32_t count = 0, begin = 0, total = (uint32_t)(bytes / 2048);
    for (uint32_t i = 0; i < MAX_BLOCKS && (uint64_t)(i + 2) * 4 <= table_bytes; i++) {
        uint32_t end = u32(sectors + (size_t)(i + 1) * 4);
        if (end < begin || end > total) return 0;
        ModelFileSize m = {(end - begin) * 2048, {0, 0, 0}};
        const uint8_t *b = file + (size_t)begin * 2048;
        if (m.packed >= 16 && u32(b) == 3 && u32(b + 4) == 16) {
            uint32_t off[4] = {16, u32(b + 8), u32(b + 12), m.packed};
            for (int k = 0; k < 3; k++) {
                if (off[k] > m.packed || off[k + 1] < off[k] || off[k + 1] - off[k] < 4) return 0;
                m.part[k] = u32(b + off[k]);
                if (!m.part[k] || m.part[k] > RAM_SIZE - RAM_GAME) return 0;
            }
        }
        next[i] = m;
        begin = end;
        if (end == total) { count = i + 1; break; }
    }
    if (!count) return 0;
    memcpy(sizes[s], next, (size_t)count * sizeof *next);
    counts[s] = count;
    return 1;
}
int modfs_model_request_size(uint32_t table, int index, uint32_t *packed,
                            uint32_t part_sizes[3]) {
    if (packed) *packed = 0;
    if (part_sizes) memset(part_sizes, 0, 3 * sizeof *part_sizes);
    int s = slot(table);
    if (!packed || !part_sizes || s < 0 || index < 0 || (uint32_t)index >= counts[s]) return 0;
    const ModelFileSize *m = &sizes[s][index];
    *packed = m->packed;
    memcpy(part_sizes, m->part, sizeof m->part);
    return 1;
}
