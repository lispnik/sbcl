;;;; SB-POSIX for DOS: only what DOS can do and other software asks for.
;;;;
;;;; The real SB-POSIX needs a POSIX system and a C compiler on the target
;;;; (its constants come from sb-grovel), so it is not built for the DOS
;;;; port.  But portable libraries reach for a few of its functions on
;;;; SBCL -- UIOP for CHDIR and SETENV, Quicklisp for RMDIR -- and those
;;;; exist on DOS.  This file provides that subset under the same names,
;;;; with the same conventions: a pathname designator for a file name, the
;;;; C function's result as the value, and SYSCALL-ERROR on failure.
;;;; make-dos-contrib.sh builds it as the module SB-POSIX.

;;;; This software is part of the SBCL system. See the README file for
;;;; more information.

(defpackage "SB-POSIX"
  (:use "COMMON-LISP" "SB-ALIEN")
  (:export "SYSCALL-ERROR" "SYSCALL-ERRNO" "SYSCALL-NAME"
           "CHDIR" "GETCWD" "GETENV" "GETPID" "MKDIR" "RMDIR" "SETENV" "UNLINK"))
(in-package "SB-POSIX")

(define-condition syscall-error (error)
  ((errno :initarg :errno :reader syscall-errno)
   (name :initarg :name :initform nil :reader syscall-name))
  (:report (lambda (c s)
             (let ((errno (syscall-errno c)))
               (format s "Error in ~S: ~A (~A)"
                       (syscall-name c) (sb-int:strerror errno) errno)))))

(defun syscall-error (&optional name)
  (error 'syscall-error :name name :errno (get-errno)))

(defun filename (designator)
  (etypecase designator
    (string designator)
    (pathname (sb-ext:native-namestring (translate-logical-pathname designator) :as-file t))))

(macrolet ((define-call (name c-name lambda-list result-type argument-types &rest arguments)
             `(defun ,name ,lambda-list
                (let ((result (alien-funcall
                               (extern-alien ,c-name (function ,result-type ,@argument-types))
                               ,@arguments)))
                  (if (minusp result) (syscall-error ',name) result)))))
  (define-call chdir "chdir" (pathname) int (c-string) (filename pathname))
  (define-call rmdir "rmdir" (pathname) int (c-string) (filename pathname))
  (define-call unlink "unlink" (pathname) int (c-string) (filename pathname))
  (define-call mkdir "mkdir" (pathname mode) int (c-string int) (filename pathname) mode)
  (define-call setenv "setenv" (name value overwrite) int (c-string c-string int)
    name value overwrite)
  (define-call getpid "getpid" () int ()))

(defun getenv (name) (sb-ext:posix-getenv name))
(defun getcwd () (sb-unix:posix-getcwd))

(provide "SB-POSIX")
