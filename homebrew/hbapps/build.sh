#!/bin/sh
# Build the app runtime (Lua 5.4 + the player's drawing code) for the NW-A829
set -e
cd "$(dirname "$0")"
L=../third_party/lua-5.4.7
Z="${ZIG:-zig}"	# zig 0.15.2 (set ZIG if it is not on PATH)
LUA="$L/lapi.c $L/lcode.c $L/lctype.c $L/ldebug.c $L/ldo.c $L/ldump.c $L/lfunc.c $L/lgc.c $L/llex.c $L/lmem.c $L/lobject.c
	$L/lopcodes.c $L/lparser.c $L/lstate.c $L/lstring.c $L/ltable.c $L/ltm.c $L/lundump.c $L/lvm.c $L/lzio.c $L/lauxlib.c
	$L/lbaselib.c $L/lcorolib.c $L/lmathlib.c $L/lstrlib.c $L/ltablib.c $L/lutf8lib.c $L/loslib.c"
OUT=${OUT:-hbapps}
if [ "$1" = "sim" ]; then	# the PC simulator (HB_KEYS / HB_SHOTS, see hbapps.c)
	OUT=hbapps_sim
	"$Z" cc -target ${HB_TARGET:-native} -DHB_PC -O1 -g -Wall -Wno-unused-function -I$L -o $OUT hbapps.c ../hbplayer/screens.c ../common/gfx.c $LUA -lm
	ls -la $OUT; exit 0
fi
"$Z" cc -target arm-linux-gnueabi.2.4 -mcpu=arm926ej_s -O2 -s -Wl,--as-needed -Wall -Wno-unused-function \
	-I$L -o $OUT hbapps.c ../hbplayer/screens.c ../common/gfx.c $LUA -lm
python ../hello/fixpthread.py $OUT
# the device has only these shared libraries
python -c "import re,sys; d=open('$OUT', 'rb').read(); bad=[x.decode() for x in set(re.findall(rb'lib[a-z0-9_]+\.so[.0-9]*', d)) if x not in (b'libc.so.6', b'libm.so.6', b'libdl.so.2', b'libpthread.so.0')]; print('NEEDED OK' if not bad else 'NEEDED MISSING ON DEVICE: ' + ' '.join(bad)); sys.exit(1 if bad else 0)"
ls -la $OUT
