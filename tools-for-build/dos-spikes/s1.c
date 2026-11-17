/* S1: which DPMI host are we on, and is the client flat/zero-based? */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <i86.h>

static unsigned short get_cs(void); 
#pragma aux get_cs = "mov ax,cs" value [ax];
static unsigned short get_ds(void); 
#pragma aux get_ds = "mov ax,ds" value [ax];
static unsigned short get_ss(void); 
#pragma aux get_ss = "mov ax,ss" value [ax];

static unsigned long seg_base(unsigned short sel) {
    union REGS r; memset(&r, 0, sizeof r);
    r.w.ax = 6; r.w.bx = sel; int386(0x31, &r, &r);
    if (r.x.cflag) return 0xFFFFFFFF;
    return ((unsigned long)r.w.cx << 16) | r.w.dx;
}
int main(int argc, char **argv) {
    union REGS r; struct SREGS s; static unsigned char buf[128]; int local;
    memset(&r, 0, sizeof r);
    r.w.ax = 0x400; int386(0x31, &r, &r);
    printf("DPMI version %d.%d flags %04x cpu %d\n", r.h.ah, r.h.al, r.w.bx, r.h.cl);
    memset(&r, 0, sizeof r); segread(&s);
    r.w.ax = 0x401; r.x.edi = (unsigned)buf; int386x(0x31, &r, &r, &s);
    if (r.x.cflag) printf("0401h unsupported\n");
    else printf("0401h caps %04x host %d.%d vendor '%s'\n", r.w.ax, buf[0], buf[1], buf + 2);
    printf("cs=%04x base %08lx  ds=%04x base %08lx  ss=%04x base %08lx\n",
           get_cs(), seg_base(get_cs()), get_ds(), seg_base(get_ds()), get_ss(), seg_base(get_ss()));
    printf("main=%p static=%p stack=%p malloc(16)=%p malloc(1M)=%p argv0=%s\n",
           (void*)main, (void*)buf, (void*)&local, malloc(16), malloc(1 << 20), argv[0]);
    return 0;
}
