;;;; Foreign symbol lookup for DOS.  There are no shared objects: the C
;;;; runtime carries a static table of the symbols it exports (see
;;;; os_dlsym_default in src/runtime/dos-os.c).

;;;; This software is part of the SBCL system. See the README file for
;;;; more information.
;;;;
;;;; This software is derived from the CMU CL system, which was
;;;; written at Carnegie Mellon University and released into the
;;;; public domain. The software is in the public domain and is
;;;; provided with absolutely no warranty. See the COPYING and CREDITS
;;;; files for more information.

(in-package "SB-ALIEN")

(defun find-dynamic-foreign-symbol-address (symbol)
  (let ((addr (sap-int
               (alien-funcall
                (extern-alien "os_dlsym_default"
                              (function system-area-pointer
                                        (c-string :external-format :ascii)))
                symbol))))
    (if (zerop addr) nil addr)))
