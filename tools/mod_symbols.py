"""port/include/respect_game.h: every function of the game that RESPeCt runs as
C (port/decomp), by name, for plugins.

    python tools/mod_symbols.py

Reads port/meta/meta.c (the runtime's function tables). The overlays share
addresses: an enemy program's functions are in program slot A (0x800F8000)
or B (0x80100000), a stage program's at 0x800F2C50; the comment says which
module a function belongs to, and a plugin hooking one should check that its
enemy or stage is the one loaded (see port/plugins/zombie_speed.c)."""
import re, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
META = os.path.join(ROOT, 'port', 'meta', 'meta.c')
OUT = os.path.join(ROOT, 'port', 'include', 'respect_game.h')
WHERE = {'kernel': 'kernel (always loaded)', 'test': 'TEST.BIN (the game, always loaded in play)',
         'title': 'title screen', 'typew': 'typewriter (story text)', 'score': 'results screen',
         'staff': 'credits', 'option': 'options', 'debug': 'debug menu'}


def main():
    text = open(META, encoding='utf-8', errors='replace').read()
    # module name for each tab_<module> table
    rows, seen = [], set()
    for m in re.finditer(r'static const RtFunc tab_(\w+)\[\] = \{(.*?)\n\};', text, re.S):
        module = m.group(1)
        for a, fn in re.findall(r'\{0x([0-9A-F]{8})u, (n_\w+),', m.group(2)):
            if fn in seen:
                continue
            seen.add(fn)
            rows.append((module, int(a, 16), fn[2:]))
    decomp = {}
    for f in os.listdir(os.path.join(ROOT, 'port', 'decomp')):
        if f.endswith('.c'):
            for fn in re.findall(r'^void (n_\w+)\(void\) \{', open(os.path.join(ROOT, 'port', 'decomp', f), encoding='utf-8',
                                                                    errors='replace').read(), re.M):
                decomp[fn[2:]] = f
    with open(OUT, 'w', newline='\n') as o:
        o.write('/* The game\'s functions RESPeCt runs as C, by name, for plugins (hook them\n'
                ' * with api->hook, call them with api->call). Written by tools/mod_symbols.py\n'
                ' * from port/meta/meta.c; what each does is in its port/decomp file (named\n'
                ' * in the comment). Arguments come in A0-A3, the result in V0.\n'
                ' * Overlays share addresses: enemy programs load at slot A (0x800F8000) or\n'
                ' * B (0x80100000), stage programs at 0x800F2C50. A hook there runs for\n'
                ' * whatever program is loaded: check the enemy\'s type (+0x315) or the stage. */\n'
                '#ifndef RESPECT_GAME_H\n#define RESPECT_GAME_H\n\n')
        last = None
        for module, a, name in sorted(rows, key=lambda r: (r[0] != 'kernel', r[0] != 'test', r[0], r[1])):
            if module != last:
                where = WHERE.get(module, 'overlay %s' % module)
                if module.startswith('ovl'):
                    n = int(module[3:])
                    where = ('enemy program, slot A' if 34 <= n <= 48 else 'enemy program, slot B' if 50 <= n <= 64
                             else 'stage program' if n < 34 else 'overlay') + ' (%s)' % module
                o.write('\n/* %s */\n' % where)
                last = module
            o.write('#define RG_%-40s 0x%08Xu   /* %s */\n' % (name, a, decomp.get(name, '')))
        o.write('\n#endif\n')
    print('%d functions written to %s' % (len(rows), OUT))


if __name__ == '__main__':
    main()
