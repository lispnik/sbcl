/* S8: does redirected stdout survive creating/closing other files (FreeDOS + DOSLFN)? */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <unistd.h>
#include <dos.h>
#include <i86.h>
static void out(const char *s) { write(1, s, strlen(s)); }
int main(int argc, char **argv) {
    int fd, i; char line[80];
    const char *name = argc > 1 ? argv[1] : "A Long File Name.lisp-expr";
    for (i = 0; i < 10; i++) { sprintf(line, "before %d: 0123456789012345678901234567890123456789\r\n", i); out(line); }
    if (argc > 2 && argv[2][0] == 'c') _dos_commit(1);
    if (argc > 2 && argv[2][0] == 'r') { union REGS r; r.h.ah = 0x0d; int386(0x21, &r, &r); }
    fd = open(name, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0666);
    write(fd, "hello\r\n", 7);
    close(fd);
    for (i = 0; i < 3; i++) { sprintf(line, "after %d\r\n", i); out(line); }
    fd = open(name, O_RDONLY | O_BINARY); read(fd, line, 7); close(fd);
    out("after reading back\r\n");
    return 0;
}
