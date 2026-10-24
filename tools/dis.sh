#!/bin/bash
# dis.sh [-m FILE.s] ADDR... : print functions compactly (address, then the
# instruction; labels kept). Default file asm/test/test.s; kernel code is in
# asm/1300.s, overlays in asm/stagprog/ and asm/enemprog/.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
F=$ROOT/asm/test/test.s
if [ "${1:-}" = -m ]; then F=$2; shift 2; fi
for f in "$@"; do
  # an address (func_ADDR, func_ADDR_ovlNN, or a named function starting
  # there) or a function name
  if [[ $f =~ ^[0-9A-Fa-f]{8}$ ]]; then
    f=${f^^}
    lab=$(grep -n -E "^glabel func_${f}(_[a-z0-9]+)?\$" "$F" | head -1 | cut -d: -f1)
    if [ -z "$lab" ]; then
      i=$(grep -n -E "^ */\* [0-9A-F]+ $f [0-9A-F]{8} \*/" "$F" | head -1 | cut -d: -f1)
      [ -n "$i" ] && lab=$(head -n "$i" "$F" | grep -n "^glabel " | tail -1 | cut -d: -f1)
    fi
  else
    lab=$(grep -n -E "^glabel $f\$" "$F" | head -1 | cut -d: -f1)
  fi
  [ -z "$lab" ] && { echo "$f not in $F"; continue; }
  name=$(sed -n "${lab}p" "$F" | awk '{print $2}')
  s=$lab
  e=$(tail -n +"$lab" "$F" | grep -n -m1 "^endlabel $name\$" | cut -d: -f1); e=$((s + e - 1))
  echo "==== $name ($(( (e - s) )) lines)"
  body=$(sed -n "${s},${e}p" "$F" | sed -E 's|^ */\* [0-9A-F]+ ([0-9A-F]{8}) [0-9A-F]{8} \*/ *|\1 |' | grep -v "^$" | grep -v "glabel\|endlabel")
  echo "$body"
  # jump tables used (in the module's data files)
  for j in $(echo "$body" | grep -o "jtbl_[0-9A-F]*\(_[a-z0-9]*\)\?" | sort -u); do
    echo "  $j:"; grep -h -A64 "dlabel $j\$" "$(dirname "$F")"/data/*.s "$ROOT"/asm/data/*.s 2>/dev/null | sed -n '2,/enddlabel/p' | grep -o "\.L[0-9A-F]*" | tr '\n' ' '; echo
  done
done
