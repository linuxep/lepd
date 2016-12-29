#include "iotop.h"

#include <sys/syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>

enum {
    IOPRIO_CLASS_NONE,
    IOPRIO_CLASS_RT,
    IOPRIO_CLASS_BE,
    IOPRIO_CLASS_IDLE,
    IOPRIO_CLASS_MAX
};

enum {
    IOPRIO_WHO_PROCESS = 1,
    IOPRIO_WHO_PGRP,
    IOPRIO_WHO_USER
};

#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_STR_MAXSIZ  5
#define IOPRIO_STR_FORMAT "%2s/%1i"

#if defined(__i386__)
#define __NR_ioprio_set         289
#define __NR_ioprio_get         290
#elif defined(__ppc__)
#define __NR_ioprio_set         273
#define __NR_ioprio_get         274
#elif defined(__x86_64__)
#define __NR_ioprio_set         251
#define __NR_ioprio_get         252
#elif defined(__ia64__)
#define __NR_ioprio_set         1274
#define __NR_ioprio_get         1275
//#else
//#error "Unsupported arch"
#endif

# define SYS_ioprio_set __NR_ioprio_set
# define SYS_ioprio_get __NR_ioprio_get

const char *str_ioprio_class[] = { "-", "rt", "be", "id" };

inline int get_ioprio(pid_t pid)
{
    return syscall(SYS_ioprio_get, IOPRIO_WHO_PROCESS, pid);
}

const char *str_ioprio(int io_prio)
{
    const static char corrupted[] = "xx/x";
    static char buf[IOPRIO_STR_MAXSIZ];
    int io_class = io_prio >> IOPRIO_CLASS_SHIFT;

    io_prio &= 0xff;

    if (io_class >= IOPRIO_CLASS_MAX)
        return corrupted;

    snprintf(
        buf,
        IOPRIO_STR_MAXSIZ,
        IOPRIO_STR_FORMAT,
        str_ioprio_class[io_class],
        io_prio
    );

    return (const char *) buf;
}

        

const char *str_ioprio_ext(int io_prio, pid_t pid)
{
#define PRIO_PROCESS 0
#define SCHED_IDLE   5
    const static char corrupted[] = "xx/x";
    static char buf[IOPRIO_STR_MAXSIZ];
    int io_class = io_prio >> IOPRIO_CLASS_SHIFT;

    io_prio &= 0xff;

    if (io_class >= IOPRIO_CLASS_MAX)
        return corrupted;

    if(io_class == 0){
	int scheduler = sched_getscheduler(pid);
	int nice = getpriority(PRIO_PROCESS, pid);
	io_prio = (nice + 20)/5;

	if(scheduler == SCHED_FIFO || scheduler == SCHED_RR)
		io_class = IOPRIO_CLASS_RT;
	else if(scheduler == SCHED_IDLE)
		io_class = IOPRIO_CLASS_IDLE;
	else
		io_class = IOPRIO_CLASS_BE;
    }

    snprintf(
        buf,
        IOPRIO_STR_MAXSIZ,
        IOPRIO_STR_FORMAT,
        str_ioprio_class[io_class],
        io_prio
    );

    return (const char *) buf;
}

