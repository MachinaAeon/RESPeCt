/* Windows front end: window, keyboard and mouse (Raw Input), sound,
 * frame pacing. Settings (keys, sensitivity, video) come from resurv.ini
 * next to the exe (see config.c for the defaults).
 *
 * The game sees a digital pad in port 1 and a PlayStation Mouse in port 2.
 * While the player is in control the mouse aims (left click fires); in
 * menus, the inventory, the title and cutscenes it moves the cursor
 * instead (left click = OK, right click = cancel).
 * Esc releases the mouse; F11 or Alt+Enter toggles fullscreen; F1 opens the
 * settings panel (the game pauses while it's open); F5 saves the game to
 * the current slot, F9 loads it, F6/F7 pick the slot (saves/ next to the exe).
 */
#include <windows.h>          /* before rt.h: its R macro clashes with winnt.h */
#include <commdlg.h>
#include <xinput.h>

#include "rt.h"
#include "config.h"
#include "render.h"
#include "texpack.h"
#include "savestate.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

int rt_boot(const char *disc_path);
void rt_set_log(FILE *f);

static HWND hwnd;
static uint32_t *frame_buf;        /* up to 2560 x 1920 */
static int frame_w = 320, frame_h = 240;
static int captured, fullscreen, quit;
static char ini_path[MAX_PATH];
extern volatile int g_reload_request, g_weapon_request, g_give_all_request;  /* decomp/cheats.c */
extern int g_opt_fov;                                                       /* gte.c */
static int cutscene_frames;                /* frames in a cutscene so far (for the hint) */
#define SAVE_SLOTS 99
static int save_slot = 1;                  /* 1-99: F5 saves there, F9 loads it; F6/F7 pick */
static char toast[128];
static DWORD toast_until;

static void show_toast(const char *msg) {
    snprintf(toast, sizeof toast, "%s", msg);
    toast_until = GetTickCount() + 2500;
}

static void slot_path(int n, char *out, size_t size) {
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char *sl = strrchr(dir, '\\');
    if (sl) sl[1] = 0;
    char sub[MAX_PATH + 8];
    snprintf(sub, sizeof sub, "%ssaves", dir);
    CreateDirectoryA(sub, NULL);
    snprintf(out, size, "%s\\slot%d.sav", sub, n);
}

/* "Oct 22, 3:41 PM" for a slot's file, or "empty". */
static void slot_when(int n, char *out, size_t size) {
    char path[MAX_PATH + 32];
    slot_path(n, path, sizeof path);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) { snprintf(out, size, "empty"); return; }
    FILETIME lt;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lt);
    FileTimeToSystemTime(&lt, &st);
    static const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int h = st.wHour % 12;
    snprintf(out, size, "%s %d, %d:%02d %s", mon[(st.wMonth + 11) % 12], st.wDay, h ? h : 12, st.wMinute, st.wHour < 12 ? "AM" : "PM");
}

/* Start on the slot saved to last, so F5 doesn't land on an old one. */
static void pick_latest_slot(void) {
    ULONGLONG best = 0;
    for (int n = 1; n <= SAVE_SLOTS; n++) {
        char path[MAX_PATH + 32];
        WIN32_FILE_ATTRIBUTE_DATA fa;
        slot_path(n, path, sizeof path);
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) continue;
        ULONGLONG t = ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
        if (t > best) { best = t; save_slot = n; }
    }
}

/* F6 / F7: the previous / next slot. */
static void step_slot(int dir) {
    save_slot = (save_slot - 1 + dir + SAVE_SLOTS) % SAVE_SLOTS + 1;
    char w[48], m[96];
    slot_when(save_slot, w, sizeof w);
    snprintf(m, sizeof m, "Slot %d  (%s)", save_slot, w);
    show_toast(m);
}

static void quick_save(void) {
    char path[MAX_PATH + 32];
    slot_path(save_slot, path, sizeof path);
    ss_request_save(path);
}

static void quick_load(void) {
    char path[MAX_PATH + 32];
    slot_path(save_slot, path, sizeof path);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        char m[64];
        snprintf(m, sizeof m, "Slot %d is empty", save_slot);
        show_toast(m);
        return;
    }
    ss_request_load(path);
}

static LARGE_INTEGER next_frame;
void plat_ss_done(int ok, int was_load, const char *msg) {
    char m[160];
    if (ok) snprintf(m, sizeof m, "%s slot %d", was_load ? "Loaded" : "Saved to", save_slot);
    else snprintf(m, sizeof m, "%s failed: %s", was_load ? "Loading" : "Saving", msg);
    show_toast(m);
    if (was_load && ok) next_frame.QuadPart = 0;   /* don't race to catch up */
}
static int menu_open, suppress_keys;
static int ff_held(void);                  /* controller section */
static int pad_ok;
#include "../decomp/savegame.h"


static WINDOWPLACEMENT saved_place = {sizeof saved_place};
static LARGE_INTEGER qpf;
static volatile LONG pending_dx, pending_dy;

/* ------------------------------------------------------------ audio
 * waveOut, 44.1 kHz stereo, fed from the SPU's ring buffer every frame in
 * blocks of AUDIO_BLOCK frames. About AUDIO_TARGET blocks stay queued. */
#define AUDIO_BLOCK 512
#define AUDIO_BUFS 32
#define AUDIO_TARGET 12
static HWAVEOUT wave;
static WAVEHDR whdr[AUDIO_BUFS];
static int16_t wbuf[AUDIO_BUFS][AUDIO_BLOCK * 2];
static int wnext, wfill;

static void audio_init(void) {
    WAVEFORMATEX f = {0};
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 2;
    f.nSamplesPerSec = 44100;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 4;
    f.nAvgBytesPerSec = 44100 * 4;
    if (waveOutOpen(&wave, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        rt_log("audio: waveOutOpen failed, no sound");
        wave = NULL;
        return;
    }
    for (int i = 0; i < AUDIO_BUFS; i++) {
        whdr[i].lpData = (LPSTR)wbuf[i];
        whdr[i].dwBufferLength = AUDIO_BLOCK * 4;
        whdr[i].dwFlags = WHDR_DONE;
    }
}

static int audio_queued(void) {
    int n = 0;
    for (int i = 0; i < AUDIO_BUFS; i++)
        if (!(whdr[i].dwFlags & WHDR_DONE)) n++;
    return n;
}

static double slow_frame_ms, slow_paint_ms;   /* worst this log period */
static LARGE_INTEGER last_end;                 /* end of the last plat_frame */

static void audio_pump(void) {
    if (!wave) {
        int16_t junk[1024 * 2];
        while (spu_take_samples(junk, 1024) > 0) {}
        return;
    }
    /* The device ran dry (it played everything we gave it): a gap, heard as
     * a crackle. Counted for the log. */
    static int started, underruns, logged_frame, rebuffer;
    if (started && audio_queued() == 0) { underruns++; rebuffer = 1; }
    if (underruns && g_frame - (uint32_t)logged_frame > 600) {
        rt_log("audio: %d underruns in the last 10 s (the game ran behind; slowest frame %.1f ms, picture %.1f ms)",
               underruns, slow_frame_ms, slow_paint_ms);
        underruns = 0;
        slow_frame_ms = slow_paint_ms = 0;
        logged_frame = (int)g_frame;
    }
    /* After running dry, wait until a few blocks are ready and queue them
     * together: one short gap instead of a string of crackles. */
    if (rebuffer) {
        if (spu_samples_available() + wfill < AUDIO_BLOCK * 4) return;
        rebuffer = 0;
    }
    for (;;) {
        WAVEHDR *h = &whdr[wnext];
        /* Every buffer queued: keep the rest in the SPU's ring buffer; the
         * frame pacing slows down until the device catches up. Sound is
         * only dropped if that ring buffer would overflow. */
        if (!(h->dwFlags & WHDR_DONE)) {
            if (spu_samples_available() > 16384) {
                int16_t junk[1024 * 2];
                while (spu_samples_available() > 4096 && spu_take_samples(junk, 1024) > 0) {}
            }
            return;
        }
        int16_t *dst = wbuf[wnext] + wfill * 2;
        int got = spu_take_samples(dst, AUDIO_BLOCK - wfill);
        wfill += got;
        if (wfill < AUDIO_BLOCK) return;
        if (h->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wave, h, sizeof *h);
        h->dwFlags = 0;
        waveOutPrepareHeader(wave, h, sizeof *h);
        waveOutWrite(wave, h, sizeof *h);
        wnext = (wnext + 1) % AUDIO_BUFS;
        wfill = 0;
        started = 1;
    }
}

/* PS1 pad bits (1 = pressed) */
enum {
    PS_SELECT = 0x0001, PS_START = 0x0008, PS_UP = 0x0010, PS_RIGHT = 0x0020, PS_DOWN = 0x0040,
    PS_LEFT = 0x0080, PS_L2 = 0x0100, PS_R2 = 0x0200, PS_L1 = 0x0400, PS_R1 = 0x0800,
    PS_TRIANGLE = 0x1000, PS_CIRCLE = 0x2000, PS_CROSS = 0x4000, PS_SQUARE = 0x8000
};

/* Key bindings from the settings, as virtual-key codes. */
static int bind_vk[ACT_COUNT][MAX_KEYS_PER_ACTION];

static const struct { const char *name; int vk; } key_names[] = {
    {"Space", VK_SPACE}, {"Enter", VK_RETURN}, {"Return", VK_RETURN}, {"Tab", VK_TAB},
    {"Backspace", VK_BACK}, {"LShift", VK_LSHIFT}, {"RShift", VK_RSHIFT}, {"Shift", VK_SHIFT},
    {"LCtrl", VK_LCONTROL}, {"RCtrl", VK_RCONTROL}, {"Ctrl", VK_CONTROL}, {"LAlt", VK_LMENU},
    {"RAlt", VK_RMENU}, {"Alt", VK_MENU}, {"Up", VK_UP}, {"Down", VK_DOWN}, {"Left", VK_LEFT},
    {"Right", VK_RIGHT}, {"Insert", VK_INSERT}, {"Delete", VK_DELETE}, {"Home", VK_HOME},
    {"End", VK_END}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT}, {"CapsLock", VK_CAPITAL},
    {"Mouse2", VK_RBUTTON}, {"Mouse3", VK_MBUTTON}, {"Mouse4", VK_XBUTTON1}, {"Mouse5", VK_XBUTTON2},
};

static int vk_from_name(const char *n) {
    char low[16];
    int i = 0;
    for (; n[i] && i < 15; i++) low[i] = (char)tolower((unsigned char)n[i]);
    low[i] = 0;
    if (!low[1] && (isalnum((unsigned char)low[0]))) return toupper((unsigned char)low[0]);
    if (low[0] == 'f' && isdigit((unsigned char)low[1])) {
        int f = atoi(low + 1);
        if (f >= 1 && f <= 12) return VK_F1 + f - 1;
    }
    if (!strncmp(low, "numpad", 6) && isdigit((unsigned char)low[6])) return VK_NUMPAD0 + low[6] - '0';
    for (size_t k = 0; k < sizeof key_names / sizeof key_names[0]; k++) {
        const char *a = key_names[k].name;
        size_t j = 0;
        while (a[j] && tolower((unsigned char)a[j]) == low[j]) j++;
        if (!a[j] && !low[j]) return key_names[k].vk;
    }
    rt_log("settings: unknown key name '%s'", n);
    return 0;
}

/* The settings-file name of a key, or NULL if it can't be bound. */
static const char *vk_name(int vk) {
    static char buf[16];
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= VK_F1 && vk <= VK_F12) { snprintf(buf, sizeof buf, "F%d", vk - VK_F1 + 1); return buf; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { snprintf(buf, sizeof buf, "Numpad%d", vk - VK_NUMPAD0); return buf; }
    for (size_t k = 0; k < sizeof key_names / sizeof key_names[0]; k++)
        if (key_names[k].vk == vk) return key_names[k].name;
    return NULL;
}

static void load_bindings(void) {
    for (int a = 0; a < ACT_COUNT; a++)
        for (int k = 0; k < MAX_KEYS_PER_ACTION; k++)
            bind_vk[a][k] = k < g_cfg.nkeys[a] ? vk_from_name(g_cfg.keys[a][k]) : 0;
}

static int is_mouse_vk(int vk) {
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
}

static void set_capture(int on) {
    if (on == captured) return;
    captured = on;
    if (on) {
        RECT r;
        GetClientRect(hwnd, &r);
        MapWindowPoints(hwnd, NULL, (POINT *)&r, 2);
        ClipCursor(&r);
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        ClipCursor(NULL);
        while (ShowCursor(TRUE) < 0) {}
    }
}

static void toggle_fullscreen(void) {
    DWORD style = (DWORD)GetWindowLongPtr(hwnd, GWL_STYLE);
    if (!fullscreen) {
        MONITORINFO mi = {sizeof mi};
        GetWindowPlacement(hwnd, &saved_place);
        GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongPtr(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &saved_place);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullscreen = !fullscreen;
    g_cfg.fullscreen = fullscreen;
    if (captured) { captured = 0; set_capture(1); }
}

/* The frame to show: drawing on the graphics card, made at the size it's
 * shown at (paint's picture area), so a picture drawn larger than the
 * window comes out smoothed rather than with rows and columns dropped. */
static void grab_frame(void) {
    extern int g_display_w, g_display_h, g_display_smooth;
    RECT r;
    GetClientRect(hwnd, &r);
    int cw = r.right, ch = r.bottom;
    int an = g_widescreen ? 16 : 4, ad = g_widescreen ? 9 : 3;
    int dw = cw, dh = cw * ad / an;
    if (dh > ch) { dh = ch; dw = ch * an / ad; }
    g_display_w = dw > 0 ? dw : 0;
    g_display_h = dh > 0 ? dh : 0;
    g_display_smooth = g_cfg.smooth_scaling;
    display_grab_bgrx(frame_buf, &frame_w, &frame_h);
}

static int key_down(int vk);
static void paint(HDC dc) {
    RECT r;
    GetClientRect(hwnd, &r);
    int cw = r.right, ch = r.bottom;
    /* 4:3 picture (16:9 in widescreen), centred with black bars */
    int an = g_widescreen ? 16 : 4, ad = g_widescreen ? 9 : 3;
    int dw = cw, dh = cw * ad / an;
    if (dh > ch) { dh = ch; dw = ch * an / ad; }
    int dx = (cw - dw) / 2, dy = (ch - dh) / 2;
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = frame_w;
    bi.bmiHeader.biHeight = -frame_h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    /* HALFTONE is slow on big pictures (tens of ms for a 4x picture on a
     * large screen: the game fell behind and the sound broke up, worst in
     * busy moments like door transitions); when the picture is already
     * sharp (rendered at 2x or more) plain scaling looks the same. */
    int smooth = g_cfg.smooth_scaling && frame_w <= 640 && dw > frame_w * 3 / 2;
    SetStretchBltMode(dc, smooth ? HALFTONE : COLORONCOLOR);
    SetBrushOrgEx(dc, 0, 0, NULL);
    StretchDIBits(dc, dx, dy, dw, dh, 0, 0, frame_w, frame_h, frame_buf, &bi, DIB_RGB_COLORS, SRCCOPY);
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT bars[4] = {{0, 0, cw, dy}, {0, dy + dh, cw, ch}, {0, dy, dx, dy + dh}, {dx + dw, dy, cw, dy + dh}};
    for (int i = 0; i < 4; i++) FillRect(dc, &bars[i], black);
    {   /* saved games: the payphone prompt */
        const char *hint = NULL;
        if (!menu_open && sg_at_payphone()) hint = "Left click: save your game here";
        if (!menu_open && cutscene_frames > 90 && !ff_held()) hint = pad_ok ? "Hold A to fast-forward" : "Hold Space to fast-forward";
        if (hint) {
            int fh = ch / 26 < 14 ? 14 : ch / 26;
            HFONT f = CreateFontA(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            HGDIOBJ of = SelectObject(dc, f);
            SetBkMode(dc, TRANSPARENT);
            RECT hr = {dx, dy + dh - fh * 3, dx + dw, dy + dh - fh};
            RECT sh = {hr.left + 2, hr.top + 2, hr.right + 2, hr.bottom + 2};
            SetTextColor(dc, RGB(0, 0, 0));
            DrawTextA(dc, hint, -1, &sh, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(dc, RGB(240, 220, 160));
            DrawTextA(dc, hint, -1, &hr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, of);
            DeleteObject(f);
        }
    }
    if (toast[0] && GetTickCount() < toast_until) {
        int fh = ch / 28 < 14 ? 14 : ch / 28;
        HFONT f = CreateFontA(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        HGDIOBJ of = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        RECT r = {dx + fh, dy + fh, dx + dw - fh, dy + fh * 3};
        RECT sh = {r.left + 2, r.top + 2, r.right + 2, r.bottom + 2};
        SetTextColor(dc, RGB(0, 0, 0));
        DrawTextA(dc, toast, -1, &sh, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SetTextColor(dc, RGB(235, 235, 235));
        DrawTextA(dc, toast, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(dc, of);
        DeleteObject(f);
    }
}

/* ------------------------------------------------------------ saved games
 * (decomp/savegame.c): a payphone opens the game's save screen; the
 * progress goes to saves\ next to the card save, and LOAD GAME picks it
 * up again. */

static void games_dir(void) {
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char *sl = strrchr(dir, '\\');
    if (sl) sl[1] = 0;
    char sub[MAX_PATH + 8];
    snprintf(sub, sizeof sub, "%ssaves", dir);
    CreateDirectoryA(sub, NULL);
    sg_set_dir(sub);
}

void sg_save_done(int ok) { show_toast(ok ? "Game saved" : "Saving the game's progress failed"); }

static void paint(HDC dc);
static void save_settings(void);
/* F2: HD textures on/off (remembered in resurv.ini). */
static void toggle_hd_textures(void) {
    if (!texpack_loaded()) { show_toast("No HD texture pack found next to the game"); return; }
    g_cfg.hd_textures = !g_cfg.hd_textures;
    g_hd_textures = g_cfg.hd_textures;
    save_settings();
    show_toast(g_hd_textures ? "HD textures on" : "HD textures off");
}
static int menu_message(UINT msg, WPARAM wp, LPARAM lp);
static void menu_toggle(void);

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (menu_open && menu_message(msg, wp, lp)) return 0;
    switch (msg) {
    case WM_CLOSE: quit = 1; return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) set_capture(0);
        return 0;
    case WM_LBUTTONDOWN: {
        if (!captured && !menu_open) { set_capture(1); return 0; }
        break;
    }
    case WM_SIZE: case WM_MOVE:
        if (captured) { captured = 0; set_capture(1); }
        break;
    case WM_SYSKEYDOWN:
        if (wp == VK_RETURN && (lp & (1 << 29))) { toggle_fullscreen(); return 0; }
        break;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) set_capture(0);
        if (wp == VK_F11) toggle_fullscreen();
        if (wp == VK_F1) menu_toggle();
        if (wp == VK_F5) quick_save();
        if (wp == VK_F6) step_slot(-1);
        if (wp == VK_F7) step_slot(1);
        if (wp == VK_F9) quick_load();
        if (wp == VK_F2 && !(lp & (1 << 30))) toggle_hd_textures();
        break;
    case WM_MOUSEWHEEL:
        /* wheel up: previous weapon, down: next (as in most shooters) */
        if (captured && game_player_in_control())
            g_weapon_request = (short)HIWORD(wp) > 0 ? 11 : 10;
        return 0;
    case WM_INPUT: {
        RAWINPUT ri;
        UINT sz = sizeof ri;
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &sz, sizeof ri.header) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE && captured && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            pending_dx += ri.data.mouse.lLastX;
            pending_dy += ri.data.mouse.lLastY;
        }
        break;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}

static void pump(void) {
    MSG m;
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

static int key_down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }

/* Mouse motion scaled by the sensitivity, keeping the fractions so slow
 * movements aren't lost. 1.0 = 2 heading units (0.18 degrees) per count. */
static double frac_x, frac_y;
static int scaled(double *frac, double v) {
    *frac += v;
    double w = floor(*frac);
    *frac -= w;
    return (int)w;
}

/* Menu cursor from the mouse: every menu_mouse_step counts of motion taps
 * the d-pad (held 2 frames, released 2). */
static int menu_acc_x, menu_acc_y, menu_tap_x, menu_tap_y;
static uint16_t menu_bits_x, menu_bits_y;

static uint16_t menu_taps(int dx, int dy) {
    int step = g_cfg.menu_mouse_step;
    menu_acc_x += dx;
    menu_acc_y += dy;
    if (menu_acc_x > 3 * step) menu_acc_x = 3 * step;
    if (menu_acc_x < -3 * step) menu_acc_x = -3 * step;
    if (menu_acc_y > 3 * step) menu_acc_y = 3 * step;
    if (menu_acc_y < -3 * step) menu_acc_y = -3 * step;
    if (menu_tap_x == 0 && (menu_acc_x >= step || menu_acc_x <= -step)) {
        menu_bits_x = menu_acc_x > 0 ? PS_RIGHT : PS_LEFT;
        menu_acc_x += menu_acc_x > 0 ? -step : step;
        menu_tap_x = 4;
    }
    if (menu_tap_y == 0 && (menu_acc_y >= step || menu_acc_y <= -step)) {
        menu_bits_y = menu_acc_y > 0 ? PS_DOWN : PS_UP;
        menu_acc_y += menu_acc_y > 0 ? -step : step;
        menu_tap_y = 4;
    }
    uint16_t b = 0;
    if (menu_tap_x) { if (menu_tap_x > 2) b |= menu_bits_x; menu_tap_x--; }
    if (menu_tap_y) { if (menu_tap_y > 2) b |= menu_bits_y; menu_tap_y--; }
    return b;
}

static int any_key_pressed;
/* ------------------------------------------------------------ controller
 * XInput (Xbox controllers; PlayStation and other controllers through
 * Steam Input or similar). Left stick and d-pad: the d-pad (walk, strafe,
 * menu cursor); right stick: look, as mouse motion; buttons as resurv.ini's
 * [controller] section says. The XInput DLL is loaded when there is one. */
typedef DWORD (WINAPI *XInputGetStateFn)(DWORD, XINPUT_STATE *);
static XInputGetStateFn xinput_get;
static int pad_slot = -1;
static DWORD pad_next_scan;
static double pad_fx, pad_fy;
static uint32_t pad_prev;                  /* last frame's buttons (for presses) */
static LARGE_INTEGER pad_last;

static XINPUT_STATE pad_st;                /* this frame's controller state (pad_update) */
static int pad_sprint;                     /* sprint toggled on (L3), until the player stops */
static int menu_pad_fresh;                 /* the panel just opened: buttons held then don't count */

static int pad_poll(XINPUT_STATE *st) {
    if (!g_cfg.pad_enabled) { pad_slot = -1; return 0; }
    if (!xinput_get) {
        static int tried;
        if (tried) return 0;
        tried = 1;
        const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
        for (int i = 0; i < 3 && !xinput_get; i++) {
            HMODULE m = LoadLibraryA(dlls[i]);
            if (m) xinput_get = (XInputGetStateFn)(void (*)(void))GetProcAddress(m, "XInputGetState");
        }
        if (!xinput_get) return 0;
    }
    if (pad_slot >= 0) {
        if (xinput_get((DWORD)pad_slot, st) == ERROR_SUCCESS) return 1;
        pad_slot = -1;
        show_toast("Controller disconnected");
    }
    DWORD now = GetTickCount();                 /* looking for one is slow: now and then */
    if ((int32_t)(now - pad_next_scan) < 0) return 0;
    pad_next_scan = now + 2000;
    for (int i = 0; i < 4; i++)
        if (xinput_get((DWORD)i, st) == ERROR_SUCCESS) {
            pad_slot = i;
            show_toast("Controller connected");
            return 1;
        }
    return 0;
}

static void pad_update(void) {
    pad_ok = GetForegroundWindow() == hwnd && pad_poll(&pad_st);
    if (!pad_ok) memset(&pad_st, 0, sizeof pad_st);
}

static int pad_down(WORD b) { return pad_ok && (pad_st.Gamepad.wButtons & b) != 0; }

/* Fast-forward held: Space, or the controller's A. */
static int ff_held(void) {
    return (GetForegroundWindow() == hwnd && key_down(VK_SPACE)) || pad_down(XINPUT_GAMEPAD_A);
}

/* A stick, -1..1 each way, past the dead zone (radial), with a curve for
 * fine aiming near the centre. */
static void pad_stick(int sx, int sy, double *ox, double *oy, int curve) {
    double x = sx / 32767.0, y = sy / 32767.0;
    double m = sqrt(x * x + y * y), dz = g_cfg.pad_deadzone / 100.0;
    if (m <= dz) { *ox = *oy = 0; return; }
    double k = (m > 1 ? 1 : m) - dz;
    k /= 1 - dz;
    if (curve) k = k * k;
    *ox = x / m * k;
    *oy = y / m * k;
}

/* Pad bits, look motion and actions from the controller this frame. */
static uint16_t pad_input(int playing, int *fire, double *look_dx, double *look_dy) {
    *fire = 0;
    *look_dx = *look_dy = 0;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double dt = pad_last.QuadPart ? (double)(now.QuadPart - pad_last.QuadPart) / (double)qpf.QuadPart : 0;
    pad_last = now;
    if (dt > 0.05) dt = 0.05;
    if (!pad_ok) { pad_prev = 0; pad_sprint = 0; return 0; }
    const XINPUT_GAMEPAD *g = &pad_st.Gamepad;
    uint32_t down = 0;                          /* bit per PADB_ */
    static const WORD xb[PADB_COUNT] = {XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
                                        XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER, 0, 0,
                                        XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_LEFT_THUMB,
                                        XINPUT_GAMEPAD_RIGHT_THUMB};
    for (int i = 0; i < PADB_COUNT; i++)
        if (xb[i] && (g->wButtons & xb[i])) down |= 1u << i;
    if (g->bLeftTrigger > 64) down |= 1u << PADB_LT;
    if (g->bRightTrigger > 64) down |= 1u << PADB_RT;
    uint32_t pressed = down & ~pad_prev;
    pad_prev = down;
    uint16_t b = 0;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_UP) b |= PS_UP;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) b |= PS_DOWN;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) b |= PS_LEFT;
    if (g->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) b |= PS_RIGHT;
    double lx, ly, rx, ry;
    pad_stick(g->sThumbLX, g->sThumbLY, &lx, &ly, 0);
    if (ly > 0.45) b |= PS_UP;
    if (ly < -0.45) b |= PS_DOWN;
    if (lx < -0.45) b |= PS_LEFT;
    if (lx > 0.45) b |= PS_RIGHT;
    for (int i = 0; i < PADB_COUNT; i++) {
        if (!(down & (1u << i))) continue;
        const char *a = g_cfg.pad_bind[i];
        int press = (pressed >> i) & 1;
        if (!strcmp(a, "cross")) b |= PS_CROSS;
        else if (!strcmp(a, "square")) b |= PS_SQUARE;
        else if (!strcmp(a, "circle")) b |= PS_CIRCLE;
        else if (!strcmp(a, "triangle")) b |= PS_TRIANGLE;
        else if (!strcmp(a, "start")) b |= PS_START;
        else if (!strcmp(a, "select")) b |= PS_SELECT;
        else if (!strcmp(a, "r1")) b |= PS_R1;
        else if (!strcmp(a, "l2")) b |= PS_L2;
        else if (!strcmp(a, "r2")) b |= PS_R2;
        else if (!strcmp(a, "quick_search")) b |= g_action_bits[ACT_QUICK_SEARCH];
        else if (!strcmp(a, "sprint")) { if (press) pad_sprint = !pad_sprint; }
        else if (!strcmp(a, "fire")) *fire = 1;
        else if (!strcmp(a, "reload")) { if (press && playing) g_reload_request = 1; }
        else if (!strcmp(a, "next_weapon")) { if (press && playing) g_weapon_request = 10; }
        else if (!strcmp(a, "prev_weapon")) { if (press && playing) g_weapon_request = 11; }
    }
    /* sprint toggle: on until the player stops moving */
    {
        static int moved;
        if (!pad_sprint) moved = 0;
        else if (b & (PS_UP | PS_DOWN | PS_LEFT | PS_RIGHT)) { b |= g_action_bits[ACT_SPRINT]; moved = 1; }
        else if (moved) pad_sprint = 0;
    }
    if (playing) {
        pad_stick(g->sThumbRX, g->sThumbRY, &rx, &ry, 1);
        double units = 4096.0 / 360.0 * dt;     /* game heading units per degree, this frame */
        *look_dx = rx * g_cfg.pad_look_speed * units;
        *look_dy = -ry * g_cfg.pad_look_speed_y * units * (g_cfg.pad_invert_y ? -1 : 1);
    }
    if (b || down) any_key_pressed = 1;
    return b;
}

extern int g_cheat_god, g_cheat_infinite_ammo, g_cheat_no_reload;   /* decomp/cheats.c */
static int reload_was_down, digit_was_down[10];

/* A number key bound to an action does that instead of picking a weapon. */
static int digit_bound(int d) {
    for (int a = 0; a < ACT_COUNT; a++)
        for (int k = 0; k < MAX_KEYS_PER_ACTION; k++)
            if (bind_vk[a][k] == '0' + d) return 1;
    return 0;
}

static void read_input(void) {
    uint16_t b = 0;
    int focused = GetForegroundWindow() == hwnd;
    int playing = game_player_in_control();
    int menu = g_cfg.menu_mouse && !playing;
    long dx = pending_dx, dy = pending_dy;
    pending_dx = pending_dy = 0;
    if (focused) {
        for (int a = 0; a < ACT_COUNT; a++)
            for (int k = 0; k < MAX_KEYS_PER_ACTION; k++) {
                int vk = bind_vk[a][k];
                if (!vk || (menu && is_mouse_vk(vk)) || !key_down(vk)) continue;
                b |= g_action_bits[a];
            }
    }
    /* Reload isn't a pad button: a press asks the port to reload. */
    int reload_down = 0;
    if (focused)
        for (int k = 0; k < MAX_KEYS_PER_ACTION; k++)
            if (bind_vk[ACT_RELOAD][k] && key_down(bind_vk[ACT_RELOAD][k])) reload_down = 1;
    if (reload_down && !reload_was_down && playing && !suppress_keys) g_reload_request = 1;
    reload_was_down = reload_down;
    /* 1-9: the nth weapon in the inventory */
    for (int d = 1; d <= 9; d++) {
        int down = focused && key_down('0' + d) && !digit_bound(d);
        if (down && !digit_was_down[d] && playing && !suppress_keys) g_weapon_request = d;
        digit_was_down[d] = down;
    }
    if (suppress_keys) {                     /* keys (or buttons) still held from the settings panel */
        if (b || key_down(VK_LBUTTON) || key_down(VK_RBUTTON) || (pad_ok && (pad_st.Gamepad.wButtons || pad_st.Gamepad.bLeftTrigger > 64 ||
                                                                            pad_st.Gamepad.bRightTrigger > 64))) { b = 0; dx = dy = 0; }
        else suppress_keys = 0;
    }
    int pad_fire = 0;
    double pad_dx = 0, pad_dy = 0;
    if (focused && !suppress_keys) b |= pad_input(playing, &pad_fire, &pad_dx, &pad_dy);
    if (b) any_key_pressed = 1;
    g_input.mouse_present = 1;
    if (menu) {
        if (captured) {
            b |= menu_taps((int)dx, (int)dy);
            if (key_down(VK_LBUTTON)) b |= PS_CROSS;
            if (key_down(VK_RBUTTON)) b |= PS_SQUARE;
        }
        g_input.mouse_left = g_input.mouse_right = 0;
        g_input.mouse_dx = g_input.mouse_dy = 0;
        g_input.look_dx = g_input.look_dy = 0;
        frac_x = frac_y = 0;
    } else {
        menu_acc_x = menu_acc_y = 0;
        g_input.mouse_left = (captured && !suppress_keys && key_down(VK_LBUTTON)) || pad_fire;
        {   /* a click at a payphone uses it (savegame.c turns the shot off there) */
            static int was;
            if (g_input.mouse_left && !was && sg_at_payphone()) g_sg_use = 1;
            was = g_input.mouse_left;
        }
        g_input.mouse_right = 0;
        int mx = scaled(&frac_x, dx * 2.0 * g_cfg.sens_x) + scaled(&pad_fx, pad_dx);
        int my = scaled(&frac_y, dy * 2.0 * g_cfg.sens_y * (g_cfg.invert_y ? -1 : 1)) + scaled(&pad_fy, pad_dy);
        g_input.mouse_dx += mx;
        g_input.mouse_dy += my;
        g_input.look_dx += mx;
        g_input.look_dy += my;
    }
    g_input.buttons = b;
    /* Don't build up a backlog of motion the game hasn't read. */
    if (g_input.mouse_dx > 512) g_input.mouse_dx = 512;
    if (g_input.mouse_dx < -512) g_input.mouse_dx = -512;
    if (g_input.mouse_dy > 512) g_input.mouse_dy = 512;
    if (g_input.mouse_dy < -512) g_input.mouse_dy = -512;
    if (g_input.look_dx > 1024) g_input.look_dx = 1024;
    if (g_input.look_dx < -1024) g_input.look_dx = -1024;
    if (g_input.look_dy > 1024) g_input.look_dy = 1024;
    if (g_input.look_dy < -1024) g_input.look_dy = -1024;
}

/* FPS turning, when the game takes the mouse's motion for its frame: raw
 * input that came in since this vblank's read_input, so a movement doesn't
 * wait up to a vblank for the next one. */
void plat_poll_look(void) {
    if (!captured || menu_open || suppress_keys) return;
    MSG m;
    while (PeekMessage(&m, hwnd, WM_INPUT, WM_INPUT, PM_REMOVE)) DispatchMessage(&m);
    long dx = pending_dx, dy = pending_dy;
    if (!dx && !dy) return;
    pending_dx = pending_dy = 0;
    int mx = scaled(&frac_x, dx * 2.0 * g_cfg.sens_x);
    int my = scaled(&frac_y, dy * 2.0 * g_cfg.sens_y * (g_cfg.invert_y ? -1 : 1));
    g_input.mouse_dx += mx;
    g_input.mouse_dy += my;
    g_input.look_dx += mx;
    g_input.look_dy += my;
}

static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep) {
    HMODULE base = GetModuleHandleA(NULL);
    uintptr_t at = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    rt_log("CRASH: exception %08lX at %p (exe+%llX), game frame %u, last loop %08X",
           ep->ExceptionRecord->ExceptionCode, (void *)at,
           (unsigned long long)(at - (uintptr_t)base), g_frame, g_poll_pc);
    char text[256];
    snprintf(text, sizeof text, "The game crashed (exception %08lX).\nDetails are in resurv.log.",
             ep->ExceptionRecord->ExceptionCode);
    set_capture(0);
    MessageBoxA(hwnd, text, "RESPeCt", MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}


/* ------------------------------------------------------------ settings panel
 * F1 pauses the game and shows every setting over the picture. Keyboard:
 * Up/Down choose, Left/Right change, Enter picks a new key, Delete removes a
 * key's extra bindings, F1 or Esc closes. Mouse: point, left click = next
 * value, right click = previous, wheel = change. Changes apply at once and
 * are saved to resurv.ini when the panel closes. */

enum { IT_FLOAT, IT_INT, IT_BOOL, IT_SCALE, IT_KEY, IT_SLOT, IT_ACTION, IT_FOV };
typedef struct {
    int page, type;
    const char *label;
    void *val;
    double lo, hi, step;
    const char *hint;
} MenuItem;

static const char *page_names[] = {"Mouse & game", "Video", "Audio", "Keys", "Saves", "Cheats"};
#define NPAGES 6
static MenuItem items[96];
static int nitems;
static int menu_page, menu_sel, menu_binding = -1, menu_bind_armed;
static int was_captured;
static RECT item_rect[96], tab_rect[NPAGES];

static const char *action_labels[ACT_COUNT] = {
    "Move forward", "Move back", "Strafe left", "Strafe right", "Sprint", "Quick search (L1)",
    "Cross (OK)", "Triangle", "Square", "Circle", "Start", "Select", "R1", "L2", "R2", "Reload"};

static void add_item(int page, int type, const char *label, void *val, double lo, double hi, double step,
                     const char *hint) {
    items[nitems++] = (MenuItem){page, type, label, val, lo, hi, step, hint};
}

static void menu_build(void) {
    if (nitems) return;
    add_item(0, IT_FLOAT, "Turn sensitivity", &g_cfg.sens_x, 0.1, 5.0, 0.1, "How fast the mouse turns you.");
    add_item(0, IT_FLOAT, "Look sensitivity", &g_cfg.sens_y, 0.1, 5.0, 0.1, "How fast the mouse looks up and down.");
    add_item(0, IT_BOOL, "Invert look", &g_cfg.invert_y, 0, 1, 1, "On: pushing the mouse forward looks down.");
    add_item(0, IT_INT, "Look up/down limit", &g_cfg.look_limit, 10, 85, 5, "How far you can look up or down, in degrees.");
    add_item(0, IT_BOOL, "Mouse in menus", &g_cfg.menu_mouse, 0, 1, 1,
             "The mouse moves menu cursors: left click = OK, right click = cancel.");
    add_item(0, IT_INT, "Menu cursor distance", &g_cfg.menu_mouse_step, 40, 800, 20,
             "Mouse movement per menu step. Lower = faster cursor.");
    add_item(0, IT_BOOL, "Controller", &g_cfg.pad_enabled, 0, 1, 1,
             "Xbox controllers (others through Steam). Buttons can be changed in resurv.ini, [controller].");
    add_item(0, IT_INT, "Controller turn speed", &g_cfg.pad_look_speed, 50, 1000, 25, "Degrees per second with the right stick all the way over.");
    add_item(0, IT_INT, "Controller look speed", &g_cfg.pad_look_speed_y, 50, 1000, 25, "Up and down, degrees per second at full tilt.");
    add_item(0, IT_BOOL, "Controller invert look", &g_cfg.pad_invert_y, 0, 1, 1, "On: pushing the right stick up looks down.");
    add_item(0, IT_INT, "Controller dead zone", &g_cfg.pad_deadzone, 0, 50, 1, "How much stick movement around the centre is ignored, in %.");
    add_item(0, IT_BOOL, "Skip intro", &g_cfg.skip_intro, 0, 1, 1, "Fast-forward the logos and warnings at startup.");
    add_item(1, IT_BOOL, "Fullscreen", &g_cfg.fullscreen, 0, 1, 1, "F11 or Alt+Enter also switches.");
    add_item(1, IT_SCALE, "Resolution", &g_cfg.resolution_scale, 1, render_on_gpu() ? 8 : 4, 1,
             "Internal rendering resolution. Higher is sharper and needs a faster PC.");
    add_item(1, IT_BOOL, "Widescreen (experimental)", &g_cfg.widescreen, 0, 1, 1,
             "16:9: a wider view of the 3D scene; the HUD keeps its shape. Some full-screen menus are stretched.");
    add_item(1, IT_INT, "Brightness (%)", &g_cfg.brightness, 50, 200, 5,
             "100 is as the PlayStation shows it. Higher lifts the dark parts of the picture.");
    add_item(1, IT_FOV, "Field of view", &g_cfg.field_of_view, 60, 110, 5,
             "How wide you see, across a 4:3 screen (widescreen adds more). Original is about 61 degrees.");
    add_item(2, IT_INT, "Master volume (%)", &g_cfg.volume, 0, 100, 5, "Everything: sound effects, voices and music.");
    add_item(2, IT_INT, "Music & voices (%)", &g_cfg.music_volume, 0, 100, 5,
             "Music and speech streamed from the disc.");
    add_item(2, IT_INT, "Sound effects (%)", &g_cfg.sfx_volume, 0, 100, 5, "Guns, footsteps, zombies, everything else.");
    add_item(2, IT_BOOL, "Limiter", &g_cfg.limiter, 0, 1, 1,
             "Turns loud moments down smoothly instead of letting them distort (clip). Recommended.");
    add_item(2, IT_INT, "Same sound at once", &g_cfg.same_sound_limit, 0, 8, 1,
             "How many copies of one sound effect may play together (a swarm of bugs starts dozens). 0 = no limit, as on the PlayStation.");
    add_item(1, IT_BOOL, "Auto resolution", &g_cfg.auto_resolution, 0, 1, 1,
             "Lower the resolution by itself when this PC can't keep the game at full speed (it's drawn by the CPU, not the graphics card).");
    add_item(1, IT_BOOL, "Perspective-correct textures", &g_cfg.perspective, 0, 1, 1,
             "Textures stay straight on big walls and floors instead of bending and swimming as you move (the PlayStation can't do this). At 2x and higher, close floors and walls also stay whole instead of jumping as the game splits them.");
    add_item(1, IT_BOOL, "Smooth textures", &g_cfg.smooth_textures, 0, 1, 1,
             "Smooth the game's own textures on 3D surfaces at 2x and higher, as the PC version draws them, instead of square texels.");
    add_item(1, IT_BOOL, "24-bit colour", &g_cfg.true_color, 0, 1, 1,
             "Shade and blend the picture in 24-bit colour at 2x and higher, as the PC version does, instead of the PlayStation's 15 bits: smooth gradients, no banding in the dark.");
    add_item(1, IT_BOOL, "Steady 3D", &g_cfg.subpixel, 0, 1, 1, "Smoother geometry without the PlayStation wobble.");
    add_item(1, IT_BOOL, "Smooth scaling", &g_cfg.smooth_scaling, 0, 1, 1, "Smooth the picture when scaling it to the window.");
    add_item(1, IT_BOOL, "Depth fix", &g_cfg.depth_buffer, 0, 1, 1,
             "Keeps the back of a model from showing through its front (faces, legs), which shows at higher resolutions.");
    add_item(1, IT_BOOL, "Close model gaps", &g_cfg.gap_fill, 0, 1, 1,
             "Close the hair-thin gaps between models' parts and where walls meet, which flicker as things move at higher resolutions. Outlines stay as they are.");
    if (texpack_loaded())
        add_item(1, IT_BOOL, "HD textures", &g_cfg.hd_textures, 0, 1, 1,
                 "Draw the game's textures from the HD texture pack next to the game (shows at resolution 2x and up). F2 also switches.");
    add_item(1, IT_BOOL, "Upscale movies", &g_cfg.movie_upscaling, 0, 1, 1,
             "Movies are only 320x240: scale them up to the internal resolution smoothly, softening the compression blocks.");
    for (int a = 0; a < ACT_COUNT; a++)
        add_item(3, IT_KEY, action_labels[a], (void *)(intptr_t)a, 0, 0, 0,
                 "Enter or click: choose a new key (it replaces the first one). Delete: drop the extra keys.");
    add_item(5, IT_BOOL, "God mode", &g_cfg.god_mode, 0, 1, 1, "Your health stays full.");
    add_item(5, IT_BOOL, "Infinite ammo", &g_cfg.infinite_ammo, 0, 1, 1,
             "The magazine never runs down and no ammo is used. Overrides No reload.");
    add_item(5, IT_BOOL, "No reload", &g_cfg.no_reload, 0, 1, 1,
             "The magazine is refilled from your spare ammo as you fire, so you never reload. Ammo is still used up.");
    add_item(5, IT_ACTION, "Give all weapons", (void *)3, 0, 0, 0,
             "Adds every weapon to your inventory, loaded, with plenty of ammo for each.");
    add_item(4, IT_SLOT, "Slot", &save_slot, 1, SAVE_SLOTS, 1,
             "Pick a slot with Left/Right (Shift: 10 at a time), or F6/F7 at any time. F5 saves to it and F9 loads it, even without this panel.");
    add_item(4, IT_ACTION, "Save here", (void *)1, 0, 0, 0, "Save the game exactly as it is now into this slot.");
    add_item(4, IT_ACTION, "Load this slot", (void *)2, 0, 0, 0, "Go back to the moment this slot was saved.");
}

static int first_on_page(int page) {
    for (int i = 0; i < nitems; i++) if (items[i].page == page) return i;
    return 0;
}

static void apply_audio(void) {
    extern int g_audio_master, g_audio_music, g_audio_sfx, g_audio_limiter, g_audio_same_sample;
    g_audio_same_sample = g_cfg.same_sound_limit;
    g_audio_master = g_cfg.volume;
    g_audio_music = g_cfg.music_volume;
    g_audio_sfx = g_cfg.sfx_volume;
    g_audio_limiter = g_cfg.limiter;
}

static void apply_settings(void) {
    apply_audio();
    g_opt_fov = g_cfg.field_of_view;
    { extern int g_opt_movie_upscale; g_opt_movie_upscale = g_cfg.movie_upscaling; g_depth_buffer = g_cfg.depth_buffer; g_gap_fill = g_cfg.gap_fill; { extern int g_brightness, g_persp, g_smooth_tex, g_true_color; g_brightness = g_cfg.brightness; g_persp = g_cfg.perspective; g_smooth_tex = g_cfg.smooth_textures; g_true_color = g_cfg.true_color; } g_hd_textures = g_cfg.hd_textures; g_hd_budget = (size_t)g_cfg.hd_texture_memory << 20; }
    rt_set_look_limit(g_cfg.look_limit);
    g_cheat_god = g_cfg.god_mode;
    g_cheat_infinite_ammo = g_cfg.infinite_ammo;
    g_cheat_no_reload = g_cfg.no_reload;
    g_opt_widescreen = g_cfg.widescreen;
    g_subpixel = g_cfg.subpixel;
    if (!!g_cfg.fullscreen != !!fullscreen) toggle_fullscreen();
    if (render_scale() != g_cfg.resolution_scale) {
        render_set_scale(g_cfg.resolution_scale);
        grab_frame();
    }
    load_bindings();
}

static void value_text(const MenuItem *it, char *out, size_t n) {
    switch (it->type) {
    case IT_FLOAT: snprintf(out, n, "%.1f", *(float *)it->val); break;
    case IT_INT: snprintf(out, n, "%d", *(int *)it->val); break;
    case IT_BOOL: snprintf(out, n, "%s", *(int *)it->val ? "On" : "Off"); break;
    case IT_SCALE: {
        int s = *(int *)it->val;
        snprintf(out, n, "%dx  (%dx%d)", s, 320 * s, 240 * s);
        break;
    }
    case IT_SLOT: {
        char w[48];
        slot_when(*(int *)it->val, w, sizeof w);
        snprintf(out, n, "%d  (%s)", *(int *)it->val, w);
        break;
    }
    case IT_ACTION: out[0] = 0; break;
    case IT_FOV: {
        int v = *(int *)it->val;
        if (v) snprintf(out, n, "%d degrees", v);
        else snprintf(out, n, "Original (61 degrees)");
        break;
    }
    case IT_KEY: {
        int a = (int)(intptr_t)it->val;
        out[0] = 0;
        for (int k = 0; k < g_cfg.nkeys[a]; k++) {
            if (k) strncat(out, ", ", n - strlen(out) - 1);
            strncat(out, g_cfg.keys[a][k], n - strlen(out) - 1);
        }
        if (!g_cfg.nkeys[a]) snprintf(out, n, "(none)");
        break;
    }
    }
}

static void change(int i, int dir) {
    MenuItem *it = &items[i];
    switch (it->type) {
    case IT_FLOAT: {
        float *f = it->val;
        double v = floor(*f / it->step + 0.5) * it->step + dir * it->step;
        if (v < it->lo) v = it->lo;
        if (v > it->hi) v = it->hi;
        *f = (float)v;
        break;
    }
    case IT_ACTION:
        if (dir < 0) return;
        menu_toggle();                       /* close the panel; the game runs on */
        if ((intptr_t)it->val == 1) quick_save();
        else if ((intptr_t)it->val == 2) quick_load();
        else if (game_player_in_control()) { g_give_all_request = 1; show_toast("All weapons added"); }
        else show_toast("Weapons can be given while you're playing");
        return;
    case IT_FOV: {
        int *v = it->val;
        /* 0 (original, 61) then 65 ... 110 */
        int cur = *v ? *v : 60;
        cur += dir * 5;
        if (cur <= 60) *v = 0;
        else *v = cur > 110 ? 110 : cur;
        break;
    }
    case IT_SLOT: {
        int *v = it->val, n = (int)it->hi, d = (GetKeyState(VK_SHIFT) & 0x8000) ? dir * 10 : dir;
        *v = ((*v - 1 + d) % n + n) % n + 1;              /* wraps around */
        break;
    }
    case IT_INT: case IT_SCALE: {
        int *v = it->val;
        *v += dir * (int)it->step;
        if (*v < it->lo) *v = (int)it->lo;
        if (*v > it->hi) *v = (int)it->hi;
        break;
    }
    case IT_BOOL: *(int *)it->val = !*(int *)it->val; break;
    case IT_KEY:
        menu_binding = i;
        menu_bind_armed = 0;                 /* wait for every key to be let go first */
        return;
    }
    apply_settings();
}

static void save_settings(void) {
    char disc[MAX_PATH] = "", extra[MAX_PATH + 32] = "";
    GetPrivateProfileStringA("resurv", "disc", "", disc, sizeof disc, ini_path);
    if (*disc) snprintf(extra, sizeof extra, "[resurv]\ndisc=%s\n", disc);
    if (!config_save(ini_path, extra)) rt_log("settings: couldn't write %s", ini_path);
}

static void menu_toggle(void) {
    if (!menu_open) {
        menu_build();
        menu_open = 1;
        menu_pad_fresh = 1;
        menu_sel = first_on_page(menu_page);
        menu_binding = -1;
        was_captured = captured;
        set_capture(0);
        if (wave) waveOutPause(wave);
    } else {
        menu_open = 0;
        menu_binding = -1;
        save_settings();
        suppress_keys = 1;
        pending_dx = pending_dy = 0;
        if (was_captured) set_capture(1);
        if (wave) waveOutRestart(wave);
        next_frame.QuadPart = 0;
        last_end.QuadPart = 0;
    }
}

/* Waiting for a new key: the first key or mouse button (not the left one,
 * which always fires) pressed after everything was let go. */
static void poll_binding(void) {
    int any = 0;
    for (int vk = 1; vk < 255; vk++) {
        if (vk == VK_LBUTTON || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;
        if (!key_down(vk)) continue;
        any = 1;
        if (!menu_bind_armed) continue;
        if (vk == VK_ESCAPE || vk == VK_F1) { menu_binding = -1; return; }
        const char *name = vk_name(vk);
        if (!name) continue;
        int a = (int)(intptr_t)items[menu_binding].val;
        if (!g_cfg.nkeys[a]) g_cfg.nkeys[a] = 1;
        snprintf(g_cfg.keys[a][0], sizeof g_cfg.keys[a][0], "%s", name);
        /* drop a now-duplicate extra key */
        for (int k = 1; k < g_cfg.nkeys[a]; k++)
            if (!strcmp(g_cfg.keys[a][k], name)) {
                memmove(g_cfg.keys[a][k], g_cfg.keys[a][k + 1], (size_t)(g_cfg.nkeys[a] - k - 1) * sizeof g_cfg.keys[a][0]);
                g_cfg.nkeys[a]--;
                k--;
            }
        load_bindings();
        menu_binding = -1;
        menu_bind_armed = 0;
        return;
    }
    if (!any) menu_bind_armed = 1;
}

static void menu_move(int dir) {
    int i = menu_sel;
    do { i = (i + dir + nitems) % nitems; } while (items[i].page != menu_page);
    menu_sel = i;
}

static void menu_set_page(int p) {
    menu_page = (p + NPAGES) % NPAGES;
    menu_sel = first_on_page(menu_page);
}

/* Window messages while the panel is open. Returns 1 if handled. */
static int menu_message(UINT msg, WPARAM wp, LPARAM lp) {
    POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
    if (menu_binding >= 0) {
        /* keys are picked up by poll_binding; swallow everything else */
        return msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
               msg == WM_MOUSEWHEEL;
    }
    switch (msg) {
    case WM_KEYDOWN:
        switch (wp) {
        case VK_F1: case VK_ESCAPE: menu_toggle(); return 1;
        case VK_UP: case 'W': menu_move(-1); return 1;
        case VK_DOWN: case 'S': menu_move(1); return 1;
        case VK_LEFT: case 'A': if (items[menu_sel].type != IT_KEY && items[menu_sel].type != IT_ACTION) change(menu_sel, -1); return 1;
        case VK_RIGHT: case 'D': if (items[menu_sel].type != IT_KEY && items[menu_sel].type != IT_ACTION) change(menu_sel, 1); return 1;
        case VK_RETURN: case VK_SPACE: change(menu_sel, 1); return 1;
        case VK_TAB: menu_set_page(menu_page + (GetKeyState(VK_SHIFT) < 0 ? -1 : 1)); return 1;
        case VK_PRIOR: menu_set_page(menu_page - 1); return 1;
        case VK_NEXT: menu_set_page(menu_page + 1); return 1;
        case VK_DELETE: case VK_BACK:
            if (items[menu_sel].type == IT_KEY) {
                int a = (int)(intptr_t)items[menu_sel].val;
                if (g_cfg.nkeys[a] > 1) g_cfg.nkeys[a] = 1;
                load_bindings();
            }
            return 1;
        case VK_F11: return 0;
        case VK_F5: menu_toggle(); quick_save(); return 1;
        case VK_F9: menu_toggle(); quick_load(); return 1;
        case VK_F2: toggle_hd_textures(); return 1;
        }
        return 1;
    case WM_MOUSEMOVE:
        for (int i = 0; i < nitems; i++)
            if (items[i].page == menu_page && PtInRect(&item_rect[i], pt)) menu_sel = i;
        return 1;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN:
        for (int p = 0; p < NPAGES; p++)
            if (PtInRect(&tab_rect[p], pt)) { menu_set_page(p); return 1; }
        for (int i = 0; i < nitems; i++)
            if (items[i].page == menu_page && PtInRect(&item_rect[i], pt)) {
                menu_sel = i;
                change(i, msg == WM_LBUTTONDOWN ? 1 : -1);
            }
        return 1;
    case WM_MOUSEWHEEL:
        if (items[menu_sel].type != IT_KEY && items[menu_sel].type != IT_ACTION) change(menu_sel, (short)HIWORD(wp) > 0 ? 1 : -1);
        return 1;
    }
    return 0;
}

static void menu_draw(HDC dc) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = cr.right, ch = cr.bottom;
    if (cw < 64 || ch < 64) return;
    /* sized to fit the tallest page (keys: 15 rows) in the window */
    int fh = ch / 36;
    if (fh < 12) fh = 12;
    int row = fh * 7 / 5, pad = fh;
    int pw = fh * 34, rows = 0;
    for (int i = 0; i < nitems; i++) if (items[i].page == menu_page) rows++;
    int ph = pad + row * 2 + pad / 2 + rows * row + pad + row * 3 + pad;
    if (pw > cw - 8) pw = cw - 8;
    int px = (cw - pw) / 2, py = (ch - ph) / 2;
    if (py < 0) py = 0;

    HFONT font = CreateFontA(-fh, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    HFONT bold = CreateFontA(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    HFONT small = CreateFontA(-fh * 4 / 5, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    HGDIOBJ oldf = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    HBRUSH bg = CreateSolidBrush(RGB(18, 18, 22)), border = CreateSolidBrush(RGB(120, 24, 24));
    HBRUSH selb = CreateSolidBrush(RGB(60, 20, 20)), tabb = CreateSolidBrush(RGB(40, 40, 46));
    RECT panel = {px, py, px + pw, py + ph};
    FillRect(dc, &panel, border);
    RECT inner = {px + 2, py + 2, px + pw - 2, py + ph - 2};
    FillRect(dc, &inner, bg);

    int y = py + pad;
    SelectObject(dc, bold);
    SetTextColor(dc, RGB(230, 230, 230));
    RECT tr = {px + pad, y, px + pw - pad, y + row};
    DrawTextA(dc, "SETTINGS", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, small);
    SetTextColor(dc, RGB(150, 150, 150));
    DrawTextA(dc, "Game paused", -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += row;

    /* tabs */
    SelectObject(dc, font);
    int tw = (pw - 2 * pad) / NPAGES;
    for (int p = 0; p < NPAGES; p++) {
        RECT r = {px + pad + p * tw, y, px + pad + (p + 1) * tw - 4, y + row};
        tab_rect[p] = r;
        FillRect(dc, &r, p == menu_page ? border : tabb);
        SetTextColor(dc, p == menu_page ? RGB(255, 255, 255) : RGB(170, 170, 170));
        DrawTextA(dc, page_names[p], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    y += row + pad / 2;

    for (int i = 0; i < nitems; i++) {
        if (items[i].page != menu_page) { SetRectEmpty(&item_rect[i]); continue; }
        RECT r = {px + pad, y, px + pw - pad, y + row};
        item_rect[i] = r;
        if (i == menu_sel) FillRect(dc, &r, selb);
        RECT lr = {r.left + fh / 2, r.top, r.right, r.bottom};
        SetTextColor(dc, i == menu_sel ? RGB(255, 255, 255) : RGB(200, 200, 200));
        DrawTextA(dc, items[i].label, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        char v[96];
        if (i == menu_binding) snprintf(v, sizeof v, "press a key or button...  (Esc cancels)");
        else {
            value_text(&items[i], v, sizeof v);
            if (i == menu_sel && items[i].type != IT_KEY && items[i].type != IT_ACTION) {
                char w[110];
                snprintf(w, sizeof w, "<  %s  >", v);
                snprintf(v, sizeof v, "%s", w);
            }
        }
        RECT vr = {r.left, r.top, r.right - fh / 2, r.bottom};
        SetTextColor(dc, i == menu_binding ? RGB(255, 200, 80) : i == menu_sel ? RGB(255, 220, 220) : RGB(220, 160, 160));
        DrawTextA(dc, v, -1, &vr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        y += row;
    }
    y += pad;

    SelectObject(dc, small);
    SetTextColor(dc, RGB(170, 170, 170));
    RECT hr = {px + pad, y, px + pw - pad, y + row * 2};
    DrawTextA(dc, items[menu_sel].hint, -1, &hr, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    y += row * 2;
    SetTextColor(dc, RGB(120, 120, 120));
    RECT fr = {px + pad, y, px + pw - pad, y + row};
    DrawTextA(dc, "Up/Down: choose    Left/Right, wheel: change    Tab: page    F1/Esc: close",
              -1, &fr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(dc, oldf);
    DeleteObject(font); DeleteObject(bold); DeleteObject(small);
    DeleteObject(bg); DeleteObject(border); DeleteObject(selb); DeleteObject(tabb);
}

/* The game is paused in here while the panel is open. */
static void menu_pad(void) {
    static WORD prev;
    static DWORD repeat_at;
    pad_update();
    WORD w = pad_ok ? pad_st.Gamepad.wButtons : 0;
    if (pad_ok) {                                /* left stick as the d-pad */
        double lx, ly;
        pad_stick(pad_st.Gamepad.sThumbLX, pad_st.Gamepad.sThumbLY, &lx, &ly, 0);
        if (ly > 0.5) w |= XINPUT_GAMEPAD_DPAD_UP;
        if (ly < -0.5) w |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (lx < -0.5) w |= XINPUT_GAMEPAD_DPAD_LEFT;
        if (lx > 0.5) w |= XINPUT_GAMEPAD_DPAD_RIGHT;
    }
    if (menu_pad_fresh) { prev = w; menu_pad_fresh = 0; return; }
    WORD press = w & ~prev;
    const WORD dirs = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT;
    DWORD now = GetTickCount();
    if (press & dirs) repeat_at = now + 350;
    else if ((w & dirs) && (int32_t)(now - repeat_at) >= 0) { press |= w & dirs; repeat_at = now + 90; }
    prev = w;
    if (!press) return;
    if (menu_binding >= 0) {                     /* picking a key: B cancels */
        if (press & XINPUT_GAMEPAD_B) menu_binding = -1;
        return;
    }
    int adjustable = items[menu_sel].type != IT_KEY && items[menu_sel].type != IT_ACTION;
    if (press & XINPUT_GAMEPAD_DPAD_UP) menu_move(-1);
    if (press & XINPUT_GAMEPAD_DPAD_DOWN) menu_move(1);
    if ((press & XINPUT_GAMEPAD_DPAD_LEFT) && adjustable) change(menu_sel, -1);
    if ((press & XINPUT_GAMEPAD_DPAD_RIGHT) && adjustable) change(menu_sel, 1);
    if (press & XINPUT_GAMEPAD_A) change(menu_sel, 1);
    if (press & XINPUT_GAMEPAD_LEFT_SHOULDER) menu_set_page(menu_page - 1);
    if (press & XINPUT_GAMEPAD_RIGHT_SHOULDER) menu_set_page(menu_page + 1);
    if ((press & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START)) ||
        ((press & (XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB)) &&
         (w & XINPUT_GAMEPAD_LEFT_THUMB) && (w & XINPUT_GAMEPAD_RIGHT_THUMB)))
        menu_toggle();
}

static void menu_run(void) {
    while (menu_open && !quit) {
        pump();
        menu_pad();
        if (!menu_open) break;
        if (menu_binding >= 0 && GetForegroundWindow() == hwnd) poll_binding();
        RECT cr;
        GetClientRect(hwnd, &cr);
        HDC dc = GetDC(hwnd);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right > 0 ? cr.right : 1, cr.bottom > 0 ? cr.bottom : 1);
        HGDIOBJ old = SelectObject(mem, bmp);
        paint(mem);
        menu_draw(mem);
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(hwnd, dc);
        Sleep(16);
    }
}

/* A movie, or a scene the player has no control in: TEST running
 * (0x8006C118 = 6), the player can't act (0x80062DEC), and the status
 * screen isn't open (0x80063B5C). */
static int in_cutscene(void) {
    return g_frame - g_mdec_frame < 8 ||
           (MEM_R32(0x8006C118) == 6 && MEM_R8(0x80062DEC) == 0 && MEM_R16(0x80063B5C) == 0 &&
            MEM_R32(0x800BB19C) != 3);
}

/* Auto resolution: the game falling behind (most frames of the last few
 * seconds finished late) at a resolution above 1x: one step lower, with a
 * message. Never raised again by itself; F1 > Video sets it back. */
static void auto_resolution(int late) {
    static int frames, lates, quiet = 300;
    if (!g_cfg.auto_resolution || menu_open || render_scale() <= 1) { frames = lates = 0; return; }
    if (quiet > 0) { quiet--; return; }                 /* just started or changed */
    frames++;
    lates += late;
    if (frames < 240) return;
    int drop = lates * 10 >= frames * 6;
    rt_log("auto resolution: %d of %d frames late at %dx", lates, frames, render_scale());
    frames = lates = 0;
    if (!drop) return;
    int s = render_scale() - 1;
    g_cfg.resolution_scale = s;
    render_set_scale(s);
    grab_frame();
    save_settings();
    char m[160];
    snprintf(m, sizeof m, "Resolution lowered to %dx to keep the game at full speed (F1 > Video)", s);
    show_toast(m);
    quiet = 600;
}

/* Called by the runtime at every vblank: show the frame, read input, and
 * wait for the next 1/59.94 s tick. */
void plat_frame(void) {
    if (g_frame == 1 || g_frame % 300 == 0)
        rt_log("frame %u (last loop %08X)", g_frame, g_poll_pc);
    /* Fast start: run the boot (disc check, logos, warnings) flat out until
     * just before the title screen, unless a key is pressed. The boot takes
     * the same number of frames on every machine. */
    pad_update();
    {   /* L3 + R3: the settings panel */
        static int was;
        int both = pad_down(XINPUT_GAMEPAD_LEFT_THUMB) && pad_down(XINPUT_GAMEPAD_RIGHT_THUMB);
        if (both && !was && !menu_open) { menu_toggle(); pad_sprint = 0; }
        was = both;
    }
    int fast = g_cfg.skip_intro && g_frame < 1560 && !any_key_pressed;
    /* Space held during a door / room change: run it at full speed. The
     * room-change sequence (func_8009016C) keeps its step in 0x800B3D2C
     * (1-14) while it runs. */
    {
        int16_t step = (int16_t)MEM_R16(0x800B3D2C);
        if (step >= 1 && step <= 14 && ff_held()) fast = 1;
    }
    /* Space held during a cutscene or a movie: fast-forward it. */
    cutscene_frames = in_cutscene() ? cutscene_frames + 1 : 0;
    if (cutscene_frames && ff_held()) fast = 1;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    {   /* how long the game took since the last frame (for the log) */
        if (last_end.QuadPart) {
            double ms = (double)(t0.QuadPart - last_end.QuadPart) * 1000.0 / (double)qpf.QuadPart;
            if (ms > slow_frame_ms) slow_frame_ms = ms;
        }
    }
    /* fast-forwarding: still shown 60 times a second (wall clock), however
     * many game frames that is */
    static LARGE_INTEGER last_paint;
    if (!fast || t0.QuadPart - last_paint.QuadPart >= qpf.QuadPart / 60) {
        last_paint = t0;
        grab_frame();
        HDC dc = GetDC(hwnd);
        paint(dc);
        ReleaseDC(hwnd, dc);
    }
    QueryPerformanceCounter(&t1);
    {
        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)qpf.QuadPart;
        if (ms > slow_paint_ms) slow_paint_ms = ms;
    }
    pump();
    if (menu_open) menu_run();
    if (quit) {
        set_capture(0);
        ExitProcess(0);
    }
    read_input();
    if (fast) {
        int16_t junk[1024 * 2];
        while (spu_take_samples(junk, 1024) > 0) {}
        next_frame.QuadPart = 0;
        last_end.QuadPart = 0;
        return;
    }
    audio_pump();
    /* Pace to the PS1's frame rate (263 lines of 2152 cycles at 33.8688 MHz,
     * about 59.83 Hz), nudged by the audio queue so sound never drifts. */
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONGLONG period = qpf.QuadPart * (263LL * 2152) / CPU_HZ;
    auto_resolution(next_frame.QuadPart && now.QuadPart > next_frame.QuadPart + period / 8);
    int q = audio_queued();
    if (wave && q > AUDIO_TARGET + 2) period += period / 50;
    else if (wave && q < AUDIO_TARGET - 2) period -= period / 50;
    if (next_frame.QuadPart == 0 || now.QuadPart - next_frame.QuadPart > period * 4)
        next_frame = now;                    /* fell far behind: don't try to catch up */
    next_frame.QuadPart += period;
    for (;;) {
        QueryPerformanceCounter(&now);
        LONGLONG left = next_frame.QuadPart - now.QuadPart;
        if (left <= 0) break;
        if (left > qpf.QuadPart / 500) Sleep(1);
    }
    QueryPerformanceCounter(&last_end);
}

int plat_quit(void) { return quit; }

void plat_fatal(const char *msg) {
    char text[768];
    set_capture(0);
    snprintf(text, sizeof text,
             "The game hit something the PC port doesn't handle yet:\n\n%s\n\n"
             "Details are in resurv.log next to the program.", msg);
    MessageBoxA(hwnd, text, "RESPeCt", MB_ICONERROR);
}

/* HD texture pack: see setup_win32.c (a folder next to the exe, or the
 * archive it came in, or where the texture_pack setting says). */
int setup_texture_pack(HWND owner, const char *configured, char *out, size_t n);
static void open_texture_pack(void) {
    char dir[MAX_PATH * 2];
    if (setup_texture_pack(hwnd, g_cfg.texture_pack, dir, sizeof dir)) texpack_open(dir);
}

/* Disc image: see setup_win32.c. */
int setup_find_disc(HWND owner, const char *arg, const char *ini, char *path, size_t n);
static int find_disc(char *path, size_t n, const char *cmdline_arg) {
    char ini[MAX_PATH];
    GetModuleFileNameA(NULL, ini, MAX_PATH);
    char *slash = strrchr(ini, '\\');
    if (slash) strcpy(slash + 1, "resurv.ini");
    return setup_find_disc(hwnd, cmdline_arg, ini, path, n);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev;
    char disc[MAX_PATH];
    char arg[MAX_PATH] = "";
    if (cmd && *cmd) {
        snprintf(arg, sizeof arg, "%s", cmd);
        if (arg[0] == '"') { memmove(arg, arg + 1, strlen(arg)); char *q = strchr(arg, '"'); if (q) *q = 0; }
    }
    char logpath[MAX_PATH];
    GetModuleFileNameA(NULL, logpath, MAX_PATH);
    char *slash = strrchr(logpath, '\\');
    if (slash) strcpy(slash + 1, "resurv.log");
    FILE *lf = fopen(logpath, "w");
    rt_set_log(lf ? lf : stderr);

    {
        char ini[MAX_PATH];
        GetModuleFileNameA(NULL, ini, MAX_PATH);
        char *sl = strrchr(ini, '\\');
        if (sl) strcpy(sl + 1, "resurv.ini");
        snprintf(ini_path, sizeof ini_path, "%s", ini);
        int fresh = GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES;
        config_load(ini);
        if (fresh) {
            /* First start: a resolution this CPU can draw (it draws every
             * frame; the graphics card doesn't help). */
            SYSTEM_INFO si0;
            GetSystemInfo(&si0);
            int n = (int)si0.dwNumberOfProcessors;
            g_cfg.resolution_scale = n >= 16 ? 4 : n >= 8 ? 3 : n >= 4 ? 2 : 1;
            rt_log("settings: first start, %d threads: resolution %dx", n, g_cfg.resolution_scale);
            save_settings();
        }
        load_bindings();
        rt_set_look_limit(g_cfg.look_limit);
        g_cheat_god = g_cfg.god_mode;
        g_cheat_infinite_ammo = g_cfg.infinite_ammo;
        g_cheat_no_reload = g_cfg.no_reload;
        g_opt_fov = g_cfg.field_of_view;
    { extern int g_opt_movie_upscale; g_opt_movie_upscale = g_cfg.movie_upscaling; g_depth_buffer = g_cfg.depth_buffer; g_gap_fill = g_cfg.gap_fill; { extern int g_brightness, g_persp, g_smooth_tex, g_true_color; g_brightness = g_cfg.brightness; g_persp = g_cfg.perspective; g_smooth_tex = g_cfg.smooth_textures; g_true_color = g_cfg.true_color; } g_hd_textures = g_cfg.hd_textures; g_hd_budget = (size_t)g_cfg.hd_texture_memory << 20; }
        apply_audio();
        games_dir();
        pick_latest_slot();
        extern int g_opt_scale, g_opt_threads;
        g_opt_scale = g_cfg.resolution_scale;
        { extern int g_gpu; g_gpu = g_cfg.graphics_card; }
        g_opt_widescreen = g_cfg.widescreen;
        g_subpixel = g_cfg.subpixel;
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        g_opt_threads = (int)si.dwNumberOfProcessors;
        frame_buf = calloc((size_t)2560 * 1920, sizeof *frame_buf);
    }
    SetUnhandledExceptionFilter(crash_handler);
    coro_init();
    SetProcessDPIAware();
    timeBeginPeriod(1);
    QueryPerformanceFrequency(&qpf);

    WNDCLASSA wc = {0};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
    wc.lpszClassName = "resurv";
    RegisterClassA(&wc);
    RECT r = {0, 0, 1280, 960};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowA("resurv", "RESPeCt - Resident Evil Survivor", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                         r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(hwnd, show);
    if (g_cfg.fullscreen) toggle_fullscreen();

    RAWINPUTDEVICE rid = {0x01, 0x02, 0, hwnd};          /* generic desktop, mouse */
    RegisterRawInputDevices(&rid, 1, sizeof rid);

    if (!find_disc(disc, sizeof disc, arg)) return 0;
    {
        /* memory cards live next to the exe */
        static char dir[MAX_PATH];
        GetModuleFileNameA(NULL, dir, MAX_PATH);
        char *sl = strrchr(dir, '\\');
        if (sl) sl[1] = 0;
        card_set_dir(dir);
    }
    open_texture_pack();
    audio_init();
    if (!rt_boot(disc)) {
        char msg[512];
        snprintf(msg, sizeof msg, "Couldn't start the game from\n%s", disc);
        MessageBoxA(hwnd, msg, "RESPeCt", MB_ICONERROR);
        return 1;
    }
    return 0;
}
