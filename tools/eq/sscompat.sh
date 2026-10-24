#!/bin/bash
# Save states made by one build must load in another: decompiled functions
# keep their translation for continuing a save state (RtFunc.resume).
# Makes save states with BASE at points where the game is loading or
# switching (door, game over, payphone save screen, loading a saved game:
# the times functions are waiting on the call chain), then continues each
# with BASE and with NEW for 150 frames and compares RAM.
# usage: sscompat.sh BASE NEW WORKDIR
set -u
A=$(realpath "$1"); B=$(realpath "$2"); W=$(realpath -m "$3"); mkdir -p "$W"
DISC=${DISC:-/root/resurv/disc/image/survivor.bin}
HERE=$(cd "$(dirname "$0")" && pwd)
SAVES=${SAVES:-$(cd "$HERE/../.." && pwd)/testdata}
declare -A ENV ARGS AT
ENV[door]="RT_LOAD=$SAVES/slot5.sav RT_MOUSE=4,0,18800,18860"
ARGS[door]="--press 18800:40:150 --press 19000:10:1400"; AT[door]="19320 19350 19372 19390 19425 19600 19803"
DP=""; for f in $(seq 9600 40 10400); do DP="$DP --press $f:4000:4"; done
ENV[death]="RT_LOAD=$SAVES/slot1.sav RT_POKE16=8530:800A8584=1 RT_MOUSE=0,0,0,1"
ARGS[death]="$DP"; AT[death]="8945 8982 9200 9300 9620 9700 9820 9870 10000"
PP=""; for f in $(seq 18900 60 19800); do PP="$PP --press $f:4000:4"; done
ENV[phone]="RT_GDIR=. RT_GUSE_AT=18790 RT_LOAD=$SAVES/slot5.sav RT_MOUSE=0,0,0,1"
ARGS[phone]="$PP"; AT[phone]="18795 18900 19100 19300 19380 19425 19438 19445 19600"
ENV[gload]="RT_GDIR=$SAVES RT_GLOAD_AT=1700:1 RT_MOUSE=0,0,0,1"
ARGS[gload]=""; AT[gload]="1705 1790 1835 1850 1858 1900 2100"
n=0; bad=0
for s in door death phone gload; do
  for f in ${AT[$s]}; do
    for k in 0 3000; do
      D=$W/$s-$f-$k; rm -rf $D; mkdir -p $D/base $D/new
      ( cd $D && env ${ENV[$s]} RT_SAVE_SKIP=$k RT_SAVE_AT=$f:$D/state.ss timeout 600 $A $DISC --frames $((f + 200)) --out . ${ARGS[$s]} > /dev/null 2> make.txt )
      [ -f $D/state.ss ] || { echo "$s $f: no save state made"; bad=$((bad+1)); continue; }
      at=$(grep -o "saved .* at frame [0-9]*" $D/make.txt | grep -o "[0-9]*$")
      for side in base new; do
        BIN=$A; [ $side = new ] && BIN=$B
        ( cd $D/$side && env RT_SS_DEBUG=1 RT_LOAD=$D/state.ss RT_MOUSE=0,0,0,1 RT_RAMDUMP=50:. timeout 600 $BIN $DISC --frames $((at + 150)) --out . > /dev/null 2> log.txt ) &
      done
      wait
      n=$((n+1))
      r=$(python3 "$HERE/cmp.py" $D/base $D/new)
      grep -o "continuing at [0-9A-F]*" $D/base/log.txt | tail -n +2 | tr "\n" " " >> $W/chains.txt; echo >> $W/chains.txt
      if echo "$r" | grep -q "first difference: none" && ! grep -q FATAL $D/new/log.txt; then :; else
        echo "$s at $f (skip $k): DIFFERENT"; echo "$r"; grep FATAL $D/*/log.txt; bad=$((bad+1)); continue; fi
      rm -f $D/*/r*.bin
    done
  done
done
echo "$n save states continued, $bad problems"
