// SPDX-License-Identifier: GPL-2.0
#include <linux/sched/thread_info_api.h>
#include <linux/sched/thread.h>
#include <linux/ptrace_api.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <asm/processor.h>
#include <asm/fpu.h>
#include <asm/traps.h>
#include <asm/ptrace.h>

int init_fpu(struct task_struct *tsk)
{
	if (tsk_used_math(tsk)) {
		if ((boot_cpu_data.flags & CPU_HAS_FPU) && tsk == current)
			unlazy_fpu(tsk, task_pt_regs(tsk));
		return 0;
	}

	/*
	 * Memory allocation at the first usage of the FPU and other state.
	 */
	if (!task_thread(tsk).xstate) {
		task_thread(tsk).xstate = kmem_cache_alloc(task_xstate_cachep,
						      GFP_KERNEL);
		if (!task_thread(tsk).xstate)
			return -ENOMEM;
	}

	if (boot_cpu_data.flags & CPU_HAS_FPU) {
		struct sh_fpu_hard_struct *fp = &task_thread(tsk).xstate->hardfpu;
		memset(fp, 0, xstate_size);
		fp->fpscr = FPSCR_INIT;
	} else {
		struct sh_fpu_soft_struct *fp = &task_thread(tsk).xstate->softfpu;
		memset(fp, 0, xstate_size);
		fp->fpscr = FPSCR_INIT;
	}

	set_stopped_child_used_math(tsk);
	return 0;
}

#ifdef CONFIG_SH_FPU
void __fpu_state_restore(void)
{
	struct task_struct *tsk = current;

	restore_fpu(tsk);

	task_thread_info(tsk)->status |= TS_USEDFPU;
	task_thread(tsk).fpu_counter++;
}

void fpu_state_restore(struct pt_regs *regs)
{
	struct task_struct *tsk = current;

	if (unlikely(!user_mode(regs))) {
		printk(KERN_ERR "BUG: FPU is used in kernel mode.\n");
		BUG();
		return;
	}

	if (!tsk_used_math(tsk)) {
		int ret;
		/*
		 * does a slab alloc which can sleep
		 */
		local_irq_enable();
		ret = init_fpu(tsk);
		local_irq_disable();
		if (ret) {
			/*
			 * ran out of memory!
			 */
			force_sig(SIGKILL);
			return;
		}
	}

	grab_fpu(regs);

	__fpu_state_restore();
}

BUILD_TRAP_HANDLER(fpu_state_restore)
{
	TRAP_HANDLER_DECL;

	fpu_state_restore(regs);
}
#endif /* CONFIG_SH_FPU */
