#!/bin/bash
# usage: build.sh NAME [pe|le] [extra objects...]   -> out/NAME.EXE
set -e
here=$(cd "$(dirname "$0")" && pwd); . "$here/env.sh"
name=$1; fmt=${2:-pe}; shift; shift || true
mkdir -p "$here/out"; cd "$here/out"
wcc386 $WCC_FLAGS -fo=$name.obj ../$name.c
if [ $fmt = pe ]; then
  cat > $name.lnk <<L
format windows nt ^
runtime console=4.0
option stub=$HXDEV/BIN/LOADPE.BIN
option stack=256k
option map=$name.map
option quiet
libpath $WATCOM/lib386
libpath $WATCOM/lib386/dos
libpath $HXDEV/OWSUPP/V20/DOS
libfile cstrtdhx.obj
library hxows
${EXTRA_LNK:-}
name $name.exe
file $name.obj
L
  for o in "$@"; do echo "file $o" >> $name.lnk; done
  wlink @$name.lnk
  # PatchPE equivalent: "PE" -> "PX" at e_lfanew
  python3 - $name.exe <<'P'
import sys,struct
f=open(sys.argv[1],'r+b'); f.seek(0x3c); off=struct.unpack('<I',f.read(4))[0]
f.seek(off); assert f.read(2)==b'PE'; f.seek(off); f.write(b'PX'); f.close()
P
else
  { echo "system dos32a"; echo "option map=$name.map"; echo "option quiet"; echo "option stack=256k"
    echo "name $name.exe"; echo "file $name.obj"; for o in "$@"; do echo "file $o"; done; } > $name.lnk
  wlink @$name.lnk
fi
cp -f "$HXRT/BIN/HDPMI32.EXE" . 2>/dev/null || true
ls -la $name.exe
