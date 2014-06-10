/* Taken from https://github.com/jlelli/schedtool-dl/blob/3ffb479929c31cbae09de08f94f58b8f0f061d91/syscall_magic.h */

#include <linux/kernel.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include <time.h>

#define SCHED_DEADLINE			6

/* XXX use the proper syscall numbers */
#ifdef __x86_64__
#define __NR_sched_setattr		314
#define __NR_sched_getattr		315
#endif

#ifdef __i386__
#define __NR_sched_setattr		351
#define __NR_sched_getattr		352
#endif

#ifdef __arm__
#define __NR_sched_setattr		380
#define __NR_sched_getattr		381
#endif

#define SF_SIG_RORUN	2
#define SF_SIG_DMISS	4

struct sched_attr {
	__u32 size;

	__u32 sched_policy;
	__u64 sched_flags;

	/* SCHED_NORMAL, SCHED_BATCH */
	__s32 sched_nice;

	/* SCHED_FIFO, SCHED_RR */
	__u32 sched_priority;

	/* SCHED_DEADLINE */
	__u64 sched_runtime;
	__u64 sched_deadline;
	__u64 sched_period;
};

#define sched_getattr(pid, attr, size, flags) \
	syscall(__NR_sched_getattr, pid, attr, size, flags)

#define sched_setattr(pid, attr, flags) \
	syscall(__NR_sched_setattr, pid, attr, flags)
