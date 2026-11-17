/* DOS port: no child processes to wait for */
#ifndef SBCL_DOS_SYS_WAIT_H
#define SBCL_DOS_SYS_WAIT_H
#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8
#endif
