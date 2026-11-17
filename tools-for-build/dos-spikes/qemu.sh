#!/bin/bash
# usage: qemu.sh "DOS command line" [dir]
# Boots FreeDOS 1.4 headless in QEMU with HIMEMX + DOSLFN, copies DIR onto a data
# disk (D:), runs the command there with output to OUT.TXT, powers off, prints output.
set -e
here=$(cd "$(dirname "$0")" && pwd); . "$here/env.sh"; dir=${2:-$here/out}
work=${QEMU_WORK:-$here/out/qemu}; mkdir -p "$work"
boot=$work/boot.img; data=$work/data.img; rc=$work/mtoolsrc
export MTOOLSRC=$rc MTOOLS_SKIP_CHECK=1
printf 'drive c: file="%s" partition=1\ndrive d: file="%s" partition=1\n' "$boot" "$data" > "$rc"
if [ ! -f "$boot" ]; then
  cp "$SBCL_DOS_TOOLS/dl-freedos/lite/FD14LITE.img" "$boot"
  printf '@echo off\r\nSET DOSDIR=\\FREEDOS\r\nSET PATH=\\FREEDOS\\BIN\r\n%sDOSLFN\r\nD:\r\nCALL RUN.BAT\r\nC:\r\nFDAPM POWEROFF\r\n' "${NOLFN:+REM }" > "$work/fdauto.bat"
  mcopy -o "$work/fdauto.bat" c:/FDAUTO.BAT
  mdel c:/SETUP.BAT 2>/dev/null || true
fi
mb=${QEMU_DISK_MB:-256}; cyl=$((mb*2048/(16*63)))
rm -f "$data"; dd if=/dev/zero of="$data" bs=1048576 count=0 seek=$mb 2>/dev/null
mpartition -I -t $cyl -h 16 -s 63 d: && mpartition -c -a -t $cyl -h 16 -s 63 d: && mformat d:
printf '@echo off\r\nSET SBCL_DOS_MERGE_STDERR=1\r\n%s > OUT.TXT\r\n' "$1" > "$work/RUN.BAT"
mcopy -s -Q -o "$dir"/*.[Ee][Xx][Ee] ${QEMU_EXTRA:-} d:/ ; mcopy -o "$work/RUN.BAT" d:/RUN.BAT
perl -e 'alarm shift; exec @ARGV' ${QEMU_TIMEOUT:-300} qemu-system-i386 -m ${QEMU_MEM:-512} -cpu pentium3 \
  -display none -no-reboot -drive file="$boot",format=raw,if=ide,index=0 \
  -drive file="$data",format=raw,if=ide,index=1 >/dev/null 2>&1 || echo "[qemu exit $?]"
mtype d:/OUT.TXT 2>/dev/null || echo "(no output file)"
