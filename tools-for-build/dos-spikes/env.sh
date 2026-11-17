# Source this: sets up Open Watcom v2 + HX paths for the DOS spikes.
# SBCL_DOS_TOOLS must hold: ow/ (Open Watcom snapshot), dl-hx/{rt,dev} (HX kits),
# dl-freedos/lite/FD14LITE.img
: ${SBCL_DOS_TOOLS:=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../sbcl-dos-tools" && pwd)}
export SBCL_DOS_TOOLS
export WATCOM=$SBCL_DOS_TOOLS/ow
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) owbin=armo64 ;; Darwin-x86_64) owbin=bino64 ;;
  Linux-aarch64) owbin=arml64 ;; *) owbin=binl64 ;;
esac
export PATH=$WATCOM/$owbin:$PATH
export INCLUDE=$WATCOM/h
export HXDEV=$SBCL_DOS_TOOLS/dl-hx/dev HXRT=$SBCL_DOS_TOOLS/dl-hx/rt
export WCC_FLAGS="-zq -bt=dos -mf -6s -ecc -fpi87 -fp6 -s -za99 -zastd=c99 -ox"
