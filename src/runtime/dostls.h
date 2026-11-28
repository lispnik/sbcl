/* TLS for Lisp on DOS, on Mbed TLS: see dostls.c.  Public domain. */
#ifndef DOSTLS_H
#define DOSTLS_H

#ifdef __WATCOMC__
#define DOSTLS_CALL __cdecl
#else
#define DOSTLS_CALL
#endif

struct dostls_config;
struct dostls;

struct dostls_config *DOSTLS_CALL dostls_config(int verify, const char *ca_file, int min, int max);
void DOSTLS_CALL dostls_config_free(struct dostls_config *config);
struct dostls *DOSTLS_CALL dostls_open(struct dostls_config *config, int fd, const char *hostname);
int DOSTLS_CALL dostls_read(struct dostls *s, void *buf, unsigned len);
int DOSTLS_CALL dostls_write(struct dostls *s, const void *buf, unsigned len);
int DOSTLS_CALL dostls_ready(struct dostls *s, int milliseconds);
void DOSTLS_CALL dostls_close(struct dostls *s);
const char *DOSTLS_CALL dostls_info(struct dostls *s, int what);
const char *DOSTLS_CALL dostls_error(void);

#endif
