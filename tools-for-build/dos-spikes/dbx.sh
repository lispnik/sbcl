#!/bin/bash
# usage: dbx.sh "DOS command line" [dir]  -- run headless in DOSBox-X, print captured output
here=$(cd "$(dirname "$0")" && pwd); dir=${2:-$here/out}
rm -f "$dir/OUT.TXT" "$dir/out.txt"
printf '@echo off\r\nSET SBCL_DOS_MERGE_STDERR=1\r\n%s > OUT.TXT\r\n' "$1" > "$dir/RUN.BAT"
dosbox-x -silent -exit -fastlaunch -nolog -time-limit ${DBX_TIMEOUT:-120} \
  -set "dosbox memsize=${DBX_MEM:-512}" -set "cpu cputype=ppro_slow" -set "cpu core=${DBX_CORE:-dynamic}" \
  -set "cpu cycles=max" -set "dos lfn=true" -set "dos ver=7.1" \
  -c "mount c \"$dir\"" -c "c:" -c "call RUN.BAT" -c "exit" >/dev/null 2>&1
echo "[dosbox-x exit $?]"
cat "$dir/OUT.TXT" 2>/dev/null || cat "$dir/out.txt" 2>/dev/null || echo "(no output file)"
