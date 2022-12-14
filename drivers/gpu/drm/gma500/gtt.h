/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************************
 * Copyright (c) 2007-2008, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#ifndef _PSB_GTT_H_
#define _PSB_GTT_H_

#include <linux/ioport.h>
#include <drm/drm_gem.h>

struct drm_psb_private;

/* This wants cleaning up with respect to the psb_dev and un-needed stuff */
struct psb_gtt {
	uint32_t gatt_start;
	uint32_t mmu_gatt_start;
	uint32_t gtt_start;
	uint32_t gtt_phys_start;
	unsigned gtt_pages;
	unsigned gatt_pages;
	unsigned long stolen_size;
	unsigned long vram_stolen_size;
	struct rw_semaphore sem;
};

/* Exported functions */
extern int psb_gtt_init(struct drm_device *dev, int resume);
extern void psb_gtt_takedown(struct drm_device *dev);
extern int psb_gtt_restore(struct drm_device *dev);

int psb_gtt_allocate_resource(struct drm_psb_private *pdev, struct resource *res,
			      const char *name, resource_size_t size, resource_size_t align,
			      bool stolen, u32 *offset);

void psb_gtt_insert_pages(struct drm_psb_private *pdev, const struct resource *res,
			  struct page **pages);
void psb_gtt_remove_pages(struct drm_psb_private *pdev, const struct resource *res);

#endif
