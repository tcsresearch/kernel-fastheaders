// SPDX-License-Identifier: GPL-2.0

#include <linux/pgtable_api.h>
#include <linux/mm_api.h>
#include <linux/io.h>
#include <linux/io_extra.h>
#include <linux/types.h>
#include <linux/mmdebug.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/sections.h>

phys_addr_t __virt_to_phys(unsigned long x)
{
	/*
	 * Boundary checking aginst the kernel linear mapping space.
	 */
	WARN(!is_linear_mapping(x) && !is_kernel_mapping(x),
	     "virt_to_phys used for non-linear address: %pK (%pS)\n",
	     (void *)x, (void *)x);

	return __va_to_pa_nodebug(x);
}
EXPORT_SYMBOL(__virt_to_phys);

phys_addr_t __phys_addr_symbol(unsigned long x)
{
	unsigned long kernel_start = kernel_map.virt_addr;
	unsigned long kernel_end = (unsigned long)_end;

	/*
	 * Boundary checking aginst the kernel image mapping.
	 * __pa_symbol should only be used on kernel symbol addresses.
	 */
	VIRTUAL_BUG_ON(x < kernel_start || x > kernel_end);

	return __va_to_pa_nodebug(x);
}
EXPORT_SYMBOL(__phys_addr_symbol);
