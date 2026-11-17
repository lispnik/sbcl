/* DOS port: there is no poll(); these exist only so that
 * tools-for-build/grovel-headers.c compiles. */
#ifndef SBCL_DOS_POLL_H
#define SBCL_DOS_POLL_H
#define POLLIN 1
#define POLLPRI 2
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
#define POLLNVAL 32
typedef unsigned nfds_t;
#endif
