#!/bin/bash
# Save states made by NEW during the intro scenario: is one made close to
# the frame asked, and do NEW and BASE continue it exactly as NEW's own
# uninterrupted run goes on?
# usage: ssmake.sh BASE NEW WORKDIR [frames...]
set -u
# The original build has no Close-up 3D change to the split threshold (rt.c),
export RT_KEEP_SPLIT=1
# nor the whole-motion mouse turning (port/decomp/player.c).
export RT_PAD_LOOK=1
A=$(realpath "$1"); B=$(realpath "$2"); W=$(realpath -m "$3"); shift 3; mkdir -p "$W"
DISC=${DISC:-$(cd "$(dirname "$0")/../.." && pwd)/disc/image/survivor.bin}; HERE=$(cd "$(dirname "$0")" && pwd)
P=""; for f in $(seq 1600 90 3400); do P="$P --press $f:8:4"; done
P="$P --press 3600:4000:4 --press 3630:4000:4 --press 3660:8:4"
AT=${*:-600 1000 2300 3500 3800 4500 5200}
bad=0
for f in $AT; do
  D=$W/m$f; rm -rf $D; mkdir -p $D/ref $D/base $D/new
  ( cd $D/ref && env RT_SAVE_AT=$f:$D/state.ss RT_RAMDUMP=50:. timeout 900 $B $DISC --frames $((f + 300)) --out . $P > /dev/null 2> log.txt )
  [ -f $D/state.ss ] || { echo "$f: no save state made in 300 frames"; bad=$((bad+1)); continue; }
  at=$(grep -o "saved .* at frame [0-9]*" $D/ref/log.txt | grep -o "[0-9]*$")
  for side in base new; do
    BIN=$A; [ $side = new ] && BIN=$B
    ( cd $D/$side && env RT_LOAD=$D/state.ss RT_RAMDUMP=50:. timeout 900 $BIN $DISC --frames $((f + 300)) --out . $P > /dev/null 2> log.txt ) &
  done
  wait
  rb=$(python3 $HERE/cmp.py $D/base $D/ref | tail -1); rn=$(python3 $HERE/cmp.py $D/new $D/ref | tail -1)
  if echo "$rb$rn" | grep -q "first difference: [^n]\|FATAL"; then echo "$f (saved at $at): base:$rb new:$rn"; bad=$((bad+1)); else echo "$f (saved at $at) ok"; rm -f $D/*/r*.bin $D/*/f*.png; fi
done
echo "$bad problems"
