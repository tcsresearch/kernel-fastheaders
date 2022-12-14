/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_TASK_H
#define _LINUX_SCHED_TASK_H

/*
 * Interface between the scheduler and various task lifetime (fork()/exit())
 * functionality:
 */

#include <linux/refcount_api.h>
#include <linux/sched/per_task.h>
#include <linux/spinlock_api.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/refcount.h>

DECLARE_PER_TASK(refcount_t, usage);

#ifdef CONFIG_SMP
DECLARE_PER_TASK(int, on_cpu);
#endif

DECLARE_PER_TASK(spinlock_t, alloc_lock);

/* Protection of the PI data structures: */
DECLARE_PER_TASK(raw_spinlock_t, pi_lock);

struct task_struct;
struct rusage;
union thread_union;
struct css_set;

/* All the bits taken by the old clone syscall. */
#define CLONE_LEGACY_FLAGS 0xffffffffULL

struct kernel_clone_args {
	u64 flags;
	int __user *pidfd;
	int __user *child_tid;
	int __user *parent_tid;
	int exit_signal;
	unsigned long stack;
	unsigned long stack_size;
	unsigned long tls;
	pid_t *set_tid;
	/* Number of elements in *set_tid */
	size_t set_tid_size;
	int cgroup;
	int io_thread;
	struct cgroup *cgrp;
	struct css_set *cset;
};

/*
 * This serializes "schedule()" and also protects
 * the run-queue from deletions/modifications (but
 * _adding_ to the beginning of the run-queue has
 * a separate lock).
 */
extern rwlock_t tasklist_lock;
extern spinlock_t mmlist_lock;

extern union thread_union init_thread_union;
extern struct task_struct init_task;

extern int lockdep_tasklist_lock_is_held(void);

extern asmlinkage void schedule_tail(struct task_struct *prev);
extern void init_idle(struct task_struct *idle, int cpu);

extern int sched_fork(unsigned long clone_flags, struct task_struct *p);
extern void sched_cgroup_fork(struct task_struct *p, struct kernel_clone_args *kargs);
extern void sched_post_fork(struct task_struct *p);
extern void sched_dead(struct task_struct *p);

void __noreturn do_task_dead(void);
void __noreturn make_task_dead(int signr);

extern void proc_caches_init(void);

extern void fork_init(void);

extern void release_task(struct task_struct * p);

extern int copy_thread(unsigned long, unsigned long, unsigned long,
		       struct task_struct *, unsigned long);

extern void flush_thread(void);

#ifdef CONFIG_HAVE_EXIT_THREAD
extern void exit_thread(struct task_struct *tsk);
#else
static inline void exit_thread(struct task_struct *tsk)
{
}
#endif
extern void do_group_exit(int);

extern void exit_files(struct task_struct *);
extern void exit_itimers(struct signal_struct *);

extern pid_t kernel_clone(struct kernel_clone_args *kargs);
struct task_struct *create_io_thread(int (*fn)(void *), void *arg, int node);
struct task_struct *fork_idle(int);
struct mm_struct *copy_init_mm(void);
extern pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);
extern long kernel_wait4(pid_t, int __user *, int, struct rusage *);
int kernel_wait(pid_t pid, int *stat);

extern void free_task(struct task_struct *tsk);

/* sched_exec is called by processes performing an exec */
#ifdef CONFIG_SMP
extern void sched_exec(void);
#else
#define sched_exec()   {}
#endif

static inline struct task_struct *get_task_struct(struct task_struct *t)
{
	refcount_inc(&per_task(t, usage));
	return t;
}

extern void __put_task_struct(struct task_struct *t);

static inline void put_task_struct(struct task_struct *t)
{
	if (refcount_dec_and_test(&per_task(t, usage)))
		__put_task_struct(t);
}

static inline void put_task_struct_many(struct task_struct *t, int nr)
{
	if (refcount_sub_and_test(nr, &per_task(t, usage)))
		__put_task_struct(t);
}

void put_task_struct_rcu_user(struct task_struct *task);

#ifdef CONFIG_ARCH_WANTS_DYNAMIC_TASK_STRUCT
extern int arch_task_struct_size __read_mostly;
#else
# define arch_task_struct_size (sizeof(struct task_struct))
#endif

/*
 * Protects ->fs, ->files, ->mm, ->group_info, ->comm, keyring
 * subscriptions and synchronises with wait4().  Also used in procfs.  Also
 * pins the final release of task.io_context.  Also protects ->cpuset and
 * ->cgroup.subsys[]. And ->vfork_done. And ->sysvshm.shm_clist.
 *
 * Nests both inside and outside of read_lock(&tasklist_lock).
 * It must not be nested with write_lock_irq(&tasklist_lock),
 * neither inside nor outside.
 */
static inline void task_lock(struct task_struct *p)
{
	spin_lock(&per_task(p, alloc_lock));
}

static inline void task_unlock(struct task_struct *p)
{
	spin_unlock(&per_task(p, alloc_lock));
}

static inline bool task_on_another_cpu(struct task_struct *task)
{
#ifdef CONFIG_SMP
	return task != current && per_task(task, on_cpu);
#else
	return false;
#endif
}

#define TASK_REPORT_IDLE	(TASK_REPORT + 1)
#define TASK_REPORT_MAX		(TASK_REPORT_IDLE << 1)

static inline unsigned int __task_state_index(unsigned int tsk_state,
					      unsigned int tsk_exit_state)
{
	unsigned int state = (tsk_state | tsk_exit_state) & TASK_REPORT;

	BUILD_BUG_ON_NOT_POWER_OF_2(TASK_REPORT_MAX);

	if (tsk_state == TASK_IDLE)
		state = TASK_REPORT_IDLE;

	/*
	 * We're lying here, but rather than expose a completely new task state
	 * to userspace, we can make this appear as if the task has gone through
	 * a regular rt_mutex_lock() call.
	 */
	if (tsk_state == TASK_RTLOCK_WAIT)
		state = TASK_UNINTERRUPTIBLE;

	return fls(state);
}

static inline unsigned int task_state_index(struct task_struct *tsk)
{
	return __task_state_index(READ_ONCE(tsk->__state), tsk->exit_state);
}

extern char task_index_to_char(unsigned int state);

static inline char task_state_to_char(struct task_struct *tsk)
{
	return task_index_to_char(task_state_index(tsk));
}

#endif /* _LINUX_SCHED_TASK_H */
