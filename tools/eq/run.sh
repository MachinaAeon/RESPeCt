#!/bin/bash
# Differential test: run scenarios with two builds and compare RAM every
# 50 frames.  usage: run.sh BIN_BASE BIN_NEW OUTDIR [scenario...]
# Scenarios replay fixed input from a new game (needs the disc image).
set -u
A=$(realpath $1); B=$(realpath $2); OUT=$3; shift 3
DISC=${DISC:-/root/resurv/disc/image/survivor.bin}
HERE=$(cd "$(dirname "$0")" && pwd)
P=""; for f in $(seq 1600 90 3400); do P="$P --press $f:8:4"; done
P="$P --press 3600:4000:4 --press 3630:4000:4 --press 3660:8:4"   # title -> new game -> play
declare -A ENV ARGS FRAMES
ENV[intro]="";                                   ARGS[intro]="";  FRAMES[intro]=6000
ENV[weapons]="RT_CHEATS=ammo RT_GIVE_AT=3850 RT_WEAPON_AT=3900:2,3960:3,4020:4,4080:5,4140:6,4200:7,4260:8,4320:9,4380:1,4440:10,4500:11,4560:9,4620:5,4680:9,4740:1 RT_CLICK=3905,4800 RT_CLICK_EVERY=6 RT_MOUSE=3,0,3905,4800"
ARGS[weapons]=""; FRAMES[weapons]=4800
# walk up the alley turning and shooting, getting into fights
ENV[walk]="RT_CLICK=3700,5200 RT_CLICK_EVERY=9 RT_MOUSE=2,1,3700,5200"
ARGS[walk]="--press 3700:10:1500"; FRAMES[walk]=5200
# status screen: open, Item, down to Handgun C, Equip, close, then fire
ENV[menu]="RT_CHEATS=ammo RT_GIVE_AT=3850 RT_CLICK=4300,4500 RT_CLICK_EVERY=8 RT_MOUSE=0,0,0,1"
ARGS[menu]="--press 3900:8:4 --press 3960:4000:4 --press 4000:40:4 --press 4030:40:4 --press 4060:4000:4 --press 4100:4000:4 --press 4150:8:4"; FRAMES[menu]=4500
# a 20-round ammo box: it runs out (removed from the inventory), then the gun is empty
ENV[ammo]="RT_POKE16=3700:800AF80A=14 RT_CLICK=3700,5000 RT_CLICK_EVERY=9 RT_MOUSE=2,1,3700,5000"
ARGS[ammo]="--press 3700:10:1300"; FRAMES[ammo]=5000
# grenade launcher (rounds from its spare), then the shotgun with no shells
ENV[launcher]="RT_GIVE_AT=3850 RT_WEAPON_AT=3900:9,4600:6 RT_POKE16=4590:800AF83A=0 RT_CLICK=3905,5000 RT_CLICK_EVERY=10 RT_MOUSE=1,0,3905,5000"
ARGS[launcher]=""; FRAMES[launcher]=5000
# every weapon without cheats, long enough to empty and reload each
ENV[reloads]="RT_GIVE_AT=3850 RT_WEAPON_AT=3900:2,4100:3,4300:4,4500:5,4700:6,4900:7,5100:8,5300:9,5500:1 RT_CLICK=3905,5700 RT_CLICK_EVERY=7 RT_MOUSE=1,0,3905,5700"
ARGS[reloads]=""; FRAMES[reloads]=5700
# magnum then shotgun, long: their reloads and the shotgun's shells dropping
ENV[heavy]="RT_GIVE_AT=3850 RT_WEAPON_AT=3900:5,4600:6 RT_POKE16=4590:800AF82A=0 RT_CLICK=3905,5400 RT_CLICK_EVERY=6 RT_MOUSE=1,0,3905,5400"
ARGS[heavy]=""; FRAMES[heavy]=5400
# Fights, from save states (not in git: put slot1.sav, first zombie in the
# alley, and slot2.sav, three zombies in the kitchen, in $SAVES)
SAVES=${SAVES:-$(cd "$HERE/../.." && pwd)/testdata}
ENV[fight1]="RT_LOAD=$SAVES/slot1.sav RT_CLICK=8530,9300 RT_CLICK_EVERY=12 RT_MOUSE=0,0,0,1"
ARGS[fight1]=""; FRAMES[fight1]=9300
ENV[hurt1]="RT_LOAD=$SAVES/slot1.sav RT_MOUSE=0,0,0,1"
ARGS[hurt1]=""; FRAMES[hurt1]=9500
ENV[fight2]="RT_LOAD=$SAVES/slot2.sav RT_CLICK=9640,10600 RT_CLICK_EVERY=10 RT_MOUSE=1,0,9700,10600"
ARGS[fight2]="--press 10200:10:120"; FRAMES[fight2]=10600
# the first zombie with the shotgun (close), then a handgun; and grenades
ENV[fight1s]="RT_LOAD=$SAVES/slot1.sav RT_GIVE_AT=8520 RT_WEAPON_AT=8525:6,8800:2 RT_CLICK=8700,9300 RT_CLICK_EVERY=12 RT_MOUSE=0,0,0,1"
ARGS[fight1s]=""; FRAMES[fight1s]=9300
ENV[fight1g]="RT_LOAD=$SAVES/slot1.sav RT_GIVE_AT=8520 RT_WEAPON_AT=8525:9 RT_CLICK=8530,9300 RT_CLICK_EVERY=20 RT_MOUSE=0,0,0,1"
ARGS[fight1g]=""; FRAMES[fight1g]=9300
# Game flow (slot5.sav: at the first payphone, after its call; game1.sav:
# a saved game made there with RT_GSAVE_AT=...:1):
# through the street door into the next room
ENV[door]="RT_LOAD=$SAVES/slot5.sav RT_MOUSE=4,0,18800,18860"
ARGS[door]="--press 18800:40:150 --press 19000:10:1400"; FRAMES[door]=20000
# killed by the first zombie, game over, continue
DP=""; for f in $(seq 9600 40 10400); do DP="$DP --press $f:4000:4"; done
ENV[death]="RT_LOAD=$SAVES/slot1.sav RT_POKE16=8530:800A8584=1 RT_MOUSE=0,0,0,1"
ARGS[death]="$DP"; FRAMES[death]=10500
# save at the payphone (the save screen, a memory card save), then play on
PP=""; for f in $(seq 18900 60 19800); do PP="$PP --press $f:4000:4"; done
ENV[phone]="RT_GDIR=. RT_GUSE_AT=18790 RT_LOAD=$SAVES/slot5.sav RT_MOUSE=0,0,0,1"
ARGS[phone]="$PP"; FRAMES[phone]=20000
# load a saved game from the title screen
ENV[gload]="RT_GDIR=$SAVES RT_GLOAD_AT=1700:1 RT_MOUSE=0,0,0,1"
ARGS[gload]="--press 2300:10:300"; FRAMES[gload]=2800
# Other areas (areaN.sav: save states around the game, stage/room in the
# name below): walk forward turning and shooting, 1500 frames
ENV[area6]="RT_LOAD=$SAVES/area6.sav RT_CLICK=7756,9246 RT_CLICK_EVERY=9 RT_MOUSE=2,1,7756,9246"
ARGS[area6]="--press 7756:10:700 --press 8556:40:300"; FRAMES[area6]=9246
ENV[area7]="RT_LOAD=$SAVES/area7.sav RT_CLICK=29202,30692 RT_CLICK_EVERY=9 RT_MOUSE=2,1,29202,30692"
ARGS[area7]="--press 29202:10:700 --press 30002:40:300"; FRAMES[area7]=30692
ENV[area8]="RT_LOAD=$SAVES/area8.sav RT_CLICK=80653,82143 RT_CLICK_EVERY=9 RT_MOUSE=2,1,80653,82143"
ARGS[area8]="--press 80653:10:700 --press 81453:40:300"; FRAMES[area8]=82143
ENV[area9]="RT_LOAD=$SAVES/area9.sav RT_CLICK=110267,111757 RT_CLICK_EVERY=9 RT_MOUSE=2,1,110267,111757"
ARGS[area9]="--press 110267:10:700 --press 111067:40:300"; FRAMES[area9]=111757
ENV[area10]="RT_LOAD=$SAVES/area10.sav RT_CLICK=21213,22703 RT_CLICK_EVERY=9 RT_MOUSE=2,1,21213,22703"
ARGS[area10]="--press 21213:10:700 --press 22013:40:300"; FRAMES[area10]=22703
ENV[area11]="RT_LOAD=$SAVES/area11.sav RT_CLICK=53407,54897 RT_CLICK_EVERY=9 RT_MOUSE=2,1,53407,54897"
ARGS[area11]="--press 53407:10:700 --press 54207:40:300"; FRAMES[area11]=54897
ENV[area12]="RT_LOAD=$SAVES/area12.sav RT_CLICK=60570,62060 RT_CLICK_EVERY=9 RT_MOUSE=2,1,60570,62060"
ARGS[area12]="--press 60570:10:700 --press 61370:40:300"; FRAMES[area12]=62060
ENV[area13]="RT_LOAD=$SAVES/area13.sav RT_CLICK=63415,64905 RT_CLICK_EVERY=9 RT_MOUSE=2,1,63415,64905"
ARGS[area13]="--press 63415:10:700 --press 64215:40:300"; FRAMES[area13]=64905
ENV[area14]="RT_LOAD=$SAVES/area14.sav RT_CLICK=128107,129597 RT_CLICK_EVERY=9 RT_MOUSE=2,1,128107,129597"
ARGS[area14]="--press 128107:10:700 --press 128907:40:300"; FRAMES[area14]=129597
ENV[area19]="RT_LOAD=$SAVES/area19.sav RT_CLICK=19737,21227 RT_CLICK_EVERY=9 RT_MOUSE=2,1,19737,21227"
ARGS[area19]="--press 19737:10:700 --press 20537:40:300"; FRAMES[area19]=21227
ENV[area21]="RT_LOAD=$SAVES/area21.sav RT_CLICK=35311,36801 RT_CLICK_EVERY=9 RT_MOUSE=2,1,35311,36801"
ARGS[area21]="--press 35311:10:700 --press 36111:40:300"; FRAMES[area21]=36801
ENV[area22]="RT_LOAD=$SAVES/area22.sav RT_CLICK=20975,22465 RT_CLICK_EVERY=9 RT_MOUSE=2,1,20975,22465"
ARGS[area22]="--press 20975:10:700 --press 21775:40:300"; FRAMES[area22]=22465
ENV[area23]="RT_LOAD=$SAVES/area23.sav RT_CLICK=42063,43553 RT_CLICK_EVERY=9 RT_MOUSE=2,1,42063,43553"
ARGS[area23]="--press 42063:10:700 --press 42863:40:300"; FRAMES[area23]=43553
ENV[area24]="RT_LOAD=$SAVES/area24.sav RT_CLICK=48772,50262 RT_CLICK_EVERY=9 RT_MOUSE=2,1,48772,50262"
ARGS[area24]="--press 48772:10:700 --press 49572:40:300"; FRAMES[area24]=50262
ENV[area25]="RT_LOAD=$SAVES/area25.sav RT_CLICK=57107,58597 RT_CLICK_EVERY=9 RT_MOUSE=2,1,57107,58597"
ARGS[area25]="--press 57107:10:700 --press 57907:40:300"; FRAMES[area25]=58597
ENV[area98]="RT_LOAD=$SAVES/area98.sav RT_CLICK=36774,38264 RT_CLICK_EVERY=9 RT_MOUSE=2,1,36774,38264"
ARGS[area98]="--press 36774:10:700 --press 37574:40:300"; FRAMES[area98]=38264
ENV[area99]="RT_LOAD=$SAVES/area99.sav RT_CLICK=9915,11405 RT_CLICK_EVERY=9 RT_MOUSE=2,1,9915,11405"
ARGS[area99]="--press 9915:10:700 --press 10715:40:300"; FRAMES[area99]=11405
# bosses and later areas: Tyrant / Mr. X, Hunter, ropeway, the final boss; credits
ENV[area26]="RT_LOAD=$SAVES/area26.sav RT_CLICK=72756,74246 RT_CLICK_EVERY=9 RT_MOUSE=2,1,72756,74246"
ARGS[area26]="--press 72756:10:700 --press 73556:40:300"; FRAMES[area26]=74246
ENV[area27]="RT_LOAD=$SAVES/area27.sav RT_CLICK=104087,105577 RT_CLICK_EVERY=9 RT_MOUSE=2,1,104087,105577"
ARGS[area27]="--press 104087:10:700 --press 104887:40:300"; FRAMES[area27]=105577
ENV[area28]="RT_LOAD=$SAVES/area28.sav RT_CLICK=127368,128858 RT_CLICK_EVERY=9 RT_MOUSE=2,1,127368,128858"
ARGS[area28]="--press 127368:10:700 --press 128168:40:300"; FRAMES[area28]=128858
ENV[area29]="RT_LOAD=$SAVES/area29.sav RT_CLICK=133714,135204 RT_CLICK_EVERY=9 RT_MOUSE=2,1,133714,135204"
ARGS[area29]="--press 133714:10:700 --press 134514:40:300"; FRAMES[area29]=135204
ENV[area30]="RT_LOAD=$SAVES/area30.sav RT_CLICK=154997,156487 RT_CLICK_EVERY=9 RT_MOUSE=2,1,154997,156487"
ARGS[area30]="--press 154997:10:700 --press 155797:40:300"; FRAMES[area30]=156487
ENV[area31]="RT_LOAD=$SAVES/area31.sav RT_CLICK=181231,182721 RT_CLICK_EVERY=9 RT_MOUSE=2,1,181231,182721"
ARGS[area31]="--press 181231:10:700 --press 182031:40:300"; FRAMES[area31]=182721
ENV[area32]="RT_LOAD=$SAVES/area32.sav RT_CLICK=201849,203339 RT_CLICK_EVERY=9 RT_MOUSE=2,1,201849,203339"
ARGS[area32]="--press 201849:10:700 --press 202649:40:300"; FRAMES[area32]=203339
ENV[area33]="RT_LOAD=$SAVES/area33.sav RT_CLICK=220799,222289 RT_CLICK_EVERY=9 RT_MOUSE=2,1,220799,222289"
ARGS[area33]="--press 220799:10:700 --press 221599:40:300"; FRAMES[area33]=222289
ENV[area34]="RT_LOAD=$SAVES/area34.sav RT_CLICK=236055,237545 RT_CLICK_EVERY=9 RT_MOUSE=2,1,236055,237545"
ARGS[area34]="--press 236055:10:700 --press 236855:40:300"; FRAMES[area34]=237545
ENV[area35]="RT_LOAD=$SAVES/area35.sav RT_CLICK=236291,237781 RT_CLICK_EVERY=9 RT_MOUSE=2,1,236291,237781"
ARGS[area35]="--press 236291:10:700 --press 237091:40:300"; FRAMES[area35]=237781
ENV[area37]="RT_LOAD=$SAVES/area37.sav RT_MOUSE=0,0,0,1"
ARGS[area37]=""; FRAMES[area37]=252802
# freezer, restaurant, library, prison, jail, shower, watch tower, alley, rooftop
ENV[area39]="RT_LOAD=$SAVES/area39.sav RT_CLICK=14777,16267 RT_CLICK_EVERY=9 RT_MOUSE=2,1,14777,16267"
ARGS[area39]="--press 14777:10:700 --press 15577:40:300"; FRAMES[area39]=16267
ENV[area40]="RT_LOAD=$SAVES/area40.sav RT_CLICK=20379,21869 RT_CLICK_EVERY=9 RT_MOUSE=2,1,20379,21869"
ARGS[area40]="--press 20379:10:700 --press 21179:40:300"; FRAMES[area40]=21869
ENV[area41]="RT_LOAD=$SAVES/area41.sav RT_CLICK=28931,30421 RT_CLICK_EVERY=9 RT_MOUSE=2,1,28931,30421"
ARGS[area41]="--press 28931:10:700 --press 29731:40:300"; FRAMES[area41]=30421
ENV[area42]="RT_LOAD=$SAVES/area42.sav RT_CLICK=37013,38503 RT_CLICK_EVERY=9 RT_MOUSE=2,1,37013,38503"
ARGS[area42]="--press 37013:10:700 --press 37813:40:300"; FRAMES[area42]=38503
ENV[area43]="RT_LOAD=$SAVES/area43.sav RT_CLICK=56373,57863 RT_CLICK_EVERY=9 RT_MOUSE=2,1,56373,57863"
ARGS[area43]="--press 56373:10:700 --press 57173:40:300"; FRAMES[area43]=57863
ENV[area44]="RT_LOAD=$SAVES/area44.sav RT_CLICK=64289,65779 RT_CLICK_EVERY=9 RT_MOUSE=2,1,64289,65779"
ARGS[area44]="--press 64289:10:700 --press 65089:40:300"; FRAMES[area44]=65779
ENV[area45]="RT_LOAD=$SAVES/area45.sav RT_CLICK=66512,68002 RT_CLICK_EVERY=9 RT_MOUSE=2,1,66512,68002"
ARGS[area45]="--press 66512:10:700 --press 67312:40:300"; FRAMES[area45]=68002
ENV[area46]="RT_LOAD=$SAVES/area46.sav RT_CLICK=71870,73360 RT_CLICK_EVERY=9 RT_MOUSE=2,1,71870,73360"
ARGS[area46]="--press 71870:10:700 --press 72670:40:300"; FRAMES[area46]=73360
ENV[area47]="RT_LOAD=$SAVES/area47.sav RT_CLICK=74989,76479 RT_CLICK_EVERY=9 RT_MOUSE=2,1,74989,76479"
ARGS[area47]="--press 74989:10:700 --press 75789:40:300"; FRAMES[area47]=76479
ENV[area48]="RT_LOAD=$SAVES/area48.sav RT_CLICK=77599,79089 RT_CLICK_EVERY=9 RT_MOUSE=2,1,77599,79089"
ARGS[area48]="--press 77599:10:700 --press 78399:40:300"; FRAMES[area48]=79089
# menus and actions (pause menu, checking an item, the map, picking up an
# item and a file, reading a file); the rest walk, turn and shoot
ENV[area49]="RT_LOAD=$SAVES/area49.sav RT_CLICK=12208,13698 RT_CLICK_EVERY=9 RT_MOUSE=2,1,12208,13698"
ARGS[area49]="--press 12208:10:700 --press 13008:40:300"; FRAMES[area49]=13698
ENV[area50]="RT_LOAD=$SAVES/area50.sav RT_MOUSE=0,0,0,1"
ARGS[area50]="--press 14161:4000:4 --press 14191:40:4 --press 14221:40:4 --press 14251:4000:4 --press 14281:20:4 --press 14311:20:4 --press 14341:4000:4 --press 14371:2000:4 --press 14401:2000:4 --press 14431:2000:4 --press 14461:20:4 --press 14491:4000:4 --press 14521:20:4 --press 14551:20:4 --press 14581:80:4 --press 14611:2000:4 --press 14641:2000:4 --press 14671:20:4 --press 14701:4000:4 --press 14731:20:4 --press 14761:80:4 --press 14791:10:4 --press 14821:40:4 --press 14851:2000:4 --press 14881:2000:4 --press 14911:20:4 --press 14941:4000:4 --press 14971:2000:4 --press 15001:2000:4 --press 15031:80:4 --press 15061:80:4 --press 15091:80:4 --press 15121:4000:4 --press 15151:40:4 --press 15181:4000:4 --press 15211:2000:4 --press 15241:2000:4 --press 15271:2000:4"; FRAMES[area50]=15651
ENV[area51]="RT_LOAD=$SAVES/area51.sav RT_MOUSE=0,0,0,1"
ARGS[area51]="--press 16142:20:4 --press 16172:20:4 --press 16202:20:4 --press 16232:80:4 --press 16262:80:4 --press 16292:10:4 --press 16322:10:4 --press 16352:40:4 --press 16382:40:4 --press 16412:2000:4 --press 16442:2000:4 --press 16472:20:4 --press 16502:4000:4 --press 16532:2000:4 --press 16562:2000:4 --press 16592:2000:4"; FRAMES[area51]=17632
ENV[area52]="RT_LOAD=$SAVES/area52.sav RT_MOUSE=0,0,0,1"
ARGS[area52]="--press 17907:80:4 --press 17937:80:4 --press 17967:20:4 --press 17997:20:4 --press 18027:10:4 --press 18057:40:4 --press 18087:10:4 --press 18117:40:4 --press 18147:4000:4 --press 18177:2000:4 --press 18207:2000:4"; FRAMES[area52]=19397
ENV[area53]="RT_LOAD=$SAVES/area53.sav RT_CLICK=19397,20687 RT_CLICK_EVERY=9 RT_MOUSE=2,1,19397,20687"
ARGS[area53]="--press 19197:4000:4 --press 19237:4000:4 --press 19277:4000:4 --press 19397:10:600"; FRAMES[area53]=20687
ENV[area54]="RT_LOAD=$SAVES/area54.sav RT_CLICK=22416,23706 RT_CLICK_EVERY=9 RT_MOUSE=2,1,22416,23706"
ARGS[area54]="--press 22216:4000:4 --press 22256:4000:4 --press 22296:4000:4 --press 22416:10:600"; FRAMES[area54]=23706
ENV[area55]="RT_LOAD=$SAVES/area55.sav RT_MOUSE=0,0,0,1"
ARGS[area55]="--press 23547:20:4 --press 23577:20:4 --press 23607:20:4 --press 23637:20:4 --press 23667:80:4 --press 23697:80:4 --press 23727:20:4 --press 23757:20:4 --press 23787:20:4 --press 23817:20:4 --press 23847:4000:4 --press 23877:2000:4 --press 23907:2000:4 --press 23937:2000:4"; FRAMES[area55]=25037
ENV[area56]="RT_LOAD=$SAVES/area56.sav RT_CLICK=25737,27227 RT_CLICK_EVERY=9 RT_MOUSE=2,1,25737,27227"
ARGS[area56]="--press 25737:10:700 --press 26537:40:300"; FRAMES[area56]=27227
ENV[area57]="RT_LOAD=$SAVES/area57.sav RT_CLICK=39412,40902 RT_CLICK_EVERY=9 RT_MOUSE=2,1,39412,40902"
ARGS[area57]="--press 39412:10:700 --press 40212:40:300"; FRAMES[area57]=40902
ENV[area58]="RT_LOAD=$SAVES/area58.sav RT_CLICK=44360,45850 RT_CLICK_EVERY=9 RT_MOUSE=2,1,44360,45850"
ARGS[area58]="--press 44360:10:700 --press 45160:40:300"; FRAMES[area58]=45850
ENV[area59]="RT_LOAD=$SAVES/area59.sav RT_CLICK=50460,51950 RT_CLICK_EVERY=9 RT_MOUSE=2,1,50460,51950"
ARGS[area59]="--press 50460:10:700 --press 51260:40:300"; FRAMES[area59]=51950
ENV[area60]="RT_LOAD=$SAVES/area60.sav RT_CLICK=62061,63551 RT_CLICK_EVERY=9 RT_MOUSE=2,1,62061,63551"
ARGS[area60]="--press 62061:10:700 --press 62861:40:300"; FRAMES[area60]=63551
ENV[area61]="RT_LOAD=$SAVES/area61.sav RT_CLICK=70060,71550 RT_CLICK_EVERY=9 RT_MOUSE=2,1,70060,71550"
ARGS[area61]="--press 70060:10:700 --press 70860:40:300"; FRAMES[area61]=71550
ENV[area62]="RT_LOAD=$SAVES/area62.sav RT_CLICK=74471,75961 RT_CLICK_EVERY=9 RT_MOUSE=2,1,74471,75961"
ARGS[area62]="--press 74471:10:700 --press 75271:40:300"; FRAMES[area62]=75961
AREAS="area6 area7 area8 area9 area10 area11 area12 area13 area14 area19 area21 area22 area23 area24 area25 area98 area99 area26 area27 area28 area29 area30 area31 area32 area33 area34 area35 area37 area39 area40 area41 area42 area43 area44 area45 area46 area47 area48 area49 area50 area51 area52 area53 area54 area55 area56 area57 area58 area59 area60 area61 area62"
LIST=${*:-intro weapons walk menu ammo launcher reloads heavy fight1 hurt1 fight2 fight1s fight1g door death phone gload $AREAS}
for s in $LIST; do
  for side in base new; do
    BIN=$A; [ $side = new ] && BIN=$B
    D=$OUT/$s/$side; rm -rf $D; mkdir -p $D
    ( cd $D && env ${ENV[$s]} RT_RAMDUMP=50:. timeout 1800 $BIN $DISC --frames ${FRAMES[$s]} --every 100 --out . $P ${ARGS[$s]} > /dev/null 2> log.txt ) &
  done
done
wait
# RAM dumps take ~2 MB each: kept only for scenarios that differ (KEEP=1: always)
for s in $LIST; do
  echo "== $s"; r=$(python3 "$HERE/cmp.py" $OUT/$s/base $OUT/$s/new); echo "$r"
  if [ "${KEEP:-0}" != 1 ] && echo "$r" | grep -q "first difference: none"; then rm -f $OUT/$s/base/r*.bin $OUT/$s/new/r*.bin; fi
done
