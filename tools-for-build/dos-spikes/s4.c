#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <sys/stat.h>
#include <io.h>
#include <fcntl.h>
#include <unistd.h>
extern long long asm_add64(long long, long long);
extern double asm_call_c(double);
extern int asm_read_data(void);
int c_counter = 5;
double c_twice(double x) { char buf[64]; sprintf(buf, "%f", x); return 2 * atof(buf); }
long long c_ret64(void) { return 0x123456789ABCDEF0LL; }
int main(void) {
    struct stat st; DIR *d; struct dirent *e; char cwd[300]; FILE *f; int fd; size_t z = 42;
    const char *ln = "A Long File Name with.several.dots.lisp-expr";
    printf("add64 %llx (want 300000001)\n", asm_add64(0x1FFFFFFFFLL, 0x100000002LL));
    printf("call_c %f (want 8.000000) counter %d (want 6)\n", asm_call_c(3.5), c_counter);
    printf("data %x (want 1006)  %%zu=%zu ret64=%llx\n", asm_read_data(), z, c_ret64());
    /* S5: long file names */
    mkdir("Long Directory Name.d"); 
    f = fopen(ln, "wb"); if (!f) { printf("fopen long name FAILED\n"); return 1; }
    fputs("hello\n", f); fclose(f);
    { int r = stat(ln, &st); printf("stat long: %d size %ld sizeof(st_size)=%d sizeof(stat)=%d mode %o\n", r, (long)st.st_size, (int)sizeof st.st_size, (int)sizeof st, (unsigned)st.st_mode); }
    printf("rename: %d\n", rename(ln, "Long Directory Name.d/Renamed Long File.fasl"));
    fd = open("Long Directory Name.d/Renamed Long File.fasl", O_RDONLY | O_BINARY);
    printf("open renamed: fd %d\n", fd); if (fd >= 0) close(fd);
    d = opendir("Long Directory Name.d");
    if (d) { while ((e = readdir(d))) printf("  dirent '%s'\n", e->d_name); closedir(d); }
    chdir("Long Directory Name.d"); printf("getcwd: %s\n", getcwd(cwd, sizeof cwd));
    printf("_fullpath: %s\n", _fullpath(cwd, "Renamed Long File.fasl", sizeof cwd));
    return 0;
}
