#!/bin/bash
# Which decompiled functions (tools/recomp/native.txt) the eq scenarios
# reach: builds a copy of the port that logs the first call of each n_*
# function, runs run.sh with it, lists the ones never called.
# usage: coverage.sh BASE_BINARY WORKDIR
set -u
BASE=$(realpath "$1"); W=$(realpath -m "$2"); mkdir -p "$W"
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
rm -rf "$W/src"; mkdir -p "$W/src"; cp -r "$ROOT/port/." "$W/src/"; rm -rf "$W/src/build"
python3 - "$W/src/decomp" <<'PY'
import re, glob, sys
for p in glob.glob(sys.argv[1] + '/*.c'):
    s = open(p).read()
    s = re.sub(r'(\nvoid (n_\w+)\(void\) \{)', lambda m: m.group(1) + ' { static int c_; if (!c_) { c_ = 1; extern void rt_log(const char *, ...); rt_log("COV %s", "' + m.group(2) + '"); } }', s)
    open(p, 'w').write(s)
PY
make -C "$W/src" -j16 >/dev/null 2>&1 || { echo "build failed"; exit 1; }
"$ROOT/tools/eq/run.sh" "$BASE" "$W/src/build/linux/resurv_headless" "$W/runs" > /dev/null 2>&1
cat "$W"/runs/*/new/log.txt | grep -o "COV n_[a-zA-Z0-9_]*" | sed 's/COV //' | sort -u > "$W/covered.txt"
awk '!/^#/ && NF == 3 && $3 !~ /^~/ {print "n_" $3}' "$ROOT/tools/recomp/native.txt" | sort > "$W/all.txt"
echo "not reached by the scenarios:"; comm -23 "$W/all.txt" "$W/covered.txt"
rm -rf "$W/runs" "$W/src/gen" "$W/src/build"
