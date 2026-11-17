#ifndef SBCL_DOS_SYS_RESOURCE_H
#define SBCL_DOS_SYS_RESOURCE_H
#include <sys/time.h>
#define RUSAGE_SELF 0
struct rusage {
    struct timeval ru_utime, ru_stime;
    long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt, ru_nswap,
         ru_inblock, ru_oublock, ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw;
};
int getrusage(int, struct rusage *);
#endif
