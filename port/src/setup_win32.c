/* First-run setup on Windows: install the game from the player's disc image.
 *
 * The player puts their copy of the game next to the exe (or in a folder
 * there): a .bin/.cue pair, a .chd, a .ccd/.img, or the .zip those came
 * in (unpacked for the moment into a "disc" folder). Each one found is
 * checked (src/discimg.c: it must be the USA release, an exact copy); the
 * first good one is copied to data\disc.bin, which the game plays from from
 * then on, so the player's own image can be deleted. If none is good, the
 * player is told what was found and why it can't be used, and can look
 * again or pick the file. */
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the same configuration as libchdr's copy of miniz (third_party/chdr_all.c) */
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_TIME
#include "miniz.h"

int disc_image_open(const char *path, char *err, size_t n);
int disc_verify(char *err, size_t n);
int cd_read_sector_raw(uint32_t lba, uint8_t out[2352]);
uint32_t disc_expected_sectors(void);

#define TITLE "RESPeCt"

typedef struct {
    char path[MAX_PATH];
    int rank;                       /* try order: lower first */
} Cand;

static Cand cands[64];
static int ncands;

static const char *ext_of(const char *p) {
    const char *dot = strrchr(p, '.'), *sl = strrchr(p, '\\');
    return dot && (!sl || dot > sl) ? dot : "";
}

/* 0 cue, 1 chd, 2 ccd, 3 bin/img, 4 zip, 9 known but unusable, -1 other */
static int rank_of(const char *name, unsigned long long size) {
    const char *e = ext_of(name);
    if (!_stricmp(e, ".cue")) return 0;
    if (!_stricmp(e, ".chd")) return 1;
    if (!_stricmp(e, ".ccd")) return 2;
    if (!_stricmp(e, ".bin") || !_stricmp(e, ".img")) return size >= 100ull << 20 ? 3 : -1;
    if (!_stricmp(e, ".zip")) return 4;
    if (!_stricmp(e, ".iso") || !_stricmp(e, ".pbp") || !_stricmp(e, ".ecm") || !_stricmp(e, ".7z") ||
        !_stricmp(e, ".rar") || !_stricmp(e, ".mdf") || !_stricmp(e, ".nrg") || !_stricmp(e, ".cso"))
        return 9;
    return -1;
}

static void scan(const char *dir, int depth) {
    char pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof pat, "%s*", dir);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char full[MAX_PATH];
        snprintf(full, sizeof full, "%s%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 2 && _stricmp(fd.cFileName, "saves") && !(depth == 0 && !_stricmp(fd.cFileName, "data"))) {
                char sub[MAX_PATH];
                snprintf(sub, sizeof sub, "%s\\", full);
                scan(sub, depth + 1);
            }
            continue;
        }
        unsigned long long size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        int r = rank_of(fd.cFileName, size);
        if (r < 0 || ncands >= 64) continue;
        snprintf(cands[ncands].path, MAX_PATH, "%s", full);
        cands[ncands].rank = r * 4 + depth;
        ncands++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static int cand_cmp(const void *a, const void *b) {
    return ((const Cand *)a)->rank - ((const Cand *)b)->rank;
}

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '\\');
    return s ? s + 1 : p;
}

static int check(const char *path, char *why, size_t n) {
    return disc_image_open(path, why, n) && disc_verify(why, n);
}

/* ------------------------------------------------------------ unzipping */

static HWND progress_wnd, progress_text;
static char extracted[8][MAX_PATH];          /* unpacked from a .zip: removed once installed */
static int nextracted;

static void progress_open(HWND owner, const char *msg) {
    progress_wnd = CreateWindowExA(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, "STATIC", TITLE,
                                   WS_POPUP | WS_CAPTION | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 460, 110,
                                   owner, NULL, GetModuleHandleA(NULL), NULL);
    RECT r, s;
    GetWindowRect(progress_wnd, &r);
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &s, 0);
    SetWindowPos(progress_wnd, NULL, (s.left + s.right - (r.right - r.left)) / 2,
                 (s.top + s.bottom - (r.bottom - r.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    progress_text = CreateWindowExA(0, "STATIC", msg, WS_CHILD | WS_VISIBLE, 16, 20, 420, 40, progress_wnd, NULL,
                                    GetModuleHandleA(NULL), NULL);
    SendMessageA(progress_text, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    UpdateWindow(progress_wnd);
}

static void progress_set(const char *msg) {
    if (!progress_text) return;
    SetWindowTextA(progress_text, msg);
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
}

static void progress_close(void) {
    if (progress_wnd) DestroyWindow(progress_wnd);
    progress_wnd = progress_text = NULL;
}

typedef struct {
    FILE *f;
    unsigned long long done, total;
    int last_pct;
    const char *name;
} Unzip;

static size_t unzip_write(void *opaque, mz_uint64 ofs, const void *buf, size_t n) {
    Unzip *u = opaque;
    (void)ofs;
    if (fwrite(buf, 1, n, u->f) != n) return 0;
    u->done += n;
    int pct = u->total ? (int)(u->done * 100 / u->total) : 0;
    if (pct != u->last_pct) {
        u->last_pct = pct;
        char msg[MAX_PATH + 64];
        snprintf(msg, sizeof msg, "Unpacking your disc image (one time only)...\n%s  %d%%", u->name, pct);
        progress_set(msg);
    }
    return n;
}

/* Unpack the disc image files of a .zip into dir\disc\. 1: done, 0: failed
 * (why says why), -1: there's no disc image in it. */
static int unzip_disc(HWND owner, const char *zip, const char *dir, char *why, size_t n) {
    mz_zip_archive za;
    memset(&za, 0, sizeof za);
    if (!mz_zip_reader_init_file(&za, zip, 0)) { snprintf(why, n, "it couldn't be opened as a .zip"); return 0; }
    char out_dir[MAX_PATH];
    snprintf(out_dir, sizeof out_dir, "%sdisc", dir);
    int got = 0, ok = 1;
    unsigned count = mz_zip_reader_get_num_files(&za);
    for (unsigned i = 0; i < count && ok; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&za, i, &st) || st.m_is_directory) continue;
        const char *name = st.m_filename, *s = strrchr(name, '/');
        if (s) name = s + 1;
        int r = rank_of(name, st.m_uncomp_size);
        if (r < 0 || r >= 4) continue;
        CreateDirectoryA(out_dir, NULL);
        char out[MAX_PATH];
        snprintf(out, sizeof out, "%s\\%s", out_dir, name);
        Unzip u = {fopen(out, "wb"), 0, st.m_uncomp_size, -1, name};
        if (!u.f) { snprintf(why, n, "%s couldn't be written next to the game", name); ok = 0; break; }
        if (!progress_wnd) progress_open(owner, "Unpacking your disc image (one time only)...");
        ok = mz_zip_reader_extract_to_callback(&za, i, unzip_write, &u, 0);
        ok &= fclose(u.f) == 0;
        if (!ok) { remove(out); snprintf(why, n, "unpacking %s failed (is the disk full?)", name); break; }
        got = 1;
        if (nextracted < 8) snprintf(extracted[nextracted++], MAX_PATH, "%s", out);
    }
    mz_zip_reader_end(&za);
    progress_close();
    if (ok && !got) return -1;                   /* not a disc image's zip: ignore it */
    return ok;
}

/* ------------------------------------------------------------ finding */

static int search(HWND owner, const char *dir, char *path, size_t n, char *found, size_t fn, char *why, size_t wn) {
    ncands = 0;
    scan(dir, 0);
    qsort(cands, (size_t)ncands, sizeof cands[0], cand_cmp);
    found[0] = why[0] = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < ncands; i++) {
            char w[512];
            const Cand *c = &cands[i];
            int r = c->rank / 4;
            if (r == 9) {
                if (!found[0]) {
                    snprintf(found, fn, "%s", base_name(c->path));
                    const char *e = ext_of(c->path);
                    if (!_stricmp(e, ".iso"))
                        snprintf(why, wn, "ISO images leave out the game's music and voices. A .bin/.cue or .chd copy of the disc is needed");
                    else if (!_stricmp(e, ".7z") || !_stricmp(e, ".rar"))
                        snprintf(why, wn, "this kind of archive can't be unpacked here. Please extract it into this folder first (or use a .zip)");
                    else
                        snprintf(why, wn, "this format can't be read. A .bin/.cue or .chd copy of the disc is needed");
                }
                continue;
            }
            if (r == 4) {                          /* a .zip: unpack, then look again */
                static char unpacked[8][MAX_PATH];
                static int nunpacked;
                int done = 0;
                for (int k = 0; k < nunpacked; k++) done |= !_stricmp(unpacked[k], c->path);
                if (pass || done) continue;
                if (nunpacked < 8) snprintf(unpacked[nunpacked++], MAX_PATH, "%s", c->path);
                int z = unzip_disc(owner, c->path, dir, w, sizeof w);
                if (z > 0) return search(owner, dir, path, n, found, fn, why, wn);
                if (z == 0 && !found[0]) { snprintf(found, fn, "%s", base_name(c->path)); snprintf(why, wn, "%s", w); }
                continue;
            }
            if (pass) continue;
            if (check(c->path, w, sizeof w)) {
                snprintf(path, n, "%s", c->path);
                return 1;
            }
            if (!found[0]) {                       /* images come first: theirs is the reason given */
                snprintf(found, fn, "%s", base_name(c->path));
                snprintf(why, wn, "%s", w);
            }
        }
    }
    return 0;
}

static int pick_file(HWND owner, char *path, size_t n) {
    OPENFILENAMEA of = {0};
    path[0] = 0;
    of.lStructSize = sizeof of;
    of.hwndOwner = owner;
    of.lpstrFilter = "Disc images (*.cue, *.bin, *.chd, *.img, *.ccd, *.zip)\0*.cue;*.bin;*.chd;*.img;*.ccd;*.zip\0All files\0*.*\0";
    of.lpstrFile = path;
    of.nMaxFile = (DWORD)n;
    of.lpstrTitle = "Choose your Resident Evil Survivor (USA) disc image";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameA(&of);
}

/* Copy the open (checked) disc image to data\disc.bin. */
static int install(HWND owner, const char *dir, char *out, size_t n, char *why, size_t wn) {
    char ddir[MAX_PATH], tmp[MAX_PATH], fin[MAX_PATH];
    snprintf(ddir, sizeof ddir, "%sdata", dir);
    snprintf(tmp, sizeof tmp, "%s\\disc.bin.part", ddir);
    snprintf(fin, sizeof fin, "%s\\disc.bin", ddir);
    CreateDirectoryA(ddir, NULL);
    uint32_t total = disc_expected_sectors();
    ULARGE_INTEGER avail;
    if (GetDiskFreeSpaceExA(ddir, &avail, NULL, NULL) && avail.QuadPart < (unsigned long long)total * 2352 + (16u << 20)) {
        snprintf(why, wn, "there isn't enough free disk space for the game's data (%u MB needed)",
                 (unsigned)((unsigned long long)total * 2352 >> 20) + 16);
        return 0;
    }
    FILE *f = fopen(tmp, "wb");
    if (!f) { snprintf(why, wn, "the data folder couldn't be written (%s)", ddir); return 0; }
    progress_open(owner, "Installing the game's data (one time only)...");
    uint8_t s[2352];
    int ok = 1, last = -1;
    for (uint32_t lba = 0; lba < total && ok; lba++) {
        ok = cd_read_sector_raw(lba, s) && fwrite(s, 1, 2352, f) == 2352;
        int pct = (int)((unsigned long long)lba * 100 / total);
        if (pct != last) {
            last = pct;
            char msg[128];
            snprintf(msg, sizeof msg, "Installing the game's data (one time only)...\n%d%%", pct);
            progress_set(msg);
        }
    }
    ok &= fclose(f) == 0;
    progress_close();
    if (!ok) { DeleteFileA(tmp); snprintf(why, wn, "copying the disc image failed (is the disk full?)"); return 0; }
    if (!MoveFileExA(tmp, fin, MOVEFILE_REPLACE_EXISTING)) { DeleteFileA(tmp); snprintf(why, wn, "the data folder couldn't be written"); return 0; }
    if (!check(fin, why, wn)) { DeleteFileA(fin); return 0; }
    snprintf(out, n, "%s", fin);
    return 1;
}

static void remove_extracted(const char *dir) {
    for (int i = 0; i < nextracted; i++) DeleteFileA(extracted[i]);
    if (nextracted) {
        char d[MAX_PATH];
        snprintf(d, sizeof d, "%sdisc", dir);
        RemoveDirectoryA(d);                     /* only if now empty */
    }
    nextracted = 0;
}

/* The disc image to play from: the command line's, the installed copy
 * (data\disc.bin), or one found (or unpacked) next to the exe, which is
 * then installed. 0: the player quit. */
int setup_find_disc(HWND owner, const char *arg, const char *ini, char *path, size_t n) {
    char dir[MAX_PATH], why[512], found[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char *slash = strrchr(dir, '\\');
    if (slash) slash[1] = 0;
    if (arg && *arg) {
        if (check(arg, why, sizeof why)) { snprintf(path, n, "%s", arg); return 1; }
        char msg[1200];
        snprintf(msg, sizeof msg, "%s can't be used: %s.", base_name(arg), why);
        MessageBoxA(owner, msg, TITLE, MB_ICONWARNING);
    }
    snprintf(path, n, "%sdata\\disc.bin", dir);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES && check(path, why, sizeof why)) return 1;
    /* an earlier version's setting: the image it played from */
    GetPrivateProfileStringA("resurv", "disc", "", path, (DWORD)n, ini);
    if (*path && check(path, why, sizeof why)) goto found;
    int opened_folder = 0;
    for (;;) {
        if (search(owner, dir, path, n, found, sizeof found, why, sizeof why)) break;
        char msg[2048];
        if (found[0])
            snprintf(msg, sizeof msg,
                     "Found %s, but it can't be used: %s.\n\n"
                     "This folder needs a copy of Resident Evil Survivor (USA):\n%s\n\n"
                     "Yes: look again    No: choose the file yourself    Cancel: quit",
                     found, why, dir);
        else
            snprintf(msg, sizeof msg,
                     "Welcome! This needs your copy of Resident Evil Survivor (USA).\n\n"
                     "Put your disc image into this folder (it's opening now):\n%s\n\n"
                     "A .bin/.cue pair, a .chd, or the .zip they came in all work.\n"
                     "Then click Yes.\n\n"
                     "Yes: look again    No: choose the file yourself    Cancel: quit",
                     dir);
        if (!found[0] && !opened_folder) {
            ShellExecuteA(NULL, "open", dir, NULL, NULL, SW_SHOWNORMAL);
            opened_folder = 1;
        }
        int a = MessageBoxA(owner, msg, TITLE, MB_YESNOCANCEL | (found[0] ? MB_ICONWARNING : MB_ICONINFORMATION));
        if (a == IDCANCEL) return 0;
        if (a == IDNO) {
            char pick[MAX_PATH];
            if (!pick_file(owner, pick, sizeof pick)) continue;
            if (!_stricmp(ext_of(pick), ".zip")) {
                int z = unzip_disc(owner, pick, dir, why, sizeof why);
                if (z > 0 && search(owner, dir, path, n, found, sizeof found, why, sizeof why))
                    break;
                if (z < 0) snprintf(why, sizeof why, "there's no disc image inside it");
            } else if (check(pick, why, sizeof why)) {
                snprintf(path, n, "%s", pick);
                break;
            }
            snprintf(msg, sizeof msg, "%s can't be used: %s.", base_name(pick), why);
            MessageBoxA(owner, msg, TITLE, MB_ICONWARNING);
        }
    }
found:;
    /* check() left the image open: copy it in */
    char source[MAX_PATH], installed[MAX_PATH];
    snprintf(source, sizeof source, "%s", path);
    if (install(owner, dir, installed, sizeof installed, why, sizeof why)) {
        snprintf(path, n, "%s", installed);
        WritePrivateProfileStringA("resurv", "disc", NULL, ini);
        int from_zip = nextracted > 0;
        remove_extracted(dir);
        char msg[1024];
        snprintf(msg, sizeof msg,
                 "All set! The game is installed (in the \"data\" folder next to the game).\n\n"
                 "Your disc image%s%s%s isn't needed any more: you can delete it if you like.",
                 from_zip ? "" : " (", from_zip ? "" : base_name(source), from_zip ? "" : ")");
        MessageBoxA(owner, msg, TITLE, MB_ICONINFORMATION);
        return 1;
    }
    /* couldn't copy it: play from the image itself */
    char msg[1024];
    snprintf(msg, sizeof msg, "The game couldn't be installed: %s.\n\nIt will play from %s instead, so keep that file.",
             why, base_name(source));
    MessageBoxA(owner, msg, TITLE, MB_ICONWARNING);
    WritePrivateProfileStringA("resurv", "disc", source, ini);
    disc_image_open(source, why, sizeof why);
    return 1;
}
#endif
