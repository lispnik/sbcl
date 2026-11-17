/* DOS port: no shared objects; see os_dlsym_default() in dos-os.c */
#ifndef SBCL_DOS_DLFCN_H
#define SBCL_DOS_DLFCN_H
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100
#endif
