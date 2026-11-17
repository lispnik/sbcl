/* S7: C dialect probes for the SBCL runtime under Open Watcom */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define __attribute__(x)
struct a { char c; double d; char e; uint64_t q; short s; };
struct b { char c; int i; } __attribute__((packed));
#pragma pack(push, 1)
struct pk { char c; int i; };
#pragma pack(pop)
static inline int sq(int x) { return x * x; }
static void die(void) __attribute__((noreturn));
static void die(void) { for (;;) ; }
typedef struct { int x, y; } pt;
static pt mk(void) { pt p = { .y = 2, .x = 1 }; return p; }
#define ALIGN_UP(v, a) (((v) + ((a) - 1)) & ~((uintptr_t)(a) - 1))
static void *frame(void);
#pragma aux frame = "mov eax,ebp" value [eax];
static unsigned sp(void);
#pragma aux sp = "mov eax,esp" value [eax];
int main(int argc, char **argv __attribute__((unused))) {
    bool t = true; uintptr_t u = 0x12345678; long long ll = -5;
    int total = 0;
    for (int i = 0; i < 4; i++) total += sq(i);
    int late = total * 2;              /* mixed declaration */
    double dbl; char pad; double dbl2;
    printf("offsets a: d=%d e=%d q=%d s=%d size=%d | b size %d | pk size %d\n",
           (int)offsetof(struct a, d), (int)offsetof(struct a, e), (int)offsetof(struct a, q),
           (int)offsetof(struct a, s), (int)sizeof(struct a), (int)sizeof(struct b), (int)sizeof(struct pk));
    printf("total %d late %d bool %d %" PRIxPTR " %lld %zu pt %d,%d up %lu\n", total, late, t, u, ll,
           sizeof(long), mk().x, mk().y, (unsigned long)ALIGN_UP(u, 4096));
    printf("frame %p sp %08x  &dbl%%8=%d &dbl2%%8=%d sizeof(long double)=%d\n", frame(), sp(),
           (int)((uintptr_t)&dbl % 8), (int)((uintptr_t)&dbl2 % 8), (int)sizeof(long double));
    if (argc > 5) die();
    return 0;
}
