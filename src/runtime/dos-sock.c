/*
 * BSD sockets for the DOS port, on the Watt-32 TCP/IP library.
 *
 * Watt-32 talks to the network card through a packet driver (a small
 * resident program that comes with the card, or one of the Crynwr
 * collection), which has to be loaded before SBCL starts.  It finds its
 * settings (address or DHCP, name servers) in WATTCP.CFG, looked for in
 * the current directory and in the directory named by the environment
 * variable WATTCP.CFG.  Nothing is touched until Lisp first asks for a
 * socket or a host name.
 *
 * This file is the cdecl face of the library for Lisp, like dos-wrap.c is
 * for the C library.  Unlike the rest of the runtime it is compiled with
 * the compiler's default calling convention, the one Watt-32 is built
 * with, and every function called from elsewhere is marked __cdecl.
 */

/*
 * This software is part of the SBCL system. See the README file for
 * more information.
 *
 * This software is derived from the CMUCL system, which was
 * written at Carnegie Mellon University and released into the
 * public domain. The software is in the public domain and is
 * provided with absolutely no warranty. See the COPYING and CREDITS
 * files for more information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dos-sock.h"

extern void __cdecl dos_prefault(void *address, size_t len);

/* A label Watt-32 expects from the C library's startup module, which the
 * HX extender replaces with its own. */
char __cdecl __begtext = 0;
/* How the C library describes the DOS extender it runs on; Watt-32 goes
 * by it.  HX's startup module leaves these unset. */
extern char _Extender, _ExtenderSubtype;

static int started;                     /* 0 not yet, 1 running, -1 failed */
static unsigned char nonblocking[DOS_SOCKET_LIMIT];

/* While it starts, Watt-32 writes its progress ("Configuring through
 * DHCP..") on the console, over whatever the program has on the screen.
 * We keep what it says instead, and show it only if starting fails, when
 * it is the explanation ("NO PACKET DRIVER FOUND."). */
static char startup_text[400];
static int startup_length;
static int W32_CALL keep_char(char c)
{
    if (startup_length < (int)sizeof startup_text - 1) startup_text[startup_length++] = c;
    return c;
}

/* Start the TCP/IP stack on first use.  Returns 0 if it is running. */
static int startup(void)
{
    if (!started) {
        int (W32_CALL *console)(char) = _outch;
        _Extender = 1;                  /* "DOS/4G compatible": a plain DPMI client */
        _ExtenderSubtype = 0;
        _watt_do_exit = 0;              /* report failure rather than exit */
        startup_length = 0;
        _outch = keep_char;
        started = sock_init() == 0 ? 1 : -1;
        _outch = console;
        if (started < 0 && startup_length) {
            startup_text[startup_length] = 0;
            fprintf(stderr, "%s\n", startup_text);
        }
    }
    if (started < 0) { errno = ENETDOWN; return -1; }
    return 0;
}

/* Give the packet driver back its handle and the DOS memory it delivers
 * into.  Must happen before the program ends, whichever way it ends. */
void __cdecl dos_sock_shutdown(void)
{
    if (started > 0) { started = 0; sock_exit(); }
}

/* Lisp's descriptors and Watt-32's socket numbers are kept apart by a
 * table.  Watt-32 does not reuse the number of a closed TCP socket until
 * two minutes have passed (the connection lingers, as TCP requires), so a
 * server that takes many short connections soon has numbers in the
 * hundreds, while Lisp has room for few descriptors.  A descriptor is
 * free again as soon as Lisp closes it. */
#define SLOTS (DOS_SOCKET_LIMIT - DOS_SOCKET_BASE)
static int watt_socket[SLOTS];          /* by descriptor - DOS_SOCKET_BASE; 0 = free */

/* Lisp's descriptor -> Watt-32's socket number, or -1 with errno set */
static int sock(int fd)
{
    if (started > 0 && dos_socket_p(fd) && watt_socket[fd - DOS_SOCKET_BASE])
        return watt_socket[fd - DOS_SOCKET_BASE];
    errno = EBADF;
    return -1;
}

/* Watt-32's new socket number -> a descriptor for Lisp */
static int descriptor(int s)
{
    int i;
    if (s < 0) return s;
    /* (select_s() takes sets of FD_SETSIZE sockets: see dos_sock_ready) */
    for (i = 0; i < SLOTS && s < FD_SETSIZE; i++)
        if (!watt_socket[i]) {
            watt_socket[i] = s;         /* never 0: Watt-32 numbers them from 3 */
            nonblocking[i + DOS_SOCKET_BASE] = 0;
            return i + DOS_SOCKET_BASE;
        }
    close_s(s);
    errno = EMFILE;
    return -1;
}

/* ---- called from dos-wrap.c, for descriptors that are sockets ---- */

int __cdecl dos_sock_read(int fd, void *buf, unsigned n)
{
    int s = sock(fd);
    if (s < 0) return -1;
    dos_prefault(buf, n);
    return read_s(s, buf, n);
}

int __cdecl dos_sock_write(int fd, const void *buf, unsigned n)
{
    int s = sock(fd);
    if (s < 0) return -1;
    dos_prefault((void *)buf, n);
    return write_s(s, buf, n);
}

int __cdecl dos_sock_close(int fd)
{
    int s = sock(fd);
    if (s < 0) return -1;
    watt_socket[fd - DOS_SOCKET_BASE] = 0;
    return close_s(s);
}

/* Is the socket ready for reading (data, a pending connection, end of
 * stream) or for writing?  Also lets the stack do its periodic work. */
int __cdecl dos_sock_ready(int fd, int for_writing)
{
    fd_set set;
    struct timeval none;
    int s = sock(fd);
    if (s < 0) return 1;                /* let the caller find out */
    FD_ZERO(&set);
    FD_SET(s, &set);
    none.tv_sec = 0; none.tv_usec = 0;
    return select_s(s + 1, for_writing ? 0 : &set, for_writing ? &set : 0, 0, &none) > 0;
}

/* The message for an error number, or 0 if it is not one of Watt-32's.
 * Watt-32 replaces the C library's table of messages with a longer one
 * that covers its own error numbers (the last is ECANCELED), but its
 * strerror() goes by the library's count and does not get that far. */
const char *__cdecl dos_sock_strerror(int e)
{
    extern char *_sys_errlist[];
    return e > 0 && e <= ECANCELED ? _sys_errlist[e] : 0;
}

/* ---- the sockets interface, under its usual names for Lisp ---- */

int __cdecl dosw_socket(int domain, int type, int protocol)
{
    if (startup()) return -1;
    return descriptor(socket(domain, type, protocol));
}

int __cdecl dosw_bind(int fd, void *addr, int len)
{
    int s = sock(fd);
    return s < 0 ? -1 : bind(s, addr, len);
}

int __cdecl dosw_listen(int fd, int backlog)
{
    int s = sock(fd);
    return s < 0 ? -1 : listen(s, backlog);
}

int __cdecl dosw_accept(int fd, void *addr, int *len)
{
    int s = sock(fd);
    return s < 0 ? -1 : descriptor(accept(s, addr, len));
}

int __cdecl dosw_connect(int fd, void *addr, int len)
{
    int s = sock(fd), result;
    if (s < 0) return -1;
    result = connect(s, addr, len);
    /* The peer answering a connection request with a reset is what
     * everyone else calls "connection refused". */
    if (result < 0 && errno == ECONNRESET) errno = ECONNREFUSED;
    return result;
}

int __cdecl dosw_getpeername(int fd, void *addr, int *len)
{
    int s = sock(fd);
    return s < 0 ? -1 : getpeername(s, addr, len);
}

int __cdecl dosw_getsockname(int fd, void *addr, int *len)
{
    int s = sock(fd);
    return s < 0 ? -1 : getsockname(s, addr, len);
}

int __cdecl dosw_shutdown(int fd, int how)
{
    int s = sock(fd);
    return s < 0 ? -1 : shutdown(s, how);
}

int __cdecl dosw_recvfrom(int fd, void *buf, int n, int flags, void *addr, int *len)
{
    int s = sock(fd);
    if (s < 0) return -1;
    dos_prefault(buf, n);
    return recvfrom(s, buf, n, flags, addr, len);
}

int __cdecl dosw_send(int fd, const void *buf, int n, int flags)
{
    int s = sock(fd);
    if (s < 0) return -1;
    dos_prefault((void *)buf, n);
    return send(s, buf, n, flags);
}

int __cdecl dosw_sendto(int fd, const void *buf, int n, int flags, void *addr, int len)
{
    int s = sock(fd);
    if (s < 0) return -1;
    dos_prefault((void *)buf, n);
    return sendto(s, buf, n, flags, addr, len);
}

int __cdecl dosw_setsockopt(int fd, int level, int name, void *value, int len)
{
    int s = sock(fd);
    return s < 0 ? -1 : setsockopt(s, level, name, value, len);
}

int __cdecl dosw_getsockopt(int fd, int level, int name, void *value, int *len)
{
    int s = sock(fd);
    return s < 0 ? -1 : getsockopt(s, level, name, value, len);
}

/* Only what sb-bsd-sockets needs: reading and setting O_NONBLOCK */
int __cdecl dosw_fcntl(int fd, int command, long arg)
{
    int s = sock(fd);
    if (s < 0) return -1;
    if (command == F_GETFL) return nonblocking[fd] ? O_NONBLOCK : 0;
    if (command == F_SETFL) {
        int on = (arg & O_NONBLOCK) != 0;
        if (ioctlsocket(s, FIONBIO, &on) < 0) return -1;
        nonblocking[fd] = on;
        return 0;
    }
    errno = EINVAL;
    return -1;
}

/* ---- names ---- */

struct hostent *__cdecl dosw_gethostbyname(const char *name)
{
    if (startup()) { h_errno = NO_RECOVERY; return 0; }
    return gethostbyname(name);
}

struct hostent *__cdecl dosw_gethostbyaddr(const char *addr, int len, int type)
{
    if (startup()) { h_errno = NO_RECOVERY; return 0; }
    return gethostbyaddr(addr, len, type);
}

struct protoent *__cdecl dosw_getprotobyname(const char *name)
{
    if (startup()) return 0;
    return getprotobyname(name);
}

struct protoent *__cdecl dosw_getprotobynumber(int number)
{
    if (startup()) return 0;
    return getprotobynumber(number);
}

const char *__cdecl dosw_inet_ntop(int family, const void *addr, char *buf, int size)
{
    return inet_ntop(family, addr, buf, size);
}

int __cdecl dosw_inet_pton(int family, const char *text, void *addr)
{
    return inet_pton(family, text, addr);
}

const char *__cdecl dosw_hstrerror(int code) { return hstrerror(code); }
int __cdecl get_h_errno(void) { return h_errno; }
