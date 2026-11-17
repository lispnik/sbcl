/* DOS port: only the protection flags are meaningful; see os_protect(). */
#ifndef SBCL_DOS_MMAN_H
#define SBCL_DOS_MMAN_H
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#endif
