#!/bin/sh
set -e

# Cross-build SBCL for DOS (32-bit protected mode, x86) from a Unix host.
#
# DOS cannot host the build, so:
#   - everything that is normally "host" work runs here as usual;
#   - the C runtime is cross-compiled with Open Watcom (src/runtime/Makefile.dos);
#   - the steps that must execute the new runtime (warm load and
#     SAVE-LISP-AND-DIE) run on FreeDOS in an emulator, through
#     tools-for-build/dos-run.sh: the files a step needs are copied onto
#     a disk image, and what it produces is copied back.
#
# Prerequisites: see src/runtime/Makefile.dos and tools-for-build/dos-run.sh.
#
# usage: ./make-dos.sh [make-config.sh options]
#
# The result is src/runtime/sbcl.exe (plus hdpmi32.exe, the DPMI host it
# runs on) and output/sbcl.core.

# This software is part of the SBCL system. See the README file for
# more information.
#
# This software is derived from the CMU CL system, which was
# written at Carnegie Mellon University and released into the
# public domain. The software is in the public domain and is
# provided with absolutely no warranty. See the COPYING and CREDITS
# files for more information.

LANG=C
LC_ALL=C
export LANG LC_ALL

# About 15 minutes of emulated time is typical for the warm build; allow a lot more.
dos_run="sh tools-for-build/dos-run.sh ${SBCL_DOS_RUN_OPTIONS:---qemu --disk 1024 --timeout 28800}"
# What the warm build reads, besides the fasls of its first phase
warm_inputs() {
    echo src make-target-2-load.lisp version.lisp-expr local-target-features.lisp-expr
    ls tools-for-build/more-ucd-consts.lisp-expr tools-for-build/unicode-phash.lisp*
    # everything genesis and the cross-compiler left for the target, but not
    # the host's own cores, logs, or earlier results
    for f in output/*; do
        case $f in
            output/after-xc.core|output/sbcl.core|output/*.log|output/*.lsp|output/genesis-2) ;;
            *) echo $f ;;
        esac
    done
}
# The DOS command line is limited to 126 characters, so keep this short and
# pass Lisp forms on standard input rather than with --eval.
# (Runtime options such as --core must precede the toplevel options.)
dos_sbcl='src\runtime\sbcl.exe --noinform'
dos_toplevel='--no-sysinit --no-userinit'

sh make-config.sh --arch=x86 --os=dos "$@"
. output/build-config

echo //building the cross-compiler and the C runtime headers
sh make-host-1.sh

# The constants that tools-for-build/grovel-headers.c would normally
# extract from the target's C headers by running on the target.
cp crossbuild-runner/backends/x86/dos-headers.lisp output/stuff-groveled-from-headers.lisp

echo //cross-compiling the system and making the cold core
sh make-host-2.sh

echo //building the DOS runtime
$GNUMAKE -C src/runtime -f Makefile.dos

echo //doing warm init under DOS - compilation phase
cat > output/warm-1.lsp <<EOF
(sb-fasl::!warm-load "src/cold/warm.lisp")
(sb-ext:exit)
EOF
rm -rf obj/from-self
$dos_run --copy-in "`warm_inputs` output/warm-1.lsp" --copy-out "obj/from-self" \
    --stdin 'output\warm-1.lsp' \
    "$dos_sbcl" --core 'output\cold-sbcl.core' $dos_toplevel --disable-debugger > output/warm-1.log \
    || { tail -40 output/warm-1.log; echo "warm compilation failed; see output/warm-1.log"; exit 1; }

echo //doing warm init under DOS - load and dump phase
rm -f output/sbcl.core
cat > output/warm-2.lsp <<EOF
(sb-fasl::!warm-load "make-target-2-load.lisp")
(setf (extern-alien "gc_coalesce_string_literals" char) 2)
(setf sb-c::*merge-pathnames* t)
(setq sb-c::*name-context-file-path-selector* 'truename)
(setq sb-c::*check-consistency* nil)
(let ((sb-ext:*invoke-debugger-hook* (prog1 sb-ext:*invoke-debugger-hook* (sb-ext:enable-debugger))))
 (sb-ext:save-lisp-and-die "output/sbcl.core"))
EOF
$dos_run --copy-in "`warm_inputs` obj/from-self output/warm-2.lsp" --copy-out "output/sbcl.core" \
    --stdin 'output\warm-2.lsp' \
    "$dos_sbcl" --core 'output\cold-sbcl.core' $dos_toplevel --noprint > output/warm-2.log || true
[ -f output/sbcl.core ] \
    || { tail -40 output/warm-2.log; echo "warm load failed; see output/warm-2.log"; exit 1; }

echo //checking the new core under DOS
cat > output/check.lsp <<EOF
(format t "~&~A ~A on ~A ~A~%" (lisp-implementation-type) (lisp-implementation-version)
        (software-type) (software-version))
(sb-ext:exit :code (if (= (funcall (compile nil '(lambda (x) (* x 6))) 7) 42) 0 1))
EOF
$dos_run --copy-in "src/runtime/sbcl.exe src/runtime/hdpmi32.exe output/sbcl.core output/check.lsp" \
    --stdin 'output\check.lsp' "$dos_sbcl" --core 'output\sbcl.core' $dos_toplevel --noprint

echo "//DOS build complete: src/runtime/sbcl.exe, src/runtime/hdpmi32.exe, output/sbcl.core"
