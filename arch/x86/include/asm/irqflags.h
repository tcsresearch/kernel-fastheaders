/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _X86_IRQFLAGS_H_
#define _X86_IRQFLAGS_H_

#ifndef __ASSEMBLY__

/* Provide __cpuidle; we can't safely include <linux/cpu.h> */
#define __cpuidle __section(".cpuidle.text")

/*
 * Interrupt control:
 */

/* Declaration required for gcc < 4.9 to prevent -Werror=missing-prototypes */
extern inline unsigned long native_save_fl(void);
extern __always_inline unsigned long native_save_fl(void)
{
	unsigned long flags;

	/*
	 * "=rm" is safe here, because "pop" adjusts the stack before
	 * it evaluates its effective address -- this is part of the
	 * documented behavior of the "pop" instruction.
	 */
	asm volatile("# __raw_save_flags\n\t"
		     "pushf ; pop %0"
		     : "=rm" (flags)
		     : /* no input */
		     : "memory");

	return flags;
}

static __always_inline void native_irq_disable(void)
{
	asm volatile("cli": : :"memory");
}

static __always_inline void native_irq_enable(void)
{
	asm volatile("sti": : :"memory");
}

#endif

#ifdef CONFIG_PARAVIRT_XXL
#include <asm/paravirt.h>
#else
#ifndef __ASSEMBLY__
#include <linux/types.h>

static __always_inline unsigned long arch_local_save_flags(void)
{
	return native_save_fl();
}

static __always_inline void arch_local_irq_disable(void)
{
	native_irq_disable();
}

static __always_inline void arch_local_irq_enable(void)
{
	native_irq_enable();
}

/*
 * For spinlocks, etc:
 */
static __always_inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();
	arch_local_irq_disable();
	return flags;
}
#else

#ifdef CONFIG_X86_64
#ifdef CONFIG_DEBUG_ENTRY
#define SAVE_FLAGS		pushfq; popq %rax
#endif

#endif

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT_XXL */

#ifndef __ASSEMBLY__
static __always_inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & 0x200); /* X86_EFLAGS_IF */
}

static __always_inline int arch_irqs_disabled(void)
{
	unsigned long flags = arch_local_save_flags();

	return arch_irqs_disabled_flags(flags);
}

static __always_inline void arch_local_irq_restore(unsigned long flags)
{
	if (!arch_irqs_disabled_flags(flags))
		arch_local_irq_enable();
}
#else
#ifdef CONFIG_X86_64
#ifdef CONFIG_XEN_PV
#define SWAPGS	ALTERNATIVE "swapgs", "", X86_FEATURE_XENPV
#define INTERRUPT_RETURN						\
	ANNOTATE_RETPOLINE_SAFE;					\
	ALTERNATIVE_TERNARY("jmp *paravirt_iret(%rip);",		\
		X86_FEATURE_XENPV, "jmp xen_iret;", "jmp native_iret;")
#else
#define SWAPGS	swapgs
#define INTERRUPT_RETURN	jmp native_iret
#endif
#endif
#endif /* !__ASSEMBLY__ */

#endif
