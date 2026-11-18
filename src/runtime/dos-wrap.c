/*
 * The C library as seen from Lisp, for the DOS port.
 *
 * Lisp resolves foreign names through dos_symtab[] (see dos-os.c).  Names
 * that belong to the C library are pointed at the dosw_* functions in
 * this file rather than at the library itself, because
 *   - Open Watcom's library functions do not use the cdecl convention
 *     that Lisp's call-out sequence assumes,
 *   - several need DOS-specific behaviour (binary mode, demand-committed
 *     buffers, no fork/pipe/select), and
 *   - a number of POSIX functions do not exist at all.
 * This file replaces wrap.c, which is not compiled for DOS.
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
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <i86.h>
#include <conio.h>
#include <direct.h>
#include <dirent.h>
#include <math.h>
#include <process.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include "genesis/sbcl.h"
#include "runtime.h"
#include "os.h"
#include "wrap.h"

/* Descriptors in this range are network sockets: see dos-sock.c */
#define DOS_SOCKET_BASE 32
#define DOS_SOCKET_LIMIT 64
#define dos_socket_p(fd) ((fd) >= DOS_SOCKET_BASE && (fd) < DOS_SOCKET_LIMIT)
#ifdef DOS_SOCKETS
extern int dos_sock_read(int fd, void *buf, unsigned n);
extern int dos_sock_write(int fd, const void *buf, unsigned n);
extern int dos_sock_close(int fd);
extern int dos_sock_ready(int fd, int for_writing);
extern void dos_sock_shutdown(void);
extern const char *dos_sock_strerror(int e);
#else
#define dos_sock_read(fd, buf, n) (errno = EBADF, -1)
#define dos_sock_write(fd, buf, n) (errno = EBADF, -1)
#define dos_sock_close(fd) (errno = EBADF, -1)
#define dos_sock_ready(fd, for_writing) 1
#define dos_sock_shutdown()
#define dos_sock_strerror(e) 0
#endif

extern void dos_prefault(void *address, size_t len);

/* ---- memory and strings ---- */
void *dosw_malloc(size_t n) { return malloc(n); }
void dosw_free(void *p) { free(p); }
void *dosw_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
void *dosw_memset(void *d, int c, size_t n) { return memset(d, c, n); }
void *dosw_memmove(void *d, const void *s, size_t n) { return memmove(d, s, n); }
int dosw_memcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
size_t dosw_strlen(const char *s) { return strlen(s); }
char *dosw_getenv(const char *name) { return getenv(name); }

char *dosw_strerror(int e)
{
    const char *network = dos_sock_strerror(e);
    return network ? (char *)network : strerror(e);
}

/* ---- process ---- */
extern void dos_record_exit_code(int code);
void dosw_exit(int code) { exit(code); }        /* dos_exit(), in fact */
void dosw__exit(int code) { dos_sock_shutdown(); dos_record_exit_code(code); _exit(code); }
/* for dos_exit() and the crash reporter */
void dos_network_shutdown(void) { dos_sock_shutdown(); }
int dosw_getpid(void) { return getpid(); }
int dosw_chdir(const char *path) { return chdir(path); }
int dosw_setenv(const char *name, const char *value, int overwrite)
{
    return setenv(name, value, overwrite);
}
int dosw_getuid(void) { return 0; }
int dosw_setsid(void) { errno = ENOSYS; return -1; }
int dosw_pipe(int fds[2]) { (void)fds; errno = ENOSYS; return -1; }
int dosw_gethostname(char *name, size_t len)
{
    const char *host = getenv("HOSTNAME");
    strncpy(name, host ? host : "dos", len);
    if (len) name[len - 1] = 0;
    return 0;
}
char *uid_username(int uid) { (void)uid; return strdup("user"); }
char *uid_homedir(int uid) { (void)uid; return 0; }
char *user_homedir(char *name) { (void)name; return 0; }

/* ---- files ---- */

/* Long file name drivers (DOSLFN) edit directories on disk behind the DOS
 * kernel's back.  If the kernel still holds unwritten directory or FAT
 * updates for a file being written -- typically the one standard output
 * is redirected to -- creating a long-named file loses that file's
 * contents (seen on FreeDOS 1.4).  A "disk reset" (INT 21h, AH=0Dh) before
 * any operation that changes a directory makes the kernel write
 * everything out first. */
static void flush_disk_buffers(void)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x0d;
    int386(0x21, &r, &r);
}

int dosw_open(const char *path, int flags, int mode)
{
    if (flags & O_CREAT) flush_disk_buffers();
    return open(path, flags | O_BINARY, mode);
}
int dosw_close(int fd) { return dos_socket_p(fd) ? dos_sock_close(fd) : close(fd); }
int dosw_read(int fd, void *buf, unsigned n)
{
    if (dos_socket_p(fd)) return dos_sock_read(fd, buf, n);
    /* DOS writes into the buffer on our behalf and cannot take a page fault */
    dos_prefault(buf, n);
    return read(fd, buf, n);
}
int dosw_write(int fd, const void *buf, unsigned n)
{
    if (dos_socket_p(fd)) return dos_sock_write(fd, buf, n);
    dos_prefault((void *)buf, n);
    return write(fd, buf, n);
}
long dosw_lseek(int fd, long offset, int whence)
{
    if (dos_socket_p(fd)) { errno = ESPIPE; return -1; }
    return lseek(fd, offset, whence);
}
int dosw_isatty(int fd) { return dos_socket_p(fd) ? 0 : isatty(fd); }
int dosw_access(const char *path, int mode) { return access(path, mode); }
int dosw_dup(int fd) { return dup(fd); }
int dosw_rename(const char *from, const char *to)
{
    flush_disk_buffers();
    /* POSIX rename replaces an existing target; DOS refuses to */
    if (rename(from, to) == 0) return 0;
    if (access(to, F_OK) == 0 && unlink(to) == 0) return rename(from, to);
    return -1;
}
int dosw_unlink(const char *path) { flush_disk_buffers(); return unlink(path); }
int dosw_mkdir(const char *path, int mode) { (void)mode; flush_disk_buffers(); return mkdir(path); }
int dosw_rmdir(const char *path) { flush_disk_buffers(); return rmdir(path); }
char *dosw_getcwd(char *buf, size_t size)
{
    /* Lisp passes (NULL, 0) and expects a malloc'ed result */
    size_t len;
    if (!buf) { size = PATH_MAX + 1; buf = malloc(size); if (!buf) return 0; }
    if (!getcwd(buf, size)) return 0;
    /* A drive's root comes back as "C:\"; Lisp appends a separator itself */
    len = strlen(buf);
    if (len && (buf[len - 1] == '\\' || buf[len - 1] == '/')) buf[len - 1] = 0;
    return buf;
}
FILE *dosw_tmpfile(void) { return tmpfile(); }
int dosw_fclose(FILE *f) { return fclose(f); }
int sb_fileno(FILE *f) { return fileno(f); }

int sb_mkstemp(char *template, int mode)
{
    static unsigned counter;
    size_t len = strlen(template);
    int tries;
    if (len < 6 || strcmp(template + len - 6, "XXXXXX")) { errno = EINVAL; return -1; }
    for (tries = 0; tries < 1000; tries++) {
        int fd;
        sprintf(template + len - 6, "%06u", (unsigned)((time(0) * 31 + counter++) % 1000000));
        flush_disk_buffers();
        fd = open(template, O_CREAT | O_EXCL | O_RDWR | O_BINARY, mode);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    return -1;
}

/* Lisp frees the result with free().  DOS has no symbolic links. */
char *wrapped_readlink(char *path) { (void)path; errno = EINVAL; return 0; }

char *sb_realpath(char *path)
{
    if (access(path, F_OK) != 0) return 0;
    return _fullpath(0, path, 0);
}

static void copy_to_stat_wrapper(struct stat_wrapper *to, struct stat *from)
{
    memset(to, 0, sizeof *to);
    to->wrapped_st_dev = from->st_dev;
    to->wrapped_st_ino = from->st_ino;
    to->wrapped_st_mode = from->st_mode;
    to->wrapped_st_nlink = from->st_nlink;
    to->wrapped_st_uid = from->st_uid;
    to->wrapped_st_gid = from->st_gid;
    to->wrapped_st_rdev = from->st_rdev;
    to->wrapped_st_size = from->st_size;
    to->wrapped_st_blksize = 4096;
    to->wrapped_st_blocks = (from->st_size + 511) / 512;
    to->wrapped_st_atime = from->st_atime;
    to->wrapped_st_mtime = from->st_mtime;
    to->wrapped_st_ctime = from->st_ctime;
}

int stat_wrapper(const char *file_name, struct stat_wrapper *buf)
{
    struct stat real_buf;
    char copy[PATH_MAX + 1];
    size_t len = strlen(file_name);
    int ret;
    /* stat() does not accept a trailing separator, except on a root */
    if (len > 1 && len <= PATH_MAX && (file_name[len - 1] == '\\' || file_name[len - 1] == '/')
        && file_name[len - 2] != ':') {
        memcpy(copy, file_name, len - 1);
        copy[len - 1] = 0;
        file_name = copy;
    }
    if ((ret = stat(file_name, &real_buf)) >= 0)
        copy_to_stat_wrapper(buf, &real_buf);
    return ret;
}

int lstat_wrapper(const char *file_name, struct stat_wrapper *buf)
{
    return stat_wrapper(file_name, buf);
}

int fstat_wrapper(int filedes, struct stat_wrapper *buf)
{
    struct stat real_buf;
    int ret;
    if (dos_socket_p(filedes)) {
        memset(buf, 0, sizeof *buf);
        buf->wrapped_st_mode = S_IFSOCK | 0666;
        return 0;
    }
    if ((ret = fstat(filedes, &real_buf)) >= 0)
        copy_to_stat_wrapper(buf, &real_buf);
    return ret;
}

DIR *sb_opendir(char *name) { return opendir(name); }
struct dirent *sb_readdir(DIR *dir) { return readdir(dir); }
int sb_closedir(DIR *dir) { return closedir(dir); }
char *sb_dirent_name(struct dirent *ent) { return ent->d_name; }

/* ---- time ---- */
int sb_gettimeofday(struct timeval *tv, void *tz) { return gettimeofday(tv, tz); }

int sb_getrusage(int who, struct rusage *usage) { return getrusage(who, usage); }

void sb_nanosleep(time_t sec, int nsec)
{
    delay(sec * 1000 + nsec / 1000000);
}
void sb_nanosleep_double(double seconds)
{
    if (seconds > 0) delay(seconds < 4.0e6 ? (unsigned)(seconds * 1000) : 0xffffffffu);
}
void sb_nanosleep_float(float seconds) { sb_nanosleep_double(seconds); }

int sb_getitimer(int which, void *value) { (void)which; (void)value; errno = ENOSYS; return -1; }
int sb_setitimer(int which, void *value, void *old)
{
    (void)which; (void)value; (void)old; errno = ENOSYS; return -1;
}

/* A stand-in for select(): disk files are always ready; the console is
 * ready for input when a key is waiting; sockets are asked.  TIMEOUT is a
 * struct timeval. */
static int fd_ready(int fd, int for_writing)
{
    if (dos_socket_p(fd)) return dos_sock_ready(fd, for_writing);
    return for_writing || !isatty(fd) || kbhit();
}

int sb_select(int top_fd, unsigned *read_set, unsigned *write_set, unsigned *except_set,
              long *timeout)
{
    unsigned ready_read[DOS_SOCKET_LIMIT / 32], ready_write[DOS_SOCKET_LIMIT / 32];
    clock_t deadline = 0;
    int i, n;
    if (top_fd > DOS_SOCKET_LIMIT) top_fd = DOS_SOCKET_LIMIT;
    if (timeout)
        deadline = clock() + timeout[0] * CLOCKS_PER_SEC + timeout[1] / (1000000 / CLOCKS_PER_SEC);
    for (;;) {
        n = 0;
        memset(ready_read, 0, sizeof ready_read);
        memset(ready_write, 0, sizeof ready_write);
        for (i = 0; i < top_fd; i++) {
            unsigned bit = 1u << (i & 31);
            if (read_set && (read_set[i >> 5] & bit) && fd_ready(i, 0)) { ready_read[i >> 5] |= bit; n++; }
            if (write_set && (write_set[i >> 5] & bit) && fd_ready(i, 1)) { ready_write[i >> 5] |= bit; n++; }
        }
        if (n || (timeout && clock() >= deadline)) break;
    }
    for (i = 0; i < (top_fd + 31) / 32; i++) {
        if (read_set) read_set[i] = ready_read[i];
        if (write_set) write_set[i] = ready_write[i];
        if (except_set) except_set[i] = 0;
    }
    return n;
}

/* ---- math: the library's functions take and return doubles the same
 * way as cdecl would, but not by the same rules for registers ---- */
#define M1(name) double dosw_##name(double x) { return name(x); }
#define M2(name) double dosw_##name(double x, double y) { return name(x, y); }
M1(sin) M1(cos) M1(tan) M1(atan) M1(acos) M1(asin) M1(cosh) M1(sinh) M1(tanh)
M1(asinh) M1(acosh) M1(atanh) M1(exp) M1(log) M1(log10) M1(log1p) M1(log2)
M1(floor) M1(ceil) M1(trunc)
M2(atan2) M2(hypot) M2(pow)
float dosw_floorf(float x) { return (float)floor(x); }
float dosw_ceilf(float x) { return (float)ceil(x); }
float dosw_truncf(float x) { return (float)trunc(x); }
