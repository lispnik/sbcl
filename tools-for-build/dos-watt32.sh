#!/bin/sh
# Build the Watt-32 TCP/IP library for the DOS port of SBCL.
#
# Watt-32 (https://github.com/gvanem/Watt-32) is the usual BSD-sockets
# library for 32-bit DOS programs; it talks to the network card through a
# resident "packet driver".  Its own makefiles build it with Watcom's
# register calling convention; here it is built with the stack-based one,
# to go with the C library variant the SBCL runtime links against.
# Nothing in the Watt-32 tree is modified: generated files and objects go
# to $SBCL_DOS_TOOLS/watt32-build.
#
# usage: dos-watt32.sh          (expects the source in $SBCL_DOS_TOOLS/dl-watt32/src)
# result: $SBCL_DOS_TOOLS/watt32-build/wattcpsf.lib
set -e
here=`cd "\`dirname "$0"\`" && pwd`
: ${SBCL_DOS_TOOLS:=`cd "$here/../.." && pwd`/sbcl-dos-tools}
WATT=${WATT_ROOT:-$SBCL_DOS_TOOLS/dl-watt32/src}
B=$SBCL_DOS_TOOLS/watt32-build
export WATCOM=$SBCL_DOS_TOOLS/ow
case "`uname -s`-`uname -m`" in
  Darwin-arm64) owbin=armo64 ;; Darwin-x86_64) owbin=bino64 ;;
  Linux-aarch64) owbin=arml64 ;; *) owbin=binl64 ;;
esac
PATH=$WATCOM/$owbin:$PATH; export PATH
export INCLUDE=$WATCOM/h
HXDEV=$SBCL_DOS_TOOLS/dl-hx/dev
# -6s without -ecc: Watcom's own stack-based convention, the one its C
# library uses.  (With -ecc, the callbacks Watt-32 hands to qsort(), signal()
# and atexit() would have the wrong convention.)  The SBCL runtime reaches
# the library only through dos-sock.c, which is compiled the same way and
# exports cdecl wrappers.
CFLAGS="-zq -bt=dos -mf -6s -fpi87 -fp6 -s -oilrtm -zlf -wcd=102 -wcd=111 -DWATT32_STATIC"
mkdir -p $B/gen/sys $B/gen/build/watcom/flat $B/obj $B/errnos

link_dos_pe() {      # link_dos_pe out.exe objects/libs...
    out=$1; shift
    { echo "format windows nt ^"; echo "runtime console=4.0"
      echo "option stub=$HXDEV/BIN/LOADPE.BIN"; echo "option stack=256k"; echo "option quiet"
      echo "libpath $WATCOM/lib386"; echo "libpath $WATCOM/lib386/dos"
      echo "libpath $HXDEV/OWSUPP/V20/DOS"; echo "libfile cstrtdhx.obj"; echo "library hxows"
      echo "name $out"; for f in "$@"; do case $f in *.lib) echo "library $f";; *) echo "file $f";; esac; done
    } > $out.lnk
    wlink @$out.lnk
    printf 'PX' | dd of=$out bs=1 seek=`od -An -tu4 -j60 -N4 $out | tr -d ' '` conv=notrunc 2>/dev/null
}

# 1. The table of error numbers the C library lacks.  Watt-32 generates it
#    with a program that has to run on the target, so it runs under DOS.
if [ ! -s $B/gen/sys/watcom.err ]; then
    echo "//generating Watt-32's errno tables (runs under DOS)"
    wcc386 -zq -bt=dos -mf -6s -ecc -s -i=$WATT/inc -fo=$B/errnos/errnos.obj $WATT/util/errnos.c
    link_dos_pe $B/errnos/errnos.exe $B/errnos/errnos.obj
    cp $SBCL_DOS_TOOLS/dl-hx/rt/BIN/HDPMI32.EXE $B/errnos/
    sh $here/dos-run.sh --qemu --disk 16 --timeout 120 --root $B/errnos errnos.exe -e | tr -d '\r' > $B/gen/sys/watcom.err || true
    sh $here/dos-run.sh --qemu --disk 16 --timeout 120 --root $B/errnos errnos.exe -s | tr -d '\r' > $B/gen/build/watcom/syserr.c || true
    [ -s $B/gen/sys/watcom.err ] && [ -s $B/gen/build/watcom/syserr.c ] || { echo "errno generation failed"; exit 1; }
fi

# 2. The real-mode packet receiver, as an array of bytes
nasm -f bin -o $B/gen/asmpkt.bin $WATT/src/asmpkt.nas
od -An -v -tx1 $B/gen/asmpkt.bin | sed 's/\([0-9a-f][0-9a-f]\)/0x\1,/g' > $B/gen/pkt_stub.h

# 3. The record of how it was built, which the library can report
echo "const char *w32_cflags = \"$CFLAGS\"; const char *w32_cc = \"wcc386\";" > $B/gen/build/watcom/flat/cflags.h
printf '%s' "$CFLAGS" | od -An -v -tx1 | sed 's/\([0-9a-f][0-9a-f]\)/0x\1,/g' > $B/gen/build/watcom/flat/cflagsbf.h

# 3b. One correction, made to copies of the files (the Watt-32 tree itself
#     is left alone).  The real-mode buffer reserves ETH_MAX+10 bytes for
#     the transmit packet, but _eth_init() clears sizeof(union link_Packet)
#     bytes there, which is larger; the excess overwrites the DOS memory
#     control block that follows, and DOS then hangs when the program
#     exits.  Reserve what is actually cleared.
mkdir -p $B/patched
cp $WATT/src/pcpkt.c $B/patched/pcpkt.c
sed 's/rdata_size += PKT_TMP_SIZE + ETH_MAX + 10;/rdata_size += PKT_TMP_SIZE + sizeof(union link_Packet) + 16;/' \
    $WATT/src/pcpkt2.c > $B/patched/pcpkt2.c
cmp -s $WATT/src/pcpkt2.c $B/patched/pcpkt2.c && { echo "the pcpkt2.c correction no longer applies"; exit 1; }

# 3c. And one change of behaviour.  While it waits, Watt-32 asks the C
#     library whether a key has been pressed, "to enable ^C generation":
#     DOS then takes a Ctrl-C out of the keyboard buffer and raises its
#     break interrupt.  A Lisp program reads the keyboard itself -- Ctrl-C
#     is an ordinary key to an editor -- so Watt-32 only looks at the
#     BIOS's buffer, as it already does first.
sed 's/^  return kbhit();$/  return (1);/' $WATT/src/misc.c > $B/patched/misc.c
cmp -s $WATT/src/misc.c $B/patched/misc.c && { echo "the misc.c change no longer applies"; exit 1; }

# 4. Compile.  The source list is makefile.all's CORE, BSD, BIND and ZLIB lists.
sources=`sed -n '/^CORE_SOURCE *=/,/^$/p;/^BSD_SOURCE *=/,/^$/p;/^BIND_SOURCE *=/,/^$/p;/^ZLIB_SOURCE *=/,/^$/p' $WATT/src/makefile.all \
         | tr -s ' \\\\=' '\n\n\n' | grep '\.c$'`
echo "//compiling Watt-32"
cd $WATT/src
fail=
for c in $sources; do
    o=$B/obj/`basename $c .c`.obj
    if [ $c = pcpkt.c ] || [ $c = misc.c ]; then
        [ $o -nt $B/patched/$c ] && [ $o -nt $0 ] && continue
        # compiled from inside the directory of copies, so that pcpkt.c's
        # #include "pcpkt2.c" finds the corrected one
        (cd $B/patched && wcc386 $CFLAGS -i=$B/gen -i=$WATT/src -i=$WATT/inc -fo=$o $c) \
            > $B/obj/`basename $c .c`.err 2>&1 || fail="$fail $c"
        continue
    fi
    [ $o -nt $c ] && continue
    wcc386 $CFLAGS -i=$B/gen -i=. -i=../inc -fo=$o $c > $B/obj/`basename $c .c`.err 2>&1 || fail="$fail $c"
done
[ -z "$fail" ] || { echo "failed:$fail"; exit 1; }
wasm -zq -bt=dos -3s -mf -dDOSX -dDOS4GW -fo=$B/obj/cpumodel.obj cpumodel.asm
rm -f $B/wattcpsf.lib
wlib -q -b -c $B/wattcpsf.lib `ls $B/obj/*.obj | sed 's/^/+/'`
ls -la $B/wattcpsf.lib
