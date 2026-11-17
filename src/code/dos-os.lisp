;;;; OS interface functions for SBCL under DOS (32-bit protected mode, DPMI)

;;;; This software is part of the SBCL system. See the README file for
;;;; more information.
;;;;
;;;; This software is derived from the CMU CL system, which was
;;;; written at Carnegie Mellon University and released into the
;;;; public domain. The software is in the public domain and is
;;;; provided with absolutely no warranty. See the COPYING and CREDITS
;;;; files for more information.

(in-package "SB-SYS")

(defun software-type ()
  "Return a string describing the supporting software."
  "DOS")

(defun software-version ()
  "Return a string describing version of the supporting software, or NIL
if not available."
  (or sb-sys::*software-version*
      (setf sb-sys::*software-version*
            ;; The runtime asks DOS (INT 21h) and the DPMI host for their versions.
            (sb-alien:alien-funcall
             (sb-alien:extern-alien "dos_software_version"
                                    (function sb-alien:c-string))))))

;;; Return system time, user time and number of page faults.
(defun get-system-info ()
  (multiple-value-bind (err? utime stime maxrss ixrss idrss
                             isrss minflt majflt)
                       (sb-unix:unix-getrusage sb-unix:rusage_self)
    (declare (ignore maxrss ixrss idrss isrss minflt))
    (unless err?
      (simple-perror "getrusage() failed" :errno utime))
    (values utime stime majflt)))

;;; support for CL:MACHINE-VERSION defined OAOO elsewhere
(defun get-machine-version ()
  nil)
