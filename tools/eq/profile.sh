#!/bin/bash
# Which recompiled game functions the eq scenarios run, and how often:
# builds a copy of the port that counts calls of every generated f_*
# function, runs the scenarios (run.sh, both sides the same binary) and
# writes WORKDIR/profile.txt: calls, function, instructions (sorted by
# module and address), and "LIVE n" for a function that was being run when
# the game polled where a save state can be made, n the most frames it had
# been running then. A save state waits while native code is running, so a
# function that runs for many frames (the main loop, a loading screen) must
# stay translated. Functions already decompiled (native.txt) are left
# out, as they aren't generated.
# usage: profile.sh WORKDIR [scenario...]
set -u
W=$(realpath -m "$1"); shift
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
rm -rf "$W/src"; mkdir -p "$W/src"; cp -r "$ROOT/port/." "$W/src/"; rm -rf "$W/src/build" "$W/src/run"
python3 - "$W/src" <<'PY'
import re, glob, sys
d = sys.argv[1]
# count calls; keep a host-side list of the game functions being run (a
# node on each one's C stack frame, popped by a cleanup on any return)
for p in glob.glob(d + '/gen/*.c'):
    if p.endswith('funcs.c'): continue
    s = open(p).read()
    s = re.sub(r'\n(void (f_\w+)\(void\) \{)', lambda m: '\n' + m.group(1) +
               ' static unsigned c_[2]; if (c_[0]++ == 0) prof_reg(c_, "' + m.group(2) + '");'
               ' __attribute__((cleanup(prof_pop))) ProfNode pn_ = {c_, prof_top, g_frame}; prof_top = &pn_;', s)
    s = ('typedef struct ProfNode { unsigned *c; struct ProfNode *prev; unsigned f0; } ProfNode;\n'
         'extern unsigned g_frame; extern ProfNode *prof_top; void prof_reg(unsigned *, const char *);\n'
         'static inline void prof_pop(ProfNode *n) { prof_top = n->prev; }\n') + s
    open(p, 'w').write(s)
# the list is per coroutine (game threads, the interrupt handler)
p = d + '/src/thread.c'; s = open(p).read()
s = s.replace('    from->native_depth = g_native_depth;\n',
              '    from->native_depth = g_native_depth;\n    { extern void *prof_top; static void *pt[64]; pt[current] = prof_top; prof_top = to->started ? pt[i] : 0; }\n')
open(p, 'w').write(s)
p = d + '/src/rt.c'; s = open(p).read()
s = s.replace('    coro_irq_run(bios_exception);\n',
              '    { extern void *prof_top; void *pt = prof_top; prof_top = 0; coro_irq_run(bios_exception); prof_top = pt; }\n')
# at a poll where a save state could be made, mark every function being run
s = s.replace('    if (g_ss_pending) ss_service();\n}',
              '    if (g_ss_pending) ss_service();\n    { extern int g_hle_depth; void prof_poll(void); if (!(here & 3) && !rt_in_interrupt() && !g_hle_depth) prof_poll(); }\n}')
open(p, 'w').write(s)
open(d + '/src/prof.c', 'w').write(r'''
#include <stdio.h>
#include <stdlib.h>
typedef struct ProfNode { unsigned *c; struct ProfNode *prev; unsigned f0; } ProfNode;
ProfNode *prof_top;
extern unsigned g_frame;
static struct { unsigned *c; const char *n; } t[20000]; static int nt;
static void dump(void) {
    const char *o = getenv("RT_PROFILE_OUT"); FILE *f = fopen(o ? o : "profile.txt", "w");
    if (!f) return;
    for (int i = 0; i < nt; i++) fprintf(f, "%u %u %s\n", t[i].c[0], t[i].c[1], t[i].n);
    fclose(f);
}
void prof_reg(unsigned *c, const char *n) { if (!nt) atexit(dump); if (nt < 20000) { t[nt].c = c; t[nt].n = n; nt++; } }
/* the longest a function has been running (in frames) at a poll */
void prof_poll(void) { for (ProfNode *p = prof_top; p; p = p->prev) if (g_frame - p->f0 + 1 > p->c[1]) p->c[1] = g_frame - p->f0 + 1; }
''')
PY
make -C "$W/src" -j2 >/dev/null 2>&1 || { echo "build failed"; exit 1; }
B="$W/src/build/linux/resurv_headless"
"$ROOT/tools/eq/run.sh" "$B" "$B" "$W/runs" "$@" > /dev/null 2>&1
python3 - "$W" "$ROOT" <<'PY'
import glob, sys, collections, re, os
w, root = sys.argv[1], sys.argv[2]
tot = collections.Counter(); live = {}
for p in glob.glob(w + '/runs/*/base/profile.txt'):
    for line in open(p):
        c, p, n = line.split(); tot[n] += int(c)
        live[n] = max(live.get(n, 0), int(p))
# instruction counts from the asm
size = {}
for p in glob.glob(root + '/asm/**/*.s', recursive=True):
    cur = None; start = False
    for line in open(p, errors='replace'):
        if line.startswith('glabel '): start = True; continue
        m = re.match(r'\s*/\* [0-9A-F]+ ([0-9A-F]{8}) [0-9A-F]{8} \*/', line)
        if m and start: cur = m.group(1); size[cur] = 0; start = False
        if m and cur: size[cur] += 1
        if line.startswith('endlabel'): cur = None
with open(w + '/profile.txt', 'w') as f:
    for n in sorted(tot):
        a = n.split('_')[-1]
        f.write(f"{tot[n]:10d} {n} {size.get(a, '?')}{(' LIVE %d' % live[n]) if live.get(n) else ''}\n")
print(len(tot), "functions run; see", w + '/profile.txt')
PY
rm -rf "$W/runs" "$W/src"
