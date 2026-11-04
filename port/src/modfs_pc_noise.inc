/* PC NOISE retains ordered banks but is not the PS1 archive wholesale.
 * Preflight every range before writing any; unchanged source banks retain
 * the native bytes, including the known different reference bank. */
static uint32_t pc_noise_header_crc(const uint8_t *data, const Bank *b) {
    uint32_t crc = (uint32_t)mz_crc32(0, data + b->at, b->vh);
    return (uint32_t)mz_crc32(crc, data + b->at + b->vh, 24);
}

static const char *pc_noise_replace(uint8_t *native, size_t native_n,
        const uint8_t *pc, size_t pc_n, const PcNoisePart *parts,
        size_t count, size_t expected_pc, unsigned *changed) {
    *changed = 0;
    if (!native || !pc || !count || count > 1024 || pc_n != expected_pc)
        return "unsupported PC noise archive size or profile";
    uint8_t *copy = calloc(count, 1);
    if (!copy) return "couldn't allocate PC noise preflight";
    uint64_t cursor = 0, native_end = 0;
    const char *why = NULL;
    for (size_t i = 0; i < count && !why; i++) {
        const PcNoisePart *p = &parts[i];
        if (!p->size || p->pc_offset != cursor ||
            (uint64_t)p->pc_offset + p->size > pc_n || p->native_offset < native_end ||
            (uint64_t)p->native_offset + p->size > native_n) {
            why = "invalid ordered PC noise profile";
            break;
        }
        cursor = (uint64_t)p->pc_offset + p->size;
        native_end = (uint64_t)p->native_offset + p->size;
        if ((uint32_t)mz_crc32(0, pc + p->pc_offset, p->size) == p->crc) continue;
        if (!p->writable) {
            why = "modified PC noise block has no verified native conversion";
            break;
        }
        Bank source, target;
        if (!bank_at(pc, p->pc_offset, p->size, &source) ||
            !bank_at(native, p->native_offset, p->size, &target) ||
            source.ps != p->programs || source.vs != p->waves || source.body != p->body ||
            target.ps != p->programs || target.vs != p->waves || target.body != p->body ||
            pc_noise_header_crc(pc, &source) != p->header_crc ||
            pc_noise_header_crc(native, &target) != p->header_crc) {
            why = "PC noise bank part layout/program/waveform counts changed";
            break;
        }
        if (vag_size(pc, &source, 0) || vag_size(native, &target, 0)) {
            why = "PC noise reserved waveform size changed";
            break;
        }
        uint64_t bytes = 0;
        for (uint32_t j = 0; j <= source.vs; j++) bytes += vag_size(pc, &source, j);
        if (bytes > p->spu_bytes || (uint64_t)source.body + bytes > p->size) {
            why = "PC noise bank exceeds its original SPU allocation";
            break;
        }
        copy[i] = 1;
    }
    if (!why && cursor != pc_n) why = "incomplete ordered PC noise profile";
    if (!why) for (size_t i = 0; i < count; i++) if (copy[i]) {
        memcpy(native + parts[i].native_offset, pc + parts[i].pc_offset, parts[i].size);
        (*changed)++;
    }
    free(copy);
    return why;
}

static int pc_noise_apply(const char *mod, const char *rel, const char *up, const char *path) {
    if (strcmp(up, "NOISE/NOISE.BIN")) return 0;
    long n = 0;
    uint8_t *data = load(path, &n);
    Touched *t = data ? touch(disc_index("NOISE.BIN")) : NULL;
    const char *why = !t ? "couldn't read PC/native noise archive" :
        (uint64_t)t->nsec * 2048 != MODFS_PC_NOISE_NATIVE_SIZE ? "unsupported native noise archive size" : NULL;
    unsigned changed = 0;
    if (!why) why = pc_noise_replace(t->data, (size_t)t->nsec * 2048, data, (size_t)n,
        modfs_pc_noise, sizeof modfs_pc_noise / sizeof modfs_pc_noise[0], MODFS_PC_NOISE_SIZE, &changed);
    if (why) note("mods: %s/%s: %s: not used", mod, rel, why);
    else {
        nused++;
        note("mods: %s/%s: %u changed PC banks into original PS1 locations; unchanged banks retained", mod, rel, changed);
    }
    free(data);
    return 1;
}
