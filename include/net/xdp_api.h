/* SPDX-License-Identifier: GPL-2.0-only */
/* include/net/xdp.h
 *
 * Copyright (c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 */
#ifndef __LINUX_NET_XDP_API_H
#define __LINUX_NET_XDP_API_H

#include <net/xdp_types.h>

#include <linux/err.h>
#include <linux/align.h>
#include <linux/skbuff.h>

static __always_inline void
xdp_init_buff(struct xdp_buff *xdp, u32 frame_sz, struct xdp_rxq_info *rxq)
{
	xdp->frame_sz = frame_sz;
	xdp->rxq = rxq;
}

static __always_inline void
xdp_prepare_buff(struct xdp_buff *xdp, unsigned char *hard_start,
		 int headroom, int data_len, const bool meta_valid)
{
	unsigned char *data = hard_start + headroom;

	xdp->data_hard_start = hard_start;
	xdp->data = data;
	xdp->data_end = data + data_len;
	xdp->data_meta = meta_valid ? data : data + 1;
}

/* Reserve memory area at end-of data area.
 *
 * This macro reserves tailroom in the XDP buffer by limiting the
 * XDP/BPF data access to data_hard_end.  Notice same area (and size)
 * is used for XDP_PASS, when constructing the SKB via build_skb().
 */
#define xdp_data_hard_end(xdp)				\
	((xdp)->data_hard_start + (xdp)->frame_sz -	\
	 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

static inline struct skb_shared_info *
xdp_get_shared_info_from_buff(struct xdp_buff *xdp)
{
	return (struct skb_shared_info *)xdp_data_hard_end(xdp);
}

static __always_inline void xdp_frame_bulk_init(struct xdp_frame_bulk *bq)
{
	/* bq->count will be zero'ed when bq->xa gets updated */
	bq->xa = NULL;
}

static inline struct skb_shared_info *
xdp_get_shared_info_from_frame(struct xdp_frame *frame)
{
	void *data_hard_start = frame->data - frame->headroom - sizeof(*frame);

	return (struct skb_shared_info *)(data_hard_start + frame->frame_sz -
				SKB_DATA_ALIGN(sizeof(struct skb_shared_info)));
}

/* Clear kernel pointers in xdp_frame */
static inline void xdp_scrub_frame(struct xdp_frame *frame)
{
	frame->data = NULL;
	frame->dev_rx = NULL;
}

/* Avoids inlining WARN macro in fast-path */
void xdp_warn(const char *msg, const char *func, const int line);
#define XDP_WARN(msg) xdp_warn(msg, __func__, __LINE__)

struct xdp_frame *xdp_convert_zc_to_xdp_frame(struct xdp_buff *xdp);
struct sk_buff *__xdp_build_skb_from_frame(struct xdp_frame *xdpf,
					   struct sk_buff *skb,
					   struct net_device *dev);
struct sk_buff *xdp_build_skb_from_frame(struct xdp_frame *xdpf,
					 struct net_device *dev);
int xdp_alloc_skb_bulk(void **skbs, int n_skb, gfp_t gfp);
struct xdp_frame *xdpf_clone(struct xdp_frame *xdpf);

static inline
void xdp_convert_frame_to_buff(struct xdp_frame *frame, struct xdp_buff *xdp)
{
	xdp->data_hard_start = frame->data - frame->headroom - sizeof(*frame);
	xdp->data = frame->data;
	xdp->data_end = frame->data + frame->len;
	xdp->data_meta = frame->data - frame->metasize;
	xdp->frame_sz = frame->frame_sz;
}

static inline
int xdp_update_frame_from_buff(struct xdp_buff *xdp,
			       struct xdp_frame *xdp_frame)
{
	int metasize, headroom;

	/* Assure headroom is available for storing info */
	headroom = xdp->data - xdp->data_hard_start;
	metasize = xdp->data - xdp->data_meta;
	metasize = metasize > 0 ? metasize : 0;
	if (unlikely((headroom - metasize) < sizeof(*xdp_frame)))
		return -ENOSPC;

	/* Catch if driver didn't reserve tailroom for skb_shared_info */
	if (unlikely(xdp->data_end > xdp_data_hard_end(xdp))) {
		XDP_WARN("Driver BUG: missing reserved tailroom");
		return -ENOSPC;
	}

	xdp_frame->data = xdp->data;
	xdp_frame->len  = xdp->data_end - xdp->data;
	xdp_frame->headroom = headroom - sizeof(*xdp_frame);
	xdp_frame->metasize = metasize;
	xdp_frame->frame_sz = xdp->frame_sz;

	return 0;
}

/* Convert xdp_buff to xdp_frame */
static inline
struct xdp_frame *xdp_convert_buff_to_frame(struct xdp_buff *xdp)
{
	struct xdp_frame *xdp_frame;

	if (xdp->rxq->mem.type == MEM_TYPE_XSK_BUFF_POOL)
		return xdp_convert_zc_to_xdp_frame(xdp);

	/* Store info in top of packet */
	xdp_frame = xdp->data_hard_start;
	if (unlikely(xdp_update_frame_from_buff(xdp, xdp_frame) < 0))
		return NULL;

	/* rxq only valid until napi_schedule ends, convert to xdp_mem_info */
	xdp_frame->mem = xdp->rxq->mem;

	return xdp_frame;
}

void xdp_return_frame(struct xdp_frame *xdpf);
void xdp_return_frame_rx_napi(struct xdp_frame *xdpf);
void xdp_return_buff(struct xdp_buff *xdp);
void xdp_flush_frame_bulk(struct xdp_frame_bulk *bq);
void xdp_return_frame_bulk(struct xdp_frame *xdpf,
			   struct xdp_frame_bulk *bq);

/* When sending xdp_frame into the network stack, then there is no
 * return point callback, which is needed to release e.g. DMA-mapping
 * resources with page_pool.  Thus, have explicit function to release
 * frame resources.
 */
void __xdp_release_frame(void *data, struct xdp_mem_info *mem);
static inline void xdp_release_frame(struct xdp_frame *xdpf)
{
	struct xdp_mem_info *mem = &xdpf->mem;

	/* Curr only page_pool needs this */
	if (mem->type == MEM_TYPE_PAGE_POOL)
		__xdp_release_frame(xdpf->data, mem);
}

int xdp_rxq_info_reg(struct xdp_rxq_info *xdp_rxq,
		     struct net_device *dev, u32 queue_index, unsigned int napi_id);
void xdp_rxq_info_unreg(struct xdp_rxq_info *xdp_rxq);
void xdp_rxq_info_unused(struct xdp_rxq_info *xdp_rxq);
bool xdp_rxq_info_is_reg(struct xdp_rxq_info *xdp_rxq);
int xdp_rxq_info_reg_mem_model(struct xdp_rxq_info *xdp_rxq,
			       enum xdp_mem_type type, void *allocator);
void xdp_rxq_info_unreg_mem_model(struct xdp_rxq_info *xdp_rxq);
int xdp_reg_mem_model(struct xdp_mem_info *mem,
		      enum xdp_mem_type type, void *allocator);
void xdp_unreg_mem_model(struct xdp_mem_info *mem);

/* Drivers not supporting XDP metadata can use this helper, which
 * rejects any room expansion for metadata as a result.
 */
static __always_inline void
xdp_set_data_meta_invalid(struct xdp_buff *xdp)
{
	xdp->data_meta = xdp->data + 1;
}

static __always_inline bool
xdp_data_meta_unsupported(const struct xdp_buff *xdp)
{
	return unlikely(xdp->data_meta > xdp->data);
}

static inline bool xdp_metalen_invalid(unsigned long metalen)
{
	return (metalen & (sizeof(__u32) - 1)) || (metalen > 32);
}

struct netdev_bpf;
void xdp_attachment_setup(struct xdp_attachment_info *info,
			  struct netdev_bpf *bpf);

#define DEV_MAP_BULK_SIZE XDP_BULK_QUEUE_SIZE

#endif /* __LINUX_NET_XDP_API_H */
