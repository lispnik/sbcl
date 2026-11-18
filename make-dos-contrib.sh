#!/bin/sh
set -e

# Build contrib modules for the DOS port, after make-dos.sh.
#
# usage: ./make-dos-contrib.sh CONTRIB...        (e.g. sb-bsd-sockets)
#
# Each module is compiled by contrib/make-contrib.lisp running under DOS
# (through tools-for-build/dos-run.sh), as on other systems.  One step
# cannot happen there: a module with sb-grovel constants files has a C
# program generated for it, which must be compiled and run to produce the
# Lisp definitions of the constants.  DOS has no C compiler, so
# make-contrib.lisp stops after writing the program; it is cross-compiled
# here with Open Watcom (src/runtime/Makefile.dos), run under DOS, and
# then make-contrib.lisp is started again.
#
# Modules that REQUIRE other modules at build time are not handled yet.
# "asdf" builds ASDF and UIOP, which are compiled directly, and "sb-posix"
# builds the small subset of it that DOS can support.
#
# The results are obj/sbcl-home/contrib/NAME.fasl and NAME.asd.  On DOS,
# SBCL finds them by (require :NAME) when they are in a directory CONTRIB
# next to SBCL.CORE, or under the directory named by SBCL_HOME.

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
. output/build-config

dos_run="sh tools-for-build/dos-run.sh ${SBCL_DOS_RUN_OPTIONS:---qemu --disk 256 --timeout 3600}"
dos_sbcl='src\runtime\sbcl.exe --noinform --core output\sbcl.core --no-sysinit --no-userinit --disable-debugger'

# Run make-contrib.lisp for $1 under DOS; returns its exit code.
compile_contrib() {
    objdir=obj/from-self/contrib/$1
    cat > output/contrib.lsp <<END
(setf *default-pathname-defaults*
      (merge-pathnames "contrib/$1/" *default-pathname-defaults*))
(setf sb-ext:*posix-argv* (list "sbcl" "$1"))
(load "../make-contrib.lisp")
END
    mkdir -p $objdir obj/sbcl-home/contrib
    $dos_run --copy-in "src/runtime/sbcl.exe src/runtime/hdpmi32.exe output/sbcl.core output/contrib.lsp
                        contrib/make-contrib.lisp contrib/sb-grovel contrib/$1 obj/sbcl-home $objdir" \
        --copy-out "$objdir obj/sbcl-home/contrib" \
        --stdin 'output\contrib.lsp' "$dos_sbcl" > output/contrib-$1.log
}

# ASDF is not built by make-contrib.lisp: its two files are compiled
# directly (contrib/asdf/Makefile), UIOP first.  UIOP works out where the
# user's home directory is as it loads and insists on an absolute answer,
# so HOME is set.
build_asdf() {
    cat > output/contrib.lsp <<'END'
(setf (sb-ext:readtable-base-char-preference *readtable*) :both)
(format t "home ~S ~S~%" (sb-int:sbcl-homedir-pathname) (user-homedir-pathname))
(defun out (name)
  (sb-ext:parse-native-namestring
   (concatenate 'string (sb-unix:posix-getcwd) "\\obj\\sbcl-home\\contrib\\" name)))
(ensure-directories-exist (out "x"))
(defun build (source fasl)
  (multiple-value-bind (output warnings failure)
      (compile-file source :print nil :output-file (out fasl))
    (declare (ignore warnings))
    (when (or failure (null output)) (sb-ext:exit :code 1 :abort t))
    (load output)))
(build "contrib/asdf/uiop.lisp" "uiop.fasl")
(build "contrib/asdf/asdf.lisp" "asdf.fasl")
(format t "~&ASDF ~A built~%" (funcall (find-symbol "ASDF-VERSION" "ASDF")))
(sb-ext:exit :code 0)
END
    mkdir -p obj/sbcl-home/contrib
    $dos_run --copy-in "src/runtime/sbcl.exe src/runtime/hdpmi32.exe output/sbcl.core output/contrib.lsp
                        contrib/asdf obj/sbcl-home" \
        --copy-out "obj/sbcl-home/contrib" \
        --env 'HOME=D:\' \
        --stdin 'output\contrib.lsp' "$dos_sbcl" > output/contrib-asdf.log
}

# SB-POSIX on DOS is one file: the subset in contrib/sb-posix/dos.lisp.
build_posix() {
    cat > output/contrib.lsp <<'END'
(let ((dir (concatenate 'string (sb-unix:posix-getcwd) "\\obj\\sbcl-home\\contrib\\")))
  (ensure-directories-exist (sb-ext:parse-native-namestring (concatenate 'string dir "x")))
  (multiple-value-bind (output warnings failure)
      (compile-file "contrib/sb-posix/dos.lisp"
                    :output-file (sb-ext:parse-native-namestring (concatenate 'string dir "sb-posix.fasl")))
    (declare (ignore warnings))
    (when (or failure (null output)) (sb-ext:exit :code 1 :abort t))
    (load output))
  (with-open-file (asd (sb-ext:parse-native-namestring (concatenate 'string dir "sb-posix.asd"))
                       :direction :output :if-exists :supersede)
    (format asd "(defsystem :sb-posix :class require-system)~%")))
(sb-ext:exit :code 0)
END
    mkdir -p obj/sbcl-home/contrib
    $dos_run --copy-in "src/runtime/sbcl.exe src/runtime/hdpmi32.exe output/sbcl.core output/contrib.lsp
                        contrib/sb-posix/dos.lisp obj/sbcl-home" \
        --copy-out "obj/sbcl-home/contrib" \
        --stdin 'output\contrib.lsp' "$dos_sbcl" > output/contrib-sb-posix.log
}

for contrib in "$@"; do
    echo "//building contrib $contrib under DOS"
    if [ $contrib = sb-posix ]; then
        if build_posix && [ -f obj/sbcl-home/contrib/sb-posix.fasl ]; then
            ls -l obj/sbcl-home/contrib/sb-posix.fasl
            continue
        fi
        tail -40 output/contrib-sb-posix.log
        echo "building sb-posix failed; see output/contrib-sb-posix.log"
        exit 1
    fi
    if [ $contrib = asdf ]; then
        if build_asdf && [ -f obj/sbcl-home/contrib/asdf.fasl ]; then
            ls -l obj/sbcl-home/contrib/uiop.fasl obj/sbcl-home/contrib/asdf.fasl
            continue
        fi
        tail -40 output/contrib-asdf.log
        echo "building asdf failed; see output/contrib-asdf.log"
        exit 1
    fi
    objdir=obj/from-self/contrib/$contrib
    rm -rf $objdir
    status=0; compile_contrib $contrib || status=$?
    if [ $status = 3 ] && [ -f $objdir/runme.c ]; then
        echo "//extracting constants for $contrib from the C headers"
        $GNUMAKE -C src/runtime -f Makefile.dos GROVEL_SRC=../../$objdir/runme.c dos-obj/grovel.exe \
            > output/contrib-$contrib-grovel.log 2>&1 \
            || { tail -20 output/contrib-$contrib-grovel.log; echo "could not compile $objdir/runme.c"; exit 1; }
        work=output/contrib-grovel
        rm -rf $work; mkdir -p $work
        cp src/runtime/dos-obj/grovel.exe src/runtime/hdpmi32.exe $work/
        # (FreeDOS does not report the exit status of such programs reliably)
        $dos_run --root $work --copy-out "CONST.LSP" grovel.exe CONST.LSP || true
        [ -s $work/CONST.LSP ] || { echo "the constants program for $contrib produced nothing"; exit 1; }
        cp $work/CONST.LSP $objdir/generated-constants.lisp
        status=0; compile_contrib $contrib || status=$?
    fi
    if [ $status != 0 ] || [ ! -f obj/sbcl-home/contrib/$contrib.fasl ]; then
        tail -40 output/contrib-$contrib.log
        echo "building $contrib failed; see output/contrib-$contrib.log"
        exit 1
    fi
    ls -l obj/sbcl-home/contrib/$contrib.fasl
done
