#!/bin/sh
# Run one command under DOS, for building and testing the DOS port of SBCL
# on a host that cannot run DOS programs itself.
#
# usage: dos-run.sh [--qemu | --dosbox] [--root DIR] [--stdin FILE]
#                   [--mem MB] [--timeout SECONDS] [--disk MB]
#                   [--copy-in "PATH..."] [--copy-out "PATH..."]
#                   [--env NAME=VALUE]... COMMAND...
#
# DIR (default: the current directory) becomes the DOS current directory.
# COMMAND is a DOS command line; FILE (relative to DIR) is fed to it as
# standard input, with each NAME set to VALUE in its environment.  Standard output and standard error are collected in
# DIR/DOSRUN.LOG and copied to our standard output.  The exit status is that
# of the command when it is the SBCL runtime, and otherwise 0 if the command
# finished with ERRORLEVEL 0.
#
# Two ways of running DOS:
#
#   --qemu    (default) FreeDOS 1.4 booted in QEMU with HIMEMX and DOSLFN
#             loaded: the real target.  The files named by --copy-in
#             (paths relative to DIR; default: everything in DIR) are
#             copied onto a FAT disk image, which becomes drive D:, and
#             afterwards those named by --copy-out are copied back.
#
#   --dosbox  DOSBox-X, with DIR mounted directly as drive C:, so the
#             command works on the host's files in place and starts
#             quickly.  DOSBox-X emulates DOS itself (with long file
#             names).  Handy for experiments, but it is not FreeDOS, and
#             its CPU emulation is both slower and less trustworthy:
#             DOSBox-X's "dynamic" core computes the overflow flag of IMUL
#             wrongly, which makes SBCL's compiler generate bad code, so
#             the (slow) "normal" core is used here.
#
# SBCL_DOS_QEMU_OPTIONS may hold extra options for QEMU (more devices, say).
# With --qemu, a DOSINIT.BAT in DIR is run first (to load a network packet
# driver, for example); its output is not captured.
#
# Requires dosbox-x or qemu-system-i386 + mtools on the PATH.  For --qemu,
# SBCL_DOS_TOOLS must hold dl-freedos/lite/FD14LITE.img (the FreeDOS 1.4
# "LiteUSB" image).

set -e
backend=qemu
copy_in=
copy_out=
root=.
stdin=
mem=512
timeout=3600
disk=512
env=
while [ $# -gt 0 ]; do
    case "$1" in
        --dosbox) backend=dosbox ;;
        --qemu) backend=qemu ;;
        --root) root=$2; shift ;;
        --stdin) stdin=$2; shift ;;
        --mem) mem=$2; shift ;;
        --timeout) timeout=$2; shift ;;
        --disk) disk=$2; shift ;;
        --copy-in) copy_in=$2; shift ;;
        --copy-out) copy_out=$2; shift ;;
        --env) env="$env$2
"; shift ;;
        --) shift; break ;;
        -*) echo "dos-run.sh: unknown option $1" >&2; exit 2 ;;
        *) break ;;
    esac
    shift
done
[ $# -gt 0 ] || { echo "usage: dos-run.sh [options] COMMAND..." >&2; exit 2; }
root=`cd "$root" && pwd`
here=`cd "\`dirname "$0"\`" && pwd`
: ${SBCL_DOS_TOOLS:=`cd "$here/../.." && pwd`/sbcl-dos-tools}

# The batch file that runs the command.  COMMAND.COM cannot redirect
# standard error; the SBCL runtime merges it into standard output when
# SBCL_DOS_MERGE_STDERR is set.
bat=$root/DOSRUN.BAT
rm -f "$root/DOSRUN.LOG" "$root/DOSRUN.OK" "$root/DOSRUN.RC"
{
    printf '@echo off\r\n'
    printf 'SET SBCL_DOS_MERGE_STDERR=1\r\n'
    printf 'SET SBCL_DOS_EXIT_CODE_FILE=DOSRUN.RC\r\n'
    printf '%s' "$env" | while read -r setting; do
        [ -n "$setting" ] && printf 'SET %s\r\n' "$setting"
    done
    if [ -n "$stdin" ]; then
        printf '%s < %s > DOSRUN.LOG\r\n' "$*" "$stdin"
    else
        printf '%s > DOSRUN.LOG\r\n' "$*"
    fi
    printf 'IF ERRORLEVEL 1 GOTO done\r\n'
    printf 'ECHO OK> DOSRUN.OK\r\n'
    printf ':done\r\n'
} > "$bat"

case $backend in
dosbox)
    dosbox-x -silent -exit -fastlaunch -nolog -time-limit $timeout \
        -set "dosbox memsize=$mem" -set "cpu cputype=ppro_slow" \
        -set "cpu core=normal" -set "cpu cycles=max" \
        -set "dos lfn=true" -set "dos ver=7.1" -set "dos files=200" \
        -c "mount c \"$root\"" -c "c:" -c "call DOSRUN.BAT" -c "exit" \
        > /dev/null 2>&1 || true
    ;;
qemu)
    work=${SBCL_DOS_QEMU_WORK:-${TMPDIR:-/tmp}/sbcl-dos-qemu.$$}
    mkdir -p "$work"
    boot=$work/boot.img; data=$work/data.img
    MTOOLSRC=$work/mtoolsrc; MTOOLS_SKIP_CHECK=1; export MTOOLSRC MTOOLS_SKIP_CHECK
    printf 'drive c: file="%s" partition=1\ndrive d: file="%s" partition=1\n' \
        "$boot" "$data" > "$MTOOLSRC"
    # Boot disk: the FreeDOS installer image with its startup file replaced.
    cp "$SBCL_DOS_TOOLS/dl-freedos/lite/FD14LITE.img" "$boot"
    printf '@echo off\r\nSET DOSDIR=\\FREEDOS\r\nSET PATH=\\FREEDOS\\BIN\r\nDOSLFN\r\nD:\r\nIF EXIST HDPMI32.EXE HDPMI32 -r\r\nIF EXIST SRC\\RUNTIME\\HDPMI32.EXE SRC\\RUNTIME\\HDPMI32 -r\r\nIF EXIST DOSINIT.BAT CALL DOSINIT.BAT\r\nCALL DOSRUN.BAT\r\nC:\r\nFDAPM POWEROFF\r\n' \
        > "$work/fdauto.bat"
    mcopy -o "$work/fdauto.bat" c:/FDAUTO.BAT
    mdel c:/SETUP.BAT 2>/dev/null || true
    # Data disk: one FAT partition holding a copy of DIR.
    cyl=`expr $disk \* 2048 / 1008`
    rm -f "$data"
    dd if=/dev/zero of="$data" bs=1048576 count=0 seek=$disk 2>/dev/null
    mpartition -I -t $cyl -h 16 -s 63 d: 2>/dev/null
    mpartition -c -a -t $cyl -h 16 -s 63 d: 2>/dev/null
    mformat d:
    # mkdir -p on the DOS disk
    dos_mkdirs() {
        d=
        for part in `echo "$1" | tr '/' ' '`; do
            d=$d/$part
            mmd -D s "d:$d" 2>/dev/null || true
        done
    }
    (cd "$root"
     if [ -z "$copy_in" ]; then copy_in=`ls`; fi
     for path in $copy_in; do
         [ -e "$path" ] || { echo "dos-run.sh: no such file: $path" >&2; exit 2; }
         parent=`dirname "$path"`
         if [ "$parent" = . ]; then parent=; else dos_mkdirs "$parent"; fi
         mcopy -s -Q -o "$path" "d:/$parent/"
     done
     mcopy -o DOSRUN.BAT d:/)
    qemu-system-i386 -m $mem -cpu pentium3 -display none -no-reboot $SBCL_DOS_QEMU_OPTIONS \
        -drive file="$boot",format=raw,if=ide,index=0 \
        -drive file="$data",format=raw,if=ide,index=1 > /dev/null 2>&1 &
    qemu=$!
    # The batch file powers the machine off when the command is done; if it
    # has not by the time limit, the command hung: stop the emulator.
    waited=0
    while kill -0 $qemu 2>/dev/null; do
        if [ $waited -ge $timeout ]; then
            echo "dos-run.sh: timed out after $timeout seconds" >&2
            kill -9 $qemu 2>/dev/null
            break
        fi
        sleep 1; waited=`expr $waited + 1`
    done
    wait $qemu 2>/dev/null || true
    mcopy -n d:/DOSRUN.LOG "$root/DOSRUN.LOG" 2>/dev/null || true
    mcopy -n d:/DOSRUN.OK "$root/DOSRUN.OK" 2>/dev/null || true
    mcopy -n d:/DOSRUN.RC "$root/DOSRUN.RC" 2>/dev/null || true
    for path in $copy_out; do
        # Copy into an empty staging directory and move the result into
        # place: mtools sometimes alters the name of what it creates
        # (a trailing "_" has been seen on a copied directory).
        stage=$work/out; rm -rf "$stage"; mkdir -p "$stage"
        if mcopy -s -n -Q -m "d:/$path" "$stage/" && [ `ls "$stage" | wc -l` -eq 1 ]; then
            mkdir -p "$root/`dirname "$path"`"
            rm -rf "$root/$path"
            mv "$stage"/* "$root/$path"
        else
            echo "dos-run.sh: could not copy back $path" >&2
        fi
    done
    [ -n "$SBCL_DOS_QEMU_WORK" ] || rm -rf "$work"
    ;;
esac

if [ -f "$root/DOSRUN.LOG" ]; then
    cat "$root/DOSRUN.LOG"
else
    echo "dos-run.sh: the command produced no output (emulator timeout or crash?)" >&2
fi
# The exit status.  The SBCL runtime leaves its exit code in DOSRUN.RC
# (because of SBCL_DOS_EXIT_CODE_FILE above); for other programs we only
# have ERRORLEVEL, which FreeDOS does not report correctly for programs
# run under the HX extender.
status=1
if [ -f "$root/DOSRUN.RC" ]; then
    status=`tr -d '\r\n ' < "$root/DOSRUN.RC"`
    case "$status" in ''|*[!0-9]*) status=1 ;; esac
elif [ -f "$root/DOSRUN.OK" ]; then
    status=0
fi
rm -f "$root/DOSRUN.OK" "$root/DOSRUN.RC" "$bat"
exit $status
