#!/bin/bash
# Differential test: run scenarios with two builds and compare RAM every
# 50 frames.  usage: run.sh BIN_BASE BIN_NEW OUTDIR [scenario...]
# Scenarios replay fixed input from a new game (needs the disc image).
# EQ_SIDES=base runs only the first build; EQ_NODUMP=1 makes no RAM dumps
# (profile.sh uses both).
set -u
# The original build has no Close-up 3D change to the split threshold (rt.c),
export RT_KEEP_SPLIT=1
# nor the whole-motion mouse turning (port/decomp/player.c).
export RT_PAD_LOOK=1
A=$(realpath $1); B=$(realpath $2); OUT=$3; shift 3
DISC=${DISC:-/root/resurv/disc/image/survivor.bin}
HERE=$(cd "$(dirname "$0")" && pwd)
P=""; for f in $(seq 1600 90 3400); do P="$P --press $f:8:4"; done
P="$P --press 3600:4000:4 --press 3630:4000:4 --press 3660:8:4"   # title -> new game -> play
declare -A ENV ARGS FRAMES NOSTART   # NOSTART[s]=1: the scenario scripts the title itself
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
# Saves from a play session (2026-11-08, made with the "Handgun One Shot" mod,
# testdata/mods_handgun): the same walk; play17 is the sewer by the water
# where the fogged water driver crashed
ENV[play1]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play1.sav RT_CLICK=54194,55684 RT_CLICK_EVERY=9 RT_MOUSE=2,1,54194,55684"
ARGS[play1]="--press 54194:10:700 --press 54994:40:300"; FRAMES[play1]=55684
ENV[play2]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play2.sav RT_CLICK=55397,56887 RT_CLICK_EVERY=9 RT_MOUSE=2,1,55397,56887"
ARGS[play2]="--press 55397:10:700 --press 56197:40:300"; FRAMES[play2]=56887
ENV[play3]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play3.sav RT_CLICK=55714,57204 RT_CLICK_EVERY=9 RT_MOUSE=2,1,55714,57204"
ARGS[play3]="--press 55714:10:700 --press 56514:40:300"; FRAMES[play3]=57204
ENV[play4]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play4.sav RT_CLICK=56906,58396 RT_CLICK_EVERY=9 RT_MOUSE=2,1,56906,58396"
ARGS[play4]="--press 56906:10:700 --press 57706:40:300"; FRAMES[play4]=58396
ENV[play5]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play5.sav RT_CLICK=57787,59277 RT_CLICK_EVERY=9 RT_MOUSE=2,1,57787,59277"
ARGS[play5]="--press 57787:10:700 --press 58587:40:300"; FRAMES[play5]=59277
ENV[play6]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play6.sav RT_CLICK=60406,61896 RT_CLICK_EVERY=9 RT_MOUSE=2,1,60406,61896"
ARGS[play6]="--press 60406:10:700 --press 61206:40:300"; FRAMES[play6]=61896
ENV[play7]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play7.sav RT_CLICK=62288,63778 RT_CLICK_EVERY=9 RT_MOUSE=2,1,62288,63778"
ARGS[play7]="--press 62288:10:700 --press 63088:40:300"; FRAMES[play7]=63778
ENV[play8]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play8.sav RT_CLICK=66701,68191 RT_CLICK_EVERY=9 RT_MOUSE=2,1,66701,68191"
ARGS[play8]="--press 66701:10:700 --press 67501:40:300"; FRAMES[play8]=68191
ENV[play9]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play9.sav RT_CLICK=66918,68408 RT_CLICK_EVERY=9 RT_MOUSE=2,1,66918,68408"
ARGS[play9]="--press 66918:10:700 --press 67718:40:300"; FRAMES[play9]=68408
ENV[play10]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play10.sav RT_CLICK=67058,68548 RT_CLICK_EVERY=9 RT_MOUSE=2,1,67058,68548"
ARGS[play10]="--press 67058:10:700 --press 67858:40:300"; FRAMES[play10]=68548
ENV[play11]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play11.sav RT_CLICK=73257,74747 RT_CLICK_EVERY=9 RT_MOUSE=2,1,73257,74747"
ARGS[play11]="--press 73257:10:700 --press 74057:40:300"; FRAMES[play11]=74747
ENV[play12]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play12.sav RT_CLICK=75465,76955 RT_CLICK_EVERY=9 RT_MOUSE=2,1,75465,76955"
ARGS[play12]="--press 75465:10:700 --press 76265:40:300"; FRAMES[play12]=76955
ENV[play13]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play13.sav RT_CLICK=75662,77152 RT_CLICK_EVERY=9 RT_MOUSE=2,1,75662,77152"
ARGS[play13]="--press 75662:10:700 --press 76462:40:300"; FRAMES[play13]=77152
ENV[play14]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play14.sav RT_CLICK=76782,78272 RT_CLICK_EVERY=9 RT_MOUSE=2,1,76782,78272"
ARGS[play14]="--press 76782:10:700 --press 77582:40:300"; FRAMES[play14]=78272
ENV[play15]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play15.sav RT_CLICK=91069,92559 RT_CLICK_EVERY=9 RT_MOUSE=2,1,91069,92559"
ARGS[play15]="--press 91069:10:700 --press 91869:40:300"; FRAMES[play15]=92559
ENV[play16]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play16.sav RT_CLICK=93962,95452 RT_CLICK_EVERY=9 RT_MOUSE=2,1,93962,95452"
ARGS[play16]="--press 93962:10:700 --press 94762:40:300"; FRAMES[play16]=95452
ENV[play17]="RT_MODS=$SAVES/mods_handgun RT_LOAD=$SAVES/play17.sav RT_CLICK=94756,96246 RT_CLICK_EVERY=9 RT_MOUSE=2,1,94756,96246"
ARGS[play17]="--press 94756:10:700 --press 95556:40:300"; FRAMES[play17]=96246
# the ending: staff credits, skipped with start, and the results screen
# coming up (area67); the results screen, its "save
# the weapons?" prompt, the memory card save and back to the title (area68)
ENV[area67]="RT_GDIR=. RT_LOAD=$SAVES/area67.sav RT_MOUSE=0,0,0,1"
ARGS[area67]="--press 204700:8:4 --press 205600:4000:4 --press 205700:4000:4"; FRAMES[area67]=205900
RP=""; for f in $(seq 205000 60 206200); do RP="$RP --press $f:4000:4"; done
ENV[area68]="RT_GDIR=. RT_LOAD=$SAVES/area68.sav RT_MOUSE=0,0,0,1"
ARGS[area68]="$RP"; FRAMES[area68]=206300
# Option menus (not in the default list: they need over 64 presses, so
# both builds must be from 2026-11-01 or later; compare with such a build)
# the title screen's Option menu from boot: sound, monitor tuning, key
# config (each changed), back out, then Load Game with no saves (needs
# the press limit raised past 64: port builds after 2026-11-01)
ENV[options]="RT_MOUSE=0,0,0,1"; NOSTART[options]=1
ARGS[options]="--press 1650:8:4 --press 1750:20:4 --press 1850:20:4 --press 1950:4000:4 --press 2100:20:4 --press 2140:4000:4 --press 2170:40:4 --press 2200:20:4 --press 2230:20:4 --press 2260:80:4 --press 2290:40:4 --press 2320:20:4 --press 2350:80:4 --press 2380:80:4 --press 2410:20:4 --press 2440:40:4 --press 2470:4000:4 --press 2500:10:4 --press 2530:10:4 --press 2560:10:4 --press 2590:20:4 --press 2620:4000:4 --press 2650:40:4 --press 2680:40:4 --press 2710:40:4 --press 2740:40:4 --press 2770:80:4 --press 2800:20:4 --press 2830:4000:4 --press 2860:40:4 --press 2890:40:4 --press 2920:40:4 --press 2950:4000:4 --press 2980:1000:4 --press 3010:2000:4 --press 3080:20:4 --press 3120:4000:4 --press 3150:40:4 --press 3180:20:4 --press 3210:20:4 --press 3240:80:4 --press 3270:40:4 --press 3300:20:4 --press 3330:80:4 --press 3360:80:4 --press 3390:20:4 --press 3420:40:4 --press 3450:4000:4 --press 3480:10:4 --press 3510:10:4 --press 3540:10:4 --press 3570:20:4 --press 3600:4000:4 --press 3630:40:4 --press 3660:40:4 --press 3690:40:4 --press 3720:40:4 --press 3750:80:4 --press 3780:20:4 --press 3810:4000:4 --press 3840:40:4 --press 3870:40:4 --press 3900:40:4 --press 3930:4000:4 --press 3960:1000:4 --press 3990:2000:4 --press 4060:20:4 --press 4100:4000:4 --press 4130:40:4 --press 4160:20:4 --press 4190:20:4 --press 4220:80:4 --press 4250:40:4 --press 4280:20:4 --press 4310:80:4 --press 4340:80:4 --press 4370:20:4 --press 4400:40:4 --press 4430:4000:4 --press 4460:10:4 --press 4490:10:4 --press 4520:10:4 --press 4550:20:4 --press 4580:4000:4 --press 4610:40:4 --press 4640:40:4 --press 4670:40:4 --press 4700:40:4 --press 4730:80:4 --press 4760:20:4 --press 4790:4000:4 --press 4820:40:4 --press 4850:40:4 --press 4880:40:4 --press 4910:4000:4 --press 4940:1000:4 --press 4970:2000:4 --press 5040:20:4 --press 5080:4000:4 --press 5110:40:4 --press 5140:20:4 --press 5170:20:4 --press 5200:80:4 --press 5230:40:4 --press 5260:20:4 --press 5290:80:4 --press 5320:80:4 --press 5350:20:4 --press 5380:40:4 --press 5410:4000:4 --press 5440:10:4 --press 5470:10:4 --press 5500:10:4 --press 5530:20:4 --press 5560:4000:4 --press 5590:40:4 --press 5620:40:4 --press 5650:40:4 --press 5680:40:4 --press 5710:80:4 --press 5740:20:4 --press 5770:4000:4 --press 5800:40:4 --press 5830:40:4 --press 5860:40:4 --press 5890:4000:4 --press 5920:1000:4 --press 5950:2000:4 --press 6020:20:4 --press 6060:4000:4"; FRAMES[options]=6560
# the in-game Option menu (select): sound, game reset (no), key config,
# monitor tuning, adjust screen, each changed and backed out of
ENV[options_game]="RT_MOUSE=0,0,0,1"
ARGS[options_game]="--press 3900:1:4 --press 4050:20:4 --press 4090:4000:4 --press 4120:40:4 --press 4150:20:4 --press 4180:20:4 --press 4210:80:4 --press 4240:40:4 --press 4270:20:4 --press 4300:80:4 --press 4330:80:4 --press 4360:20:4 --press 4390:40:4 --press 4420:4000:4 --press 4450:10:4 --press 4480:10:4 --press 4510:10:4 --press 4540:20:4 --press 4570:4000:4 --press 4600:40:4 --press 4630:40:4 --press 4660:40:4 --press 4690:40:4 --press 4720:80:4 --press 4750:20:4 --press 4780:4000:4 --press 4810:40:4 --press 4840:40:4 --press 4870:40:4 --press 4900:4000:4 --press 4930:1000:4 --press 4960:2000:4 --press 5050:1:4 --press 5170:20:4 --press 5210:4000:4 --press 5240:40:4 --press 5270:20:4 --press 5300:20:4 --press 5330:80:4 --press 5360:40:4 --press 5390:20:4 --press 5420:80:4 --press 5450:80:4 --press 5480:20:4 --press 5510:40:4 --press 5540:4000:4 --press 5570:10:4 --press 5600:10:4 --press 5630:10:4 --press 5660:20:4 --press 5690:4000:4 --press 5720:40:4 --press 5750:40:4 --press 5780:40:4 --press 5810:40:4 --press 5840:80:4 --press 5870:20:4 --press 5900:4000:4 --press 5930:40:4 --press 5960:40:4 --press 5990:40:4 --press 6020:4000:4 --press 6050:1000:4 --press 6080:2000:4 --press 6170:1:4 --press 6290:20:4 --press 6330:4000:4 --press 6360:40:4 --press 6390:20:4 --press 6420:20:4 --press 6450:80:4 --press 6480:40:4 --press 6510:20:4 --press 6540:80:4 --press 6570:80:4 --press 6600:20:4 --press 6630:40:4 --press 6660:4000:4 --press 6690:10:4 --press 6720:10:4 --press 6750:10:4 --press 6780:20:4 --press 6810:4000:4 --press 6840:40:4 --press 6870:40:4 --press 6900:40:4 --press 6930:40:4 --press 6960:80:4 --press 6990:20:4 --press 7020:4000:4 --press 7050:40:4 --press 7080:40:4 --press 7110:40:4 --press 7140:4000:4 --press 7170:1000:4 --press 7200:2000:4 --press 7290:1:4 --press 7410:80:4 --press 7450:4000:4 --press 7480:40:4 --press 7510:20:4 --press 7540:20:4 --press 7570:80:4 --press 7600:40:4 --press 7630:20:4 --press 7660:80:4 --press 7690:80:4 --press 7720:20:4 --press 7750:40:4 --press 7780:4000:4 --press 7810:10:4 --press 7840:10:4 --press 7870:10:4 --press 7900:20:4 --press 7930:4000:4 --press 7960:40:4 --press 7990:40:4 --press 8020:40:4 --press 8050:40:4 --press 8080:80:4 --press 8110:20:4 --press 8140:4000:4 --press 8170:40:4 --press 8200:40:4 --press 8230:40:4 --press 8260:4000:4 --press 8290:1000:4 --press 8320:2000:4 --press 8410:1:4 --press 8530:80:4 --press 8570:4000:4 --press 8600:40:4 --press 8630:20:4 --press 8660:20:4 --press 8690:80:4 --press 8720:40:4 --press 8750:20:4 --press 8780:80:4 --press 8810:80:4 --press 8840:20:4 --press 8870:40:4 --press 8900:4000:4 --press 8930:10:4 --press 8960:10:4 --press 8990:10:4 --press 9020:20:4 --press 9050:4000:4 --press 9080:40:4 --press 9110:40:4 --press 9140:40:4 --press 9170:40:4 --press 9200:80:4 --press 9230:20:4 --press 9260:4000:4 --press 9290:40:4 --press 9320:40:4 --press 9350:40:4 --press 9380:4000:4 --press 9410:1000:4 --press 9440:2000:4 --press 9530:1:4"; FRAMES[options_game]=9950
# the attract demos: start at the title, then left alone
ENV[demo]="RT_MOUSE=0,0,0,1"; NOSTART[demo]=1
ARGS[demo]="--press 1650:8:4"; FRAMES[demo]=20000
AREAS="demo area67 area68 area6 area7 area8 area9 area10 area11 area12 area13 area14 area19 area21 area22 area23 area24 area25 area98 area99 area26 area27 area28 area29 area30 area31 area32 area33 area34 area35 area37 area39 area40 area41 area42 area43 area44 area45 area46 area47 area48 area49 area50 area51 area52 area53 area54 area55 area56 area57 area58 area59 area60 area61 area62 play1 play2 play3 play4 play5 play6 play7 play8 play9 play10 play11 play12 play13 play14 play15 play16 play17"
LIST=${*:-intro weapons walk menu ammo launcher reloads heavy fight1 hurt1 fight2 fight1s fight1g door death phone gload $AREAS}
for s in $LIST; do
  for side in ${EQ_SIDES:-base new}; do
    BIN=$A; [ $side = new ] && BIN=$B
    D=$OUT/$s/$side; rm -rf $D; mkdir -p $D
    DUMP="RT_RAMDUMP=50:."; [ -n "${EQ_NODUMP:-}" ] && DUMP="RT_X=1"
    ( cd $D && env ${ENV[$s]} $DUMP timeout 1800 $BIN $DISC --frames ${FRAMES[$s]} --every 100 --out . $([ -n "${NOSTART[$s]:-}" ] || echo $P) ${ARGS[$s]} > /dev/null 2> log.txt ) &
  done
done
wait
# RAM dumps take ~2 MB each: kept only for scenarios that differ (KEEP=1: always)
for s in $LIST; do
  echo "== $s"; r=$(python3 "$HERE/cmp.py" $OUT/$s/base $OUT/$s/new); echo "$r"
  if [ "${KEEP:-0}" != 1 ] && echo "$r" | grep -q "first difference: none"; then rm -f $OUT/$s/base/r*.bin $OUT/$s/new/r*.bin; fi
done
