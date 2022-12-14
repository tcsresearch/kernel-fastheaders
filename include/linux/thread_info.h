/* SPDX-License-Identifier: GPL-2.0 */
/* thread_info.h: common low-level thread information accessors
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds
 */

#ifndef _LINUX_THREAD_INFO_H
#define _LINUX_THREAD_INFO_H

#include <vdso/limits.h>

#include <linux/types.h>
#include <linux/limits.h>
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/bitops.h>

#ifdef CONFIG_THREAD_INFO_IN_TASK
/* We are racing with <linux/sched/thread_info.h> to do this: */
# ifndef current_thread_info
  /*
   * For CONFIG_THREAD_INFO_IN_TASK kernels we need <asm/current.h> for the
   * definition of current, but for !CONFIG_THREAD_INFO_IN_TASK kernels,
   * including <asm/current.h> can cause a circular dependency on some platforms.
   */
# include <asm/current.h>
# define current_thread_info() task_thread_info(current)
# endif
#endif

/*
 * For per-arch arch_within_stack_frames() implementations, defined in
 * asm/thread_info.h.
 */
enum {
	BAD_STACK = -1,
	NOT_STACK = 0,
	GOOD_FRAME,
	GOOD_STACK,
};

#ifdef CONFIG_GENERIC_ENTRY
enum syscall_work_bit {
	SYSCALL_WORK_BIT_SECCOMP,
	SYSCALL_WORK_BIT_SYSCALL_TRACEPOINT,
	SYSCALL_WORK_BIT_SYSCALL_TRACE,
	SYSCALL_WORK_BIT_SYSCALL_EMU,
	SYSCALL_WORK_BIT_SYSCALL_AUDIT,
	SYSCALL_WORK_BIT_SYSCALL_USER_DISPATCH,
	SYSCALL_WORK_BIT_SYSCALL_EXIT_TRAP,
};

#define SYSCALL_WORK_SECCOMP		BIT(SYSCALL_WORK_BIT_SECCOMP)
#define SYSCALL_WORK_SYSCALL_TRACEPOINT	BIT(SYSCALL_WORK_BIT_SYSCALL_TRACEPOINT)
#define SYSCALL_WORK_SYSCALL_TRACE	BIT(SYSCALL_WORK_BIT_SYSCALL_TRACE)
#define SYSCALL_WORK_SYSCALL_EMU	BIT(SYSCALL_WORK_BIT_SYSCALL_EMU)
#define SYSCALL_WORK_SYSCALL_AUDIT	BIT(SYSCALL_WORK_BIT_SYSCALL_AUDIT)
#define SYSCALL_WORK_SYSCALL_USER_DISPATCH BIT(SYSCALL_WORK_BIT_SYSCALL_USER_DISPATCH)
#define SYSCALL_WORK_SYSCALL_EXIT_TRAP	BIT(SYSCALL_WORK_BIT_SYSCALL_EXIT_TRAP)
#endif

#include <asm/thread_info.h>

#include <linux/sched/thread_info_api_lowlevel.h>

#ifdef __KERNEL__

#ifndef THREAD_ALIGN
#define THREAD_ALIGN	THREAD_SIZE
#endif

#define THREADINFO_GFP		(GFP_KERNEL_ACCOUNT | __GFP_ZERO)

#ifndef CONFIG_HAVE_ARCH_WITHIN_STACK_FRAMES
static inline int arch_within_stack_frames(const void * const stack,
					   const void * const stackend,
					   const void *obj, unsigned long len)
{
	return 0;
}
#endif

#ifndef arch_setup_new_exec
static inline void arch_setup_new_exec(void) { }
#endif

#endif	/* __KERNEL__ */

#endif /* _LINUX_THREAD_INFO_H */
