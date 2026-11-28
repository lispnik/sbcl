/*
 * TLS for Lisp on DOS: a small C face on Mbed TLS (3.6), for a client
 * that already has a connected TCP socket and wants to talk TLS over it.
 *
 * The same file serves two Lisps.  Built with DJGPP it is linked into
 * CLISP (src/misc.d has the Lisp functions) and reads and writes the
 * socket with Watt-32's read_s and write_s.  Built with Open Watcom it is
 * linked into SBCL's DOS runtime and goes through that runtime's
 * dos_sock_read and dos_sock_write.
 *
 *   dostls_config(verify, ca_file, min, max)   what to insist on
 *   dostls_open(config, fd, hostname)          the handshake
 *   dostls_read, dostls_write, dostls_ready    the data
 *   dostls_close, dostls_config_free
 *   dostls_info, dostls_error                  what happened
 *
 * Everything blocks.  Nothing here closes the socket.
 *
 * Random numbers.  DOS has no source of them, and TLS is worthless
 * without one.  mbedtls_hardware_poll below, which Mbed TLS calls for
 * its seed, mixes: the jitter between the processor's cycle counter and
 * the 8254 timer chip, which run from different crystals; the date and
 * time; a random-number device (NOISE.SYS and the like), if one is
 * installed; and a seed file, if the environment variable TLSSEED names
 * one, which is rewritten on every start so that no two runs begin alike.
 * On a real machine the jitter alone is a fair source.  In an emulator it
 * may not be: use a seed file there for anything that matters.
 *
 * This file is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"

#include "dostls.h"

/* ------------------------- the two systems ------------------------- */

#if defined(__DJGPP__)

#include <pc.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <tcp.h>                        /* Watt-32: read_s, write_s, select_s */
#define port_in(p) inportb(p)
#define port_out(p,v) outportb(p,v)
#define socket_read(fd,buf,n) read_s(fd,(char*)(buf),n)
#define socket_write(fd,buf,n) write_s(fd,(const char*)(buf),n)
static int socket_ready(int fd, int milliseconds)
{
    fd_set readable;
    struct timeval wait;
    FD_ZERO(&readable); FD_SET(fd,&readable);
    wait.tv_sec = milliseconds / 1000; wait.tv_usec = (milliseconds % 1000) * 1000;
    return select_s(fd+1,&readable,NULL,NULL,&wait) > 0;
}
static unsigned long long cycles(void)
{
    unsigned lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

#elif defined(__WATCOMC__)

#include <conio.h>
#define port_in(p) inp(p)
#define port_out(p,v) outp(p,v)
extern int __cdecl dos_sock_read(int fd, void *buf, unsigned n);
extern int __cdecl dos_sock_write(int fd, const void *buf, unsigned n);
extern int __cdecl dos_sock_ready(int fd, int for_writing);
#define socket_read(fd,buf,n) dos_sock_read(fd,buf,n)
#define socket_write(fd,buf,n) dos_sock_write(fd,buf,n)
static int socket_ready(int fd, int milliseconds)
{
    clock_t end = clock() + (clock_t)((double)milliseconds * CLOCKS_PER_SEC / 1000);
    for (;;) {
        if (dos_sock_ready(fd,0) > 0) return 1;
        if (clock() >= end) return 0;
    }
}
/* Clearing secrets from memory (MBEDTLS_PLATFORM_ZEROIZE_ALT): Mbed TLS
 * would use memset_s, which this C library announces but does not have. */
static void *(*volatile clear_memory)(void *, int, size_t) = memset;
void mbedtls_platform_zeroize(void *buf, size_t len)
{
    if (len > 0) clear_memory(buf,0,len);
}
unsigned long long dostls_rdtsc(void);
#pragma aux dostls_rdtsc = 0x0f 0x31 value [edx eax] modify exact [edx eax];
#define cycles() dostls_rdtsc()

#else
#error "dostls.c: DJGPP or Open Watcom"
#endif

/* -------------------- what Mbed TLS asks of us --------------------- */

/* The time in milliseconds, for timeouts in the handshake */
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)((double)clock() * 1000 / CLOCKS_PER_SEC);
}

/* The date, for the validity of certificates.  Mbed TLS wants the time in
 * Greenwich; it gets what the PC's clock shows, which is the best that can
 * be said on DOS without knowing the time zone (and DJGPP's idea of the
 * zone, when TZ is not set, is not to be relied on).  A day either way
 * does not matter here. */
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf)
{
    time_t t = (time_t)*tt;
    struct tm *tm = localtime(&t);
    if (tm == NULL) return NULL;
    *tm_buf = *tm;
    return tm_buf;
}

static unsigned timer_count(void)
{   /* the 8254's counter 0: it counts down 65536 or so 18.2 times a second */
    unsigned lo, hi;
    port_out(0x43,0x00);
    lo = port_in(0x40) & 0xFF; hi = port_in(0x40) & 0xFF;
    return (hi << 8) | lo;
}

static void seed_file(mbedtls_sha256_context *hash)
{   /* mix in the seed file, and leave a different one for next time */
    const char *name = getenv("TLSSEED");
    unsigned char old[64], next[32];
    mbedtls_sha256_context copy;
    size_t n = 0;
    FILE *f;
    if (name == NULL || *name == 0) return;
    f = fopen(name,"rb");
    if (f != NULL) { n = fread(old,1,sizeof old,f); fclose(f); }
    mbedtls_sha256_update(hash,old,n);
    mbedtls_sha256_init(&copy);
    mbedtls_sha256_clone(&copy,hash);
    mbedtls_sha256_update(&copy,(const unsigned char*)"next",4);
    mbedtls_sha256_finish(&copy,next);
    mbedtls_sha256_free(&copy);
    f = fopen(name,"wb");
    if (f != NULL) { fwrite(next,1,sizeof next,f); fclose(f); }
    mbedtls_platform_zeroize(old,sizeof old);
    mbedtls_platform_zeroize(next,sizeof next);
}

static void random_device(mbedtls_sha256_context *hash)
{   /* NOISE.SYS calls its device URANDOM$; others, RANDOM$ */
    static const char *const names[] = { "URANDOM$", "RANDOM$" };
    unsigned i;
    for (i = 0; i < 2; i++) {
        FILE *f = fopen(names[i],"rb");
        if (f != NULL) {
            unsigned char bytes[32];
            size_t n;
            setvbuf(f,NULL,_IONBF,0);
            n = fread(bytes,1,sizeof bytes,f);
            fclose(f);
            mbedtls_sha256_update(hash,bytes,n);
            mbedtls_platform_zeroize(bytes,sizeof bytes);
            return;
        }
    }
}

static volatile unsigned churn;

/* Mbed TLS's source of entropy (MBEDTLS_ENTROPY_HARDWARE_ALT) */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    static unsigned char pool[32];
    static int started;
    static unsigned long polls;
    mbedtls_sha256_context hash;
    unsigned char digest[32];
    unsigned i;
    (void)data;
    mbedtls_sha256_init(&hash);
    mbedtls_sha256_starts(&hash,0);
    mbedtls_sha256_update(&hash,pool,sizeof pool);
    polls++;
    mbedtls_sha256_update(&hash,(const unsigned char*)&polls,sizeof polls);
    /* The cycle counter against the timer chip, 512 times: each sample is
       taken after a little work whose length depends on the last one. */
    for (i = 0; i < 512; i++) {
        struct { unsigned long long c; unsigned t; } sample;
        unsigned spin;
        sample.c = cycles();
        sample.t = timer_count();
        mbedtls_sha256_update(&hash,(const unsigned char*)&sample,sizeof sample);
        for (spin = (unsigned)(sample.c ^ sample.t) & 63; spin > 0; spin--) churn += spin;
    }
    { time_t now = time(NULL); clock_t ticks = clock();
      mbedtls_sha256_update(&hash,(const unsigned char*)&now,sizeof now);
      mbedtls_sha256_update(&hash,(const unsigned char*)&ticks,sizeof ticks); }
    if (!started) {
        started = 1;
        random_device(&hash);
        seed_file(&hash);
    }
    mbedtls_sha256_finish(&hash,digest);
    mbedtls_sha256_free(&hash);
    /* keep half of what we know for the next call, give out the other half */
    mbedtls_sha256(digest,sizeof digest,pool,0);
    digest[0] ^= 0x5A;
    mbedtls_sha256(digest,sizeof digest,digest,0);
    if (len > sizeof digest) len = sizeof digest;
    memcpy(output,digest,len);
    *olen = len;
    mbedtls_platform_zeroize(digest,sizeof digest);
    return 0;
}

/* ----------------------------- the rest ---------------------------- */

static char last_error[200];
static void set_error(const char *what, int code)
{
    if (code != 0) {
        char text[120];
        mbedtls_strerror(code,text,sizeof text);
        sprintf(last_error,"%.60s: %.110s",what,text);
    } else
        sprintf(last_error,"%.190s",what);
}
const char *DOSTLS_CALL dostls_error(void) { return last_error; }

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context random_numbers;
static int started;                     /* 0 not yet, 1 running, -1 failed */

static int startup(void)
{
    if (started == 0) {
        int result;
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&random_numbers);
        result = mbedtls_ctr_drbg_seed(&random_numbers,mbedtls_entropy_func,&entropy,
                                       (const unsigned char*)"dostls",6);
        if (result != 0) { set_error("no random numbers",result); started = -1; return -1; }
        if (psa_crypto_init() != PSA_SUCCESS) {
            set_error("the cryptography library did not start",0); started = -1; return -1;
        }
        started = 1;
    }
    if (started < 0) { set_error("TLS failed to start earlier",0); return -1; }
    return 0;
}

struct dostls_config {
    mbedtls_ssl_config ssl;
    mbedtls_x509_crt authorities;
    int verify;
};

struct dostls {
    mbedtls_ssl_context ssl;
    int fd;
    char problem[160];                  /* why the certificate was refused */
    char text[160];                     /* an answer of dostls_info */
};

static mbedtls_ssl_protocol_version version_of(int v)
{
    return v >= 13 ? MBEDTLS_SSL_VERSION_TLS1_3 : MBEDTLS_SSL_VERSION_TLS1_2;
}

/* VERIFY: 0 accepts any certificate; 1 wants one that the authorities in
 * CA_FILE (PEM; if NULL, the file named by the environment variable
 * SSL_CERT_FILE) vouch for and that is for the host asked for.
 * MIN and MAX: 12 for TLS 1.2, 13 for TLS 1.3; 0 for no preference. */
struct dostls_config *DOSTLS_CALL dostls_config(int verify, const char *ca_file, int min, int max)
{
    struct dostls_config *c;
    int result;
    if (startup() != 0) return NULL;
    c = (struct dostls_config*)calloc(1,sizeof *c);
    if (c == NULL) { set_error("out of memory",0); return NULL; }
    mbedtls_ssl_config_init(&c->ssl);
    mbedtls_x509_crt_init(&c->authorities);
    c->verify = verify;
    result = mbedtls_ssl_config_defaults(&c->ssl,MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) { set_error("configuration",result); goto fail; }
    mbedtls_ssl_conf_rng(&c->ssl,mbedtls_ctr_drbg_random,&random_numbers);
    mbedtls_ssl_conf_min_tls_version(&c->ssl,version_of(min == 0 ? 12 : min));
    mbedtls_ssl_conf_max_tls_version(&c->ssl,version_of(max == 0 ? 13 : max));
    if (verify) {
        if (ca_file == NULL || *ca_file == 0) ca_file = getenv("SSL_CERT_FILE");
        if (ca_file == NULL || *ca_file == 0) {
            set_error("no file of certificate authorities: name one, or set SSL_CERT_FILE",0);
            goto fail;
        }
        result = mbedtls_x509_crt_parse_file(&c->authorities,ca_file);
        if (result < 0) {
            char what[100];
            sprintf(what,"certificate authorities in %.60s",ca_file);
            set_error(what,result);
            goto fail;
        }
        mbedtls_ssl_conf_ca_chain(&c->ssl,&c->authorities,NULL);
        mbedtls_ssl_conf_authmode(&c->ssl,MBEDTLS_SSL_VERIFY_REQUIRED);
    } else
        mbedtls_ssl_conf_authmode(&c->ssl,MBEDTLS_SSL_VERIFY_NONE);
    return c;
 fail:
    dostls_config_free(c);
    return NULL;
}

void DOSTLS_CALL dostls_config_free(struct dostls_config *c)
{
    if (c == NULL) return;
    mbedtls_ssl_config_free(&c->ssl);
    mbedtls_x509_crt_free(&c->authorities);
    free(c);
}

static int send_bytes(void *context, const unsigned char *buf, size_t len)
{
    int n = socket_write(((struct dostls*)context)->fd,buf,len);
    return n < 0 ? -0x004E /* MBEDTLS_ERR_NET_SEND_FAILED */ : n;
}
static int receive_bytes(void *context, unsigned char *buf, size_t len)
{
    int n = socket_read(((struct dostls*)context)->fd,buf,len);
    return n < 0 ? -0x004C /* MBEDTLS_ERR_NET_RECV_FAILED */ : n;
}

static int temporary(int result)
{   /* results that mean "call again" */
    return result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE
        || result == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        || result == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS;
}

/* Do the handshake on the connected socket FD, as a client talking to
 * HOSTNAME (which is sent to the server, and which its certificate must
 * be for; NULL to do neither).  NULL if it fails: see dostls_error. */
struct dostls *DOSTLS_CALL dostls_open(struct dostls_config *config, int fd, const char *hostname)
{
    struct dostls *s;
    int result;
    if (config == NULL) { set_error("no configuration",0); return NULL; }
    s = (struct dostls*)calloc(1,sizeof *s);
    if (s == NULL) { set_error("out of memory",0); return NULL; }
    s->fd = fd;
    mbedtls_ssl_init(&s->ssl);
    result = mbedtls_ssl_setup(&s->ssl,&config->ssl);
    if (result != 0) { set_error("setup",result); goto fail; }
    if (hostname != NULL && *hostname == 0) hostname = NULL;
    result = mbedtls_ssl_set_hostname(&s->ssl,hostname);
    if (result != 0) { set_error("host name",result); goto fail; }
    mbedtls_ssl_set_bio(&s->ssl,s,send_bytes,receive_bytes,NULL);
    do result = mbedtls_ssl_handshake(&s->ssl); while (temporary(result));
    if (result != 0) {
        unsigned flags = mbedtls_ssl_get_verify_result(&s->ssl);
        if (result == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags != 0 && flags != 0xFFFFFFFFu) {
            char why[120];
            int n = mbedtls_x509_crt_verify_info(why,sizeof why,"",flags);
            while (n > 0 && (why[n-1] == '\n' || why[n-1] == ' ')) why[--n] = 0;
            { char *p; for (p = why; *p; p++) if (*p == '\n') *p = ';'; }
            sprintf(last_error,"certificate refused: %.150s",n > 0 ? why : "no reason given");
        } else
            set_error("handshake",result);
        goto fail;
    }
    return s;
 fail:
    mbedtls_ssl_free(&s->ssl);
    free(s);
    return NULL;
}

/* Read up to LEN bytes: how many (at least one), 0 at the end of the
 * data, -1 for an error. */
int DOSTLS_CALL dostls_read(struct dostls *s, void *buf, unsigned len)
{
    int result;
    if (len == 0) return 0;
    do result = mbedtls_ssl_read(&s->ssl,(unsigned char*)buf,len); while (temporary(result));
    if (result > 0) return result;
    if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY
        || result == MBEDTLS_ERR_SSL_CONN_EOF)
        return 0;
    set_error("read",result);
    return -1;
}

/* Write LEN bytes, all of them: LEN, or -1 for an error. */
int DOSTLS_CALL dostls_write(struct dostls *s, const void *buf, unsigned len)
{
    unsigned done = 0;
    while (done < len) {
        int result = mbedtls_ssl_write(&s->ssl,(const unsigned char*)buf + done,len - done);
        if (result > 0) done += result;
        else if (!temporary(result)) { set_error("write",result); return -1; }
    }
    return (int)len;
}

/* Is there something to read, now or within MILLISECONDS?  (At the end of
 * the data, yes: reading will not wait.) */
int DOSTLS_CALL dostls_ready(struct dostls *s, int milliseconds)
{
    if (mbedtls_ssl_get_bytes_avail(&s->ssl) > 0 || mbedtls_ssl_check_pending(&s->ssl)) return 1;
    return socket_ready(s->fd,milliseconds);
}

/* Say goodbye and forget the connection.  The socket stays open. */
void DOSTLS_CALL dostls_close(struct dostls *s)
{
    if (s == NULL) return;
    mbedtls_ssl_close_notify(&s->ssl);
    mbedtls_ssl_free(&s->ssl);
    free(s);
}

/* About the connection.  WHAT: 0 the protocol ("TLSv1.3"), 1 the cipher
 * suite, 2 who the certificate is for, 3 who issued it, 4 when it expires
 * ("2027-01-31 23:59:59").  The answer lasts until the next call for the
 * same connection; "" if there is none. */
const char *DOSTLS_CALL dostls_info(struct dostls *s, int what)
{
    const mbedtls_x509_crt *certificate = mbedtls_ssl_get_peer_cert(&s->ssl);
    const char *text = NULL;
    s->text[0] = 0;
    switch (what) {
    case 0: text = mbedtls_ssl_get_version(&s->ssl); break;
    case 1: text = mbedtls_ssl_get_ciphersuite(&s->ssl); break;
    case 2: if (certificate != NULL
                && mbedtls_x509_dn_gets(s->text,sizeof s->text,&certificate->subject) < 0)
                s->text[0] = 0;
            break;
    case 3: if (certificate != NULL
                && mbedtls_x509_dn_gets(s->text,sizeof s->text,&certificate->issuer) < 0)
                s->text[0] = 0;
            break;
    case 4: if (certificate != NULL) {
                const mbedtls_x509_time *t = &certificate->valid_to;
                sprintf(s->text,"%04d-%02d-%02d %02d:%02d:%02d",
                        t->year,t->mon,t->day,t->hour,t->min,t->sec);
            }
            break;
    }
    if (text != NULL) sprintf(s->text,"%.150s",text);
    return s->text;
}
