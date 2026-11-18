/*
 * Sockets for the DOS port, provided by the Watt-32 TCP/IP library
 * (https://github.com/gvanem/Watt-32) on top of a packet driver.
 *
 * This header is used by dos-sock.c and is force-included when the
 * constants of the sb-bsd-sockets contrib are extracted, so that both see
 * the same definitions: Watt-32's headers plus the few names they lack.
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

#ifndef SBCL_DOS_SOCK_H
#define SBCL_DOS_SOCK_H

#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <tcp.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* Sockets appear to Lisp as descriptors in a range that DOS file handles
 * do not reach (a DOS process has 20 by default) and that still fits in
 * the fd_set the Lisp side uses (FD_SETSIZE is 64): so, 32 open at a time.
 * dos-sock.c maps them to Watt-32's own socket numbers. */
#define DOS_SOCKET_BASE 32
#define DOS_SOCKET_LIMIT 64
#define dos_socket_p(fd) ((fd) >= DOS_SOCKET_BASE && (fd) < DOS_SOCKET_LIMIT)

/* fcntl() on a socket understands just these */
#ifndef F_GETFL
#define F_GETFL 3
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x4000
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x80
#endif

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41                 /* named IPPROTO_IP6 by Watt-32 */
#endif

/* for dos-wrap.c */
int __cdecl dos_sock_read(int fd, void *buf, unsigned n);
int __cdecl dos_sock_write(int fd, const void *buf, unsigned n);
int __cdecl dos_sock_close(int fd);
int __cdecl dos_sock_ready(int fd, int for_writing);
void __cdecl dos_sock_shutdown(void);
const char *__cdecl dos_sock_strerror(int e);

#endif
