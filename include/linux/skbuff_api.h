/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Definitions for the 'struct sk_buff' memory handlers.
 *
 *	Authors:
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Florian La Roche, <rzsfl@rz.uni-sb.de>
 */

#ifndef _LINUX_SKBUFF_API_H
#define _LINUX_SKBUFF_API_H

#include <linux/list.h>
#include <linux/cache.h>
#include <linux/uio_api.h>
#include <linux/rcupdate_api_debug.h>
#include <linux/numa_types.h>
#include <linux/gfp_types.h>
#include <linux/rbtree_api.h>
#include <linux/skbuff_types.h>
#include <linux/log2.h>

#include <linux/lockdep_api.h>
#include <linux/bvec.h>
#include <linux/socket.h>
#include <linux/ktime_api.h>
#include <linux/if_packet.h>
#include <linux/netdev_features.h>
#include <linux/refcount_api.h>

#include <linux/siphash_types.h>

#include <asm/processor_types.h>

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
# include <linux/netfilter/nf_conntrack_common.h>
#endif

struct flow_keys_basic;
struct flow_dissector_key;
struct flow_dissector;
struct net;

#define skb_uarg(SKB)	((struct ubuf_info *)(skb_shinfo(SKB)->destructor_arg))

int mm_account_pinned_pages(struct mmpin *mmp, size_t size);
void mm_unaccount_pinned_pages(struct mmpin *mmp);

struct ubuf_info *msg_zerocopy_alloc(struct sock *sk, size_t size);
struct ubuf_info *msg_zerocopy_realloc(struct sock *sk, size_t size,
				       struct ubuf_info *uarg);

void msg_zerocopy_put_abort(struct ubuf_info *uarg, bool have_uref);

void msg_zerocopy_callback(struct sk_buff *skb, struct ubuf_info *uarg,
			   bool success);

int skb_zerocopy_iter_dgram(struct sk_buff *skb, struct msghdr *msg, int len);
int skb_zerocopy_iter_stream(struct sock *sk, struct sk_buff *skb,
			     struct msghdr *msg, int len,
			     struct ubuf_info *uarg);

/*
 *	Handling routines are only of interest to the kernel
 */

#define SKB_ALLOC_FCLONE	0x01
#define SKB_ALLOC_RX		0x02
#define SKB_ALLOC_NAPI		0x04

/**
 * skb_pfmemalloc - Test if the skb was allocated from PFMEMALLOC reserves
 * @skb: buffer
 */
static inline bool skb_pfmemalloc(const struct sk_buff *skb)
{
	return unlikely(skb->pfmemalloc);
}

/*
 * skb might have a dst pointer attached, refcounted or not.
 * _skb_refdst low order bit is set if refcount was _not_ taken
 */
#define SKB_DST_NOREF	1UL
#define SKB_DST_PTRMASK	~(SKB_DST_NOREF)

/**
 * skb_dst - returns skb dst_entry
 * @skb: buffer
 *
 * Returns skb dst_entry, regardless of reference taken or not.
 */
static inline struct dst_entry *skb_dst(const struct sk_buff *skb)
{
	/* If refdst was not refcounted, check we still are in a
	 * rcu_read_lock section
	 */
	WARN_ON((skb->_skb_refdst & SKB_DST_NOREF) &&
		!rcu_read_lock_held() &&
		!rcu_read_lock_bh_held());
	return (struct dst_entry *)(skb->_skb_refdst & SKB_DST_PTRMASK);
}

/**
 * skb_dst_set - sets skb dst
 * @skb: buffer
 * @dst: dst entry
 *
 * Sets skb dst, assuming a reference was taken on dst and should
 * be released by skb_dst_drop()
 */
static inline void skb_dst_set(struct sk_buff *skb, struct dst_entry *dst)
{
	skb->slow_gro |= !!dst;
	skb->_skb_refdst = (unsigned long)dst;
}

/**
 * skb_dst_set_noref - sets skb dst, hopefully, without taking reference
 * @skb: buffer
 * @dst: dst entry
 *
 * Sets skb dst, assuming a reference was not taken on dst.
 * If dst entry is cached, we do not take reference and dst_release
 * will be avoided by refdst_drop. If dst entry is not cached, we take
 * reference, so that last dst_release can destroy the dst immediately.
 */
static inline void skb_dst_set_noref(struct sk_buff *skb, struct dst_entry *dst)
{
	WARN_ON(!rcu_read_lock_held() && !rcu_read_lock_bh_held());
	skb->slow_gro |= !!dst;
	skb->_skb_refdst = (unsigned long)dst | SKB_DST_NOREF;
}

/**
 * skb_dst_is_noref - Test if skb dst isn't refcounted
 * @skb: buffer
 */
static inline bool skb_dst_is_noref(const struct sk_buff *skb)
{
	return (skb->_skb_refdst & SKB_DST_NOREF) && skb_dst(skb);
}

/**
 * skb_rtable - Returns the skb &rtable
 * @skb: buffer
 */
static inline struct rtable *skb_rtable(const struct sk_buff *skb)
{
	return (struct rtable *)skb_dst(skb);
}

/* For mangling skb->pkt_type from user space side from applications
 * such as nft, tc, etc, we only allow a conservative subset of
 * possible pkt_types to be set.
*/
static inline bool skb_pkt_type_ok(u32 ptype)
{
	return ptype <= PACKET_OTHERHOST;
}

/**
 * skb_napi_id - Returns the skb's NAPI id
 * @skb: buffer
 */
static inline unsigned int skb_napi_id(const struct sk_buff *skb)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	return skb->napi_id;
#else
	return 0;
#endif
}

/**
 * skb_unref - decrement the skb's reference count
 * @skb: buffer
 *
 * Returns true if we can free the skb.
 */
static inline bool skb_unref(struct sk_buff *skb)
{
	if (unlikely(!skb))
		return false;
	if (likely(refcount_read(&skb->users) == 1))
		smp_rmb();
	else if (likely(!refcount_dec_and_test(&skb->users)))
		return false;

	return true;
}

void kfree_skb_reason(struct sk_buff *skb, enum skb_drop_reason reason);

/**
 *	kfree_skb - free an sk_buff with 'NOT_SPECIFIED' reason
 *	@skb: buffer to free
 */
static inline void kfree_skb(struct sk_buff *skb)
{
	kfree_skb_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
}

void skb_release_head_state(struct sk_buff *skb);
void kfree_skb_list(struct sk_buff *segs);
void skb_dump(const char *level, const struct sk_buff *skb, bool full_pkt);
void skb_tx_error(struct sk_buff *skb);

#ifdef CONFIG_TRACEPOINTS
void consume_skb(struct sk_buff *skb);
#else
static inline void consume_skb(struct sk_buff *skb)
{
	return kfree_skb(skb);
}
#endif

void __consume_stateless_skb(struct sk_buff *skb);
void  __kfree_skb(struct sk_buff *skb);
extern struct kmem_cache *skbuff_head_cache;

void kfree_skb_partial(struct sk_buff *skb, bool head_stolen);
bool skb_try_coalesce(struct sk_buff *to, struct sk_buff *from,
		      bool *fragstolen, int *delta_truesize);

struct sk_buff *__alloc_skb(unsigned int size, gfp_t priority, int flags,
			    int node);
struct sk_buff *__build_skb(void *data, unsigned int frag_size);
struct sk_buff *build_skb(void *data, unsigned int frag_size);
struct sk_buff *build_skb_around(struct sk_buff *skb,
				 void *data, unsigned int frag_size);

struct sk_buff *napi_build_skb(void *data, unsigned int frag_size);

/**
 * alloc_skb - allocate a network buffer
 * @size: size to allocate
 * @priority: allocation mask
 *
 * This function is a convenient wrapper around __alloc_skb().
 */
static inline struct sk_buff *alloc_skb(unsigned int size,
					gfp_t priority)
{
	return __alloc_skb(size, priority, 0, NUMA_NO_NODE);
}

struct sk_buff *alloc_skb_with_frags(unsigned long header_len,
				     unsigned long data_len,
				     int max_page_order,
				     int *errcode,
				     gfp_t gfp_mask);
struct sk_buff *alloc_skb_for_msg(struct sk_buff *first);

/**
 *	skb_fclone_busy - check if fclone is busy
 *	@sk: socket
 *	@skb: buffer
 *
 * Returns true if skb is a fast clone, and its clone is not freed.
 * Some drivers call skb_orphan() in their ndo_start_xmit(),
 * so we also check that this didnt happen.
 */
static inline bool skb_fclone_busy(const struct sock *sk,
				   const struct sk_buff *skb)
{
	const struct sk_buff_fclones *fclones;

	fclones = container_of(skb, struct sk_buff_fclones, skb1);

	return skb->fclone == SKB_FCLONE_ORIG &&
	       refcount_read(&fclones->fclone_ref) > 1 &&
	       READ_ONCE(fclones->skb2.sk) == sk;
}

/**
 * alloc_skb_fclone - allocate a network buffer from fclone cache
 * @size: size to allocate
 * @priority: allocation mask
 *
 * This function is a convenient wrapper around __alloc_skb().
 */
static inline struct sk_buff *alloc_skb_fclone(unsigned int size,
					       gfp_t priority)
{
	return __alloc_skb(size, priority, SKB_ALLOC_FCLONE, NUMA_NO_NODE);
}

struct sk_buff *skb_morph(struct sk_buff *dst, struct sk_buff *src);
void skb_headers_offset_update(struct sk_buff *skb, int off);
int skb_copy_ubufs(struct sk_buff *skb, gfp_t gfp_mask);
struct sk_buff *skb_clone(struct sk_buff *skb, gfp_t priority);
void skb_copy_header(struct sk_buff *new, const struct sk_buff *old);
struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t priority);
struct sk_buff *__pskb_copy_fclone(struct sk_buff *skb, int headroom,
				   gfp_t gfp_mask, bool fclone);
static inline struct sk_buff *__pskb_copy(struct sk_buff *skb, int headroom,
					  gfp_t gfp_mask)
{
	return __pskb_copy_fclone(skb, headroom, gfp_mask, false);
}

int pskb_expand_head(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp_mask);
struct sk_buff *skb_realloc_headroom(struct sk_buff *skb,
				     unsigned int headroom);
struct sk_buff *skb_expand_head(struct sk_buff *skb, unsigned int headroom);
struct sk_buff *skb_copy_expand(const struct sk_buff *skb, int newheadroom,
				int newtailroom, gfp_t priority);
int __must_check skb_to_sgvec_nomark(struct sk_buff *skb, struct scatterlist *sg,
				     int offset, int len);
int __must_check skb_to_sgvec(struct sk_buff *skb, struct scatterlist *sg,
			      int offset, int len);
int skb_cow_data(struct sk_buff *skb, int tailbits, struct sk_buff **trailer);
int __skb_pad(struct sk_buff *skb, int pad, bool free_on_error);

/**
 *	skb_pad			-	zero pad the tail of an skb
 *	@skb: buffer to pad
 *	@pad: space to pad
 *
 *	Ensure that a buffer is followed by a padding area that is zero
 *	filled. Used by network drivers which may DMA or transfer data
 *	beyond the buffer end onto the wire.
 *
 *	May return error in out of memory cases. The skb is freed on error.
 */
static inline int skb_pad(struct sk_buff *skb, int pad)
{
	return __skb_pad(skb, pad, true);
}
#define dev_kfree_skb(a)	consume_skb(a)

int skb_append_pagefrags(struct sk_buff *skb, struct page *page,
			 int offset, size_t size);

void skb_prepare_seq_read(struct sk_buff *skb, unsigned int from,
			  unsigned int to, struct skb_seq_state *st);
unsigned int skb_seq_read(unsigned int consumed, const u8 **data,
			  struct skb_seq_state *st);
void skb_abort_seq_read(struct skb_seq_state *st);

struct ts_config;
unsigned int skb_find_text(struct sk_buff *skb, unsigned int from,
			   unsigned int to, struct ts_config *config);

static inline void skb_clear_hash(struct sk_buff *skb)
{
	skb->hash = 0;
	skb->sw_hash = 0;
	skb->l4_hash = 0;
}

static inline void skb_clear_hash_if_not_l4(struct sk_buff *skb)
{
	if (!skb->l4_hash)
		skb_clear_hash(skb);
}

static inline void
__skb_set_hash(struct sk_buff *skb, __u32 hash, bool is_sw, bool is_l4)
{
	skb->l4_hash = is_l4;
	skb->sw_hash = is_sw;
	skb->hash = hash;
}

static inline void
skb_set_hash(struct sk_buff *skb, __u32 hash, enum pkt_hash_types type)
{
	/* Used by drivers to set hash from HW */
	__skb_set_hash(skb, hash, false, type == PKT_HASH_TYPE_L4);
}

static inline void
__skb_set_sw_hash(struct sk_buff *skb, __u32 hash, bool is_l4)
{
	__skb_set_hash(skb, hash, true, is_l4);
}

void __skb_get_hash(struct sk_buff *skb);
u32 __skb_get_hash_symmetric(const struct sk_buff *skb);
u32 skb_get_poff(const struct sk_buff *skb);
u32 __skb_get_poff(const struct sk_buff *skb, const void *data,
		   const struct flow_keys_basic *keys, int hlen);
__be32 __skb_flow_get_ports(const struct sk_buff *skb, int thoff, u8 ip_proto,
			    const void *data, int hlen_proto);

static inline __be32 skb_flow_get_ports(const struct sk_buff *skb,
					int thoff, u8 ip_proto)
{
	return __skb_flow_get_ports(skb, thoff, ip_proto, NULL, 0);
}

void skb_flow_dissector_init(struct flow_dissector *flow_dissector,
			     const struct flow_dissector_key *key,
			     unsigned int key_count);

struct bpf_flow_dissector;
bool bpf_flow_dissect(struct bpf_prog *prog, struct bpf_flow_dissector *ctx,
		      __be16 proto, int nhoff, int hlen, unsigned int flags);

bool __skb_flow_dissect(const struct net *net,
			const struct sk_buff *skb,
			struct flow_dissector *flow_dissector,
			void *target_container, const void *data,
			__be16 proto, int nhoff, int hlen, unsigned int flags);

static inline bool skb_flow_dissect(const struct sk_buff *skb,
				    struct flow_dissector *flow_dissector,
				    void *target_container, unsigned int flags)
{
	return __skb_flow_dissect(NULL, skb, flow_dissector,
				  target_container, NULL, 0, 0, 0, flags);
}

void skb_flow_dissect_meta(const struct sk_buff *skb,
			   struct flow_dissector *flow_dissector,
			   void *target_container);

/* Gets a skb connection tracking info, ctinfo map should be a
 * map of mapsize to translate enum ip_conntrack_info states
 * to user states.
 */
void
skb_flow_dissect_ct(const struct sk_buff *skb,
		    struct flow_dissector *flow_dissector,
		    void *target_container,
		    u16 *ctinfo_map, size_t mapsize,
		    bool post_ct, u16 zone);
void
skb_flow_dissect_tunnel_info(const struct sk_buff *skb,
			     struct flow_dissector *flow_dissector,
			     void *target_container);

void skb_flow_dissect_hash(const struct sk_buff *skb,
			   struct flow_dissector *flow_dissector,
			   void *target_container);

static inline __u32 skb_get_hash(struct sk_buff *skb)
{
	if (!skb->l4_hash && !skb->sw_hash)
		__skb_get_hash(skb);

	return skb->hash;
}

struct flowi6;
__u32 skb_get_hash_flowi6(struct sk_buff *skb, const struct flowi6 *fl6);

__u32 skb_get_hash_perturb(const struct sk_buff *skb,
			   const siphash_key_t *perturb);

static inline __u32 skb_get_hash_raw(const struct sk_buff *skb)
{
	return skb->hash;
}

static inline void skb_copy_hash(struct sk_buff *to, const struct sk_buff *from)
{
	to->hash = from->hash;
	to->sw_hash = from->sw_hash;
	to->l4_hash = from->l4_hash;
};

static inline void skb_copy_decrypted(struct sk_buff *to,
				      const struct sk_buff *from)
{
#ifdef CONFIG_TLS_DEVICE
	to->decrypted = from->decrypted;
#endif
}

#ifdef NET_SKBUFF_DATA_USES_OFFSET
static inline unsigned char *skb_end_pointer(const struct sk_buff *skb)
{
	return skb->head + skb->end;
}

static inline unsigned int skb_end_offset(const struct sk_buff *skb)
{
	return skb->end;
}
#else
static inline unsigned char *skb_end_pointer(const struct sk_buff *skb)
{
	return skb->end;
}

static inline unsigned int skb_end_offset(const struct sk_buff *skb)
{
	return skb->end - skb->head;
}
#endif

/* Internal */
#define skb_shinfo(SKB)	((struct skb_shared_info *)(skb_end_pointer(SKB)))

static inline struct skb_shared_hwtstamps *skb_hwtstamps(struct sk_buff *skb)
{
	return &skb_shinfo(skb)->hwtstamps;
}

static inline struct ubuf_info *skb_zcopy(struct sk_buff *skb)
{
	bool is_zcopy = skb && skb_shinfo(skb)->flags & SKBFL_ZEROCOPY_ENABLE;

	return is_zcopy ? skb_uarg(skb) : NULL;
}

static inline bool skb_zcopy_pure(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->flags & SKBFL_PURE_ZEROCOPY;
}

static inline bool skb_pure_zcopy_same(const struct sk_buff *skb1,
				       const struct sk_buff *skb2)
{
	return skb_zcopy_pure(skb1) == skb_zcopy_pure(skb2);
}

static inline void net_zcopy_get(struct ubuf_info *uarg)
{
	refcount_inc(&uarg->refcnt);
}

static inline void skb_zcopy_init(struct sk_buff *skb, struct ubuf_info *uarg)
{
	skb_shinfo(skb)->destructor_arg = uarg;
	skb_shinfo(skb)->flags |= uarg->flags;
}

static inline void skb_zcopy_set(struct sk_buff *skb, struct ubuf_info *uarg,
				 bool *have_ref)
{
	if (skb && uarg && !skb_zcopy(skb)) {
		if (unlikely(have_ref && *have_ref))
			*have_ref = false;
		else
			net_zcopy_get(uarg);
		skb_zcopy_init(skb, uarg);
	}
}

static inline void skb_zcopy_set_nouarg(struct sk_buff *skb, void *val)
{
	skb_shinfo(skb)->destructor_arg = (void *)((uintptr_t) val | 0x1UL);
	skb_shinfo(skb)->flags |= SKBFL_ZEROCOPY_FRAG;
}

static inline bool skb_zcopy_is_nouarg(struct sk_buff *skb)
{
	return (uintptr_t) skb_shinfo(skb)->destructor_arg & 0x1UL;
}

static inline void *skb_zcopy_get_nouarg(struct sk_buff *skb)
{
	return (void *)((uintptr_t) skb_shinfo(skb)->destructor_arg & ~0x1UL);
}

static inline void net_zcopy_put(struct ubuf_info *uarg)
{
	if (uarg)
		uarg->callback(NULL, uarg, true);
}

static inline void net_zcopy_put_abort(struct ubuf_info *uarg, bool have_uref)
{
	if (uarg) {
		if (uarg->callback == msg_zerocopy_callback)
			msg_zerocopy_put_abort(uarg, have_uref);
		else if (have_uref)
			net_zcopy_put(uarg);
	}
}

/* Release a reference on a zerocopy structure */
static inline void skb_zcopy_clear(struct sk_buff *skb, bool zerocopy_success)
{
	struct ubuf_info *uarg = skb_zcopy(skb);

	if (uarg) {
		if (!skb_zcopy_is_nouarg(skb))
			uarg->callback(skb, uarg, zerocopy_success);

		skb_shinfo(skb)->flags &= ~SKBFL_ALL_ZEROCOPY;
	}
}

/* Iterate through singly-linked GSO fragments of an skb. */
#define skb_list_walk_safe(first, skb, next_skb)                               \
	for ((skb) = (first), (next_skb) = (skb) ? (skb)->next : NULL; (skb);  \
	     (skb) = (next_skb), (next_skb) = (skb) ? (skb)->next : NULL)

static inline void skb_list_del_init(struct sk_buff *skb)
{
	__list_del_entry(&skb->list);
	skb_mark_not_on_list(skb);
}

/**
 *	skb_queue_empty - check if a queue is empty
 *	@list: queue head
 *
 *	Returns true if the queue is empty, false otherwise.
 */
static inline int skb_queue_empty(const struct sk_buff_head *list)
{
	return list->next == (const struct sk_buff *) list;
}

/**
 *	skb_queue_empty_lockless - check if a queue is empty
 *	@list: queue head
 *
 *	Returns true if the queue is empty, false otherwise.
 *	This variant can be used in lockless contexts.
 */
static inline bool skb_queue_empty_lockless(const struct sk_buff_head *list)
{
	return READ_ONCE(list->next) == (const struct sk_buff *) list;
}


/**
 *	skb_queue_is_last - check if skb is the last entry in the queue
 *	@list: queue head
 *	@skb: buffer
 *
 *	Returns true if @skb is the last buffer on the list.
 */
static inline bool skb_queue_is_last(const struct sk_buff_head *list,
				     const struct sk_buff *skb)
{
	return skb->next == (const struct sk_buff *) list;
}

/**
 *	skb_queue_is_first - check if skb is the first entry in the queue
 *	@list: queue head
 *	@skb: buffer
 *
 *	Returns true if @skb is the first buffer on the list.
 */
static inline bool skb_queue_is_first(const struct sk_buff_head *list,
				      const struct sk_buff *skb)
{
	return skb->prev == (const struct sk_buff *) list;
}

/**
 *	skb_queue_next - return the next packet in the queue
 *	@list: queue head
 *	@skb: current buffer
 *
 *	Return the next packet in @list after @skb.  It is only valid to
 *	call this if skb_queue_is_last() evaluates to false.
 */
static inline struct sk_buff *skb_queue_next(const struct sk_buff_head *list,
					     const struct sk_buff *skb)
{
	/* This BUG_ON may seem severe, but if we just return then we
	 * are going to dereference garbage.
	 */
	BUG_ON(skb_queue_is_last(list, skb));
	return skb->next;
}

/**
 *	skb_queue_prev - return the prev packet in the queue
 *	@list: queue head
 *	@skb: current buffer
 *
 *	Return the prev packet in @list before @skb.  It is only valid to
 *	call this if skb_queue_is_first() evaluates to false.
 */
static inline struct sk_buff *skb_queue_prev(const struct sk_buff_head *list,
					     const struct sk_buff *skb)
{
	/* This BUG_ON may seem severe, but if we just return then we
	 * are going to dereference garbage.
	 */
	BUG_ON(skb_queue_is_first(list, skb));
	return skb->prev;
}

/**
 *	skb_get - reference buffer
 *	@skb: buffer to reference
 *
 *	Makes another reference to a socket buffer and returns a pointer
 *	to the buffer.
 */
static inline struct sk_buff *skb_get(struct sk_buff *skb)
{
	refcount_inc(&skb->users);
	return skb;
}

/*
 * If users == 1, we are the only owner and can avoid redundant atomic changes.
 */

/**
 *	skb_cloned - is the buffer a clone
 *	@skb: buffer to check
 *
 *	Returns true if the buffer was generated with skb_clone() and is
 *	one of multiple shared copies of the buffer. Cloned buffers are
 *	shared data so must not be written to under normal circumstances.
 */
static inline int skb_cloned(const struct sk_buff *skb)
{
	return skb->cloned &&
	       (atomic_read(&skb_shinfo(skb)->dataref) & SKB_DATAREF_MASK) != 1;
}

static inline int skb_unclone(struct sk_buff *skb, gfp_t pri)
{
	might_sleep_if(gfpflags_allow_blocking(pri));

	if (skb_cloned(skb))
		return pskb_expand_head(skb, 0, 0, pri);

	return 0;
}

/* This variant of skb_unclone() makes sure skb->truesize is not changed */
static inline int skb_unclone_keeptruesize(struct sk_buff *skb, gfp_t pri)
{
	might_sleep_if(gfpflags_allow_blocking(pri));

	if (skb_cloned(skb)) {
		unsigned int save = skb->truesize;
		int res;

		res = pskb_expand_head(skb, 0, 0, pri);
		skb->truesize = save;
		return res;
	}
	return 0;
}

/**
 *	skb_header_cloned - is the header a clone
 *	@skb: buffer to check
 *
 *	Returns true if modifying the header part of the buffer requires
 *	the data to be copied.
 */
static inline int skb_header_cloned(const struct sk_buff *skb)
{
	int dataref;

	if (!skb->cloned)
		return 0;

	dataref = atomic_read(&skb_shinfo(skb)->dataref);
	dataref = (dataref & SKB_DATAREF_MASK) - (dataref >> SKB_DATAREF_SHIFT);
	return dataref != 1;
}

static inline int skb_header_unclone(struct sk_buff *skb, gfp_t pri)
{
	might_sleep_if(gfpflags_allow_blocking(pri));

	if (skb_header_cloned(skb))
		return pskb_expand_head(skb, 0, 0, pri);

	return 0;
}

/**
 *	__skb_header_release - release reference to header
 *	@skb: buffer to operate on
 */
static inline void __skb_header_release(struct sk_buff *skb)
{
	skb->nohdr = 1;
	atomic_set(&skb_shinfo(skb)->dataref, 1 + (1 << SKB_DATAREF_SHIFT));
}


/**
 *	skb_shared - is the buffer shared
 *	@skb: buffer to check
 *
 *	Returns true if more than one person has a reference to this
 *	buffer.
 */
static inline int skb_shared(const struct sk_buff *skb)
{
	return refcount_read(&skb->users) != 1;
}

/**
 *	skb_share_check - check if buffer is shared and if so clone it
 *	@skb: buffer to check
 *	@pri: priority for memory allocation
 *
 *	If the buffer is shared the buffer is cloned and the old copy
 *	drops a reference. A new clone with a single reference is returned.
 *	If the buffer is not shared the original buffer is returned. When
 *	being called from interrupt status or with spinlocks held pri must
 *	be GFP_ATOMIC.
 *
 *	NULL is returned on a memory allocation failure.
 */
static inline struct sk_buff *skb_share_check(struct sk_buff *skb, gfp_t pri)
{
	might_sleep_if(gfpflags_allow_blocking(pri));
	if (skb_shared(skb)) {
		struct sk_buff *nskb = skb_clone(skb, pri);

		if (likely(nskb))
			consume_skb(skb);
		else
			kfree_skb(skb);
		skb = nskb;
	}
	return skb;
}

/*
 *	Copy shared buffers into a new sk_buff. We effectively do COW on
 *	packets to handle cases where we have a local reader and forward
 *	and a couple of other messy ones. The normal one is tcpdumping
 *	a packet thats being forwarded.
 */

/**
 *	skb_unshare - make a copy of a shared buffer
 *	@skb: buffer to check
 *	@pri: priority for memory allocation
 *
 *	If the socket buffer is a clone then this function creates a new
 *	copy of the data, drops a reference count on the old copy and returns
 *	the new copy with the reference count at 1. If the buffer is not a clone
 *	the original buffer is returned. When called with a spinlock held or
 *	from interrupt state @pri must be %GFP_ATOMIC
 *
 *	%NULL is returned on a memory allocation failure.
 */
static inline struct sk_buff *skb_unshare(struct sk_buff *skb,
					  gfp_t pri)
{
	might_sleep_if(gfpflags_allow_blocking(pri));
	if (skb_cloned(skb)) {
		struct sk_buff *nskb = skb_copy(skb, pri);

		/* Free our shared copy */
		if (likely(nskb))
			consume_skb(skb);
		else
			kfree_skb(skb);
		skb = nskb;
	}
	return skb;
}

/**
 *	skb_peek - peek at the head of an &sk_buff_head
 *	@list_: list to peek at
 *
 *	Peek an &sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. You must hold
 *	the appropriate locks or have a private queue to do this.
 *
 *	Returns %NULL for an empty list or a pointer to the head element.
 *	The reference count is not incremented and the reference is therefore
 *	volatile. Use with caution.
 */
static inline struct sk_buff *skb_peek(const struct sk_buff_head *list_)
{
	struct sk_buff *skb = list_->next;

	if (skb == (struct sk_buff *)list_)
		skb = NULL;
	return skb;
}

/**
 *	__skb_peek - peek at the head of a non-empty &sk_buff_head
 *	@list_: list to peek at
 *
 *	Like skb_peek(), but the caller knows that the list is not empty.
 */
static inline struct sk_buff *__skb_peek(const struct sk_buff_head *list_)
{
	return list_->next;
}

/**
 *	skb_peek_next - peek skb following the given one from a queue
 *	@skb: skb to start from
 *	@list_: list to peek at
 *
 *	Returns %NULL when the end of the list is met or a pointer to the
 *	next element. The reference count is not incremented and the
 *	reference is therefore volatile. Use with caution.
 */
static inline struct sk_buff *skb_peek_next(struct sk_buff *skb,
		const struct sk_buff_head *list_)
{
	struct sk_buff *next = skb->next;

	if (next == (struct sk_buff *)list_)
		next = NULL;
	return next;
}

/**
 *	skb_peek_tail - peek at the tail of an &sk_buff_head
 *	@list_: list to peek at
 *
 *	Peek an &sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. You must hold
 *	the appropriate locks or have a private queue to do this.
 *
 *	Returns %NULL for an empty list or a pointer to the tail element.
 *	The reference count is not incremented and the reference is therefore
 *	volatile. Use with caution.
 */
static inline struct sk_buff *skb_peek_tail(const struct sk_buff_head *list_)
{
	struct sk_buff *skb = READ_ONCE(list_->prev);

	if (skb == (struct sk_buff *)list_)
		skb = NULL;
	return skb;

}

/**
 *	skb_queue_len	- get queue length
 *	@list_: list to measure
 *
 *	Return the length of an &sk_buff queue.
 */
static inline __u32 skb_queue_len(const struct sk_buff_head *list_)
{
	return list_->qlen;
}

/**
 *	skb_queue_len_lockless	- get queue length
 *	@list_: list to measure
 *
 *	Return the length of an &sk_buff queue.
 *	This variant can be used in lockless contexts.
 */
static inline __u32 skb_queue_len_lockless(const struct sk_buff_head *list_)
{
	return READ_ONCE(list_->qlen);
}

/**
 *	__skb_queue_head_init - initialize non-spinlock portions of sk_buff_head
 *	@list: queue to initialize
 *
 *	This initializes only the list and queue length aspects of
 *	an sk_buff_head object.  This allows to initialize the list
 *	aspects of an sk_buff_head without reinitializing things like
 *	the spinlock.  It can also be used for on-stack sk_buff_head
 *	objects where the spinlock is known to not be used.
 */
static inline void __skb_queue_head_init(struct sk_buff_head *list)
{
	list->prev = list->next = (struct sk_buff *)list;
	list->qlen = 0;
}

/*
 * This function creates a split out lock class for each invocation;
 * this is needed for now since a whole lot of users of the skb-queue
 * infrastructure in drivers have different locking usage (in hardirq)
 * than the networking core (in softirq only). In the long run either the
 * network layer or drivers should need annotation to consolidate the
 * main types of usage into 3 classes.
 */
static inline void skb_queue_head_init(struct sk_buff_head *list)
{
	spin_lock_init(&list->lock);
	__skb_queue_head_init(list);
}

static inline void skb_queue_head_init_class(struct sk_buff_head *list,
		struct lock_class_key *class)
{
	skb_queue_head_init(list);
	lockdep_set_class(&list->lock, class);
}

/*
 *	Insert an sk_buff on a list.
 *
 *	The "__skb_xxxx()" functions are the non-atomic ones that
 *	can only be called with interrupts disabled.
 */
static inline void __skb_insert(struct sk_buff *newsk,
				struct sk_buff *prev, struct sk_buff *next,
				struct sk_buff_head *list)
{
	/* See skb_queue_empty_lockless() and skb_peek_tail()
	 * for the opposite READ_ONCE()
	 */
	WRITE_ONCE(newsk->next, next);
	WRITE_ONCE(newsk->prev, prev);
	WRITE_ONCE(((struct sk_buff_list *)next)->prev, newsk);
	WRITE_ONCE(((struct sk_buff_list *)prev)->next, newsk);
	WRITE_ONCE(list->qlen, list->qlen + 1);
}

static inline void __skb_queue_splice(const struct sk_buff_head *list,
				      struct sk_buff *prev,
				      struct sk_buff *next)
{
	struct sk_buff *first = list->next;
	struct sk_buff *last = list->prev;

	WRITE_ONCE(first->prev, prev);
	WRITE_ONCE(prev->next, first);

	WRITE_ONCE(last->next, next);
	WRITE_ONCE(next->prev, last);
}

/**
 *	skb_queue_splice - join two skb lists, this is designed for stacks
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 */
static inline void skb_queue_splice(const struct sk_buff_head *list,
				    struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, (struct sk_buff *) head, head->next);
		head->qlen += list->qlen;
	}
}

/**
 *	skb_queue_splice_init - join two skb lists and reinitialise the emptied list
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 *
 *	The list at @list is reinitialised
 */
static inline void skb_queue_splice_init(struct sk_buff_head *list,
					 struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, (struct sk_buff *) head, head->next);
		head->qlen += list->qlen;
		__skb_queue_head_init(list);
	}
}

/**
 *	skb_queue_splice_tail - join two skb lists, each list being a queue
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 */
static inline void skb_queue_splice_tail(const struct sk_buff_head *list,
					 struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, head->prev, (struct sk_buff *) head);
		head->qlen += list->qlen;
	}
}

/**
 *	skb_queue_splice_tail_init - join two skb lists and reinitialise the emptied list
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 *
 *	Each of the lists is a queue.
 *	The list at @list is reinitialised
 */
static inline void skb_queue_splice_tail_init(struct sk_buff_head *list,
					      struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, head->prev, (struct sk_buff *) head);
		head->qlen += list->qlen;
		__skb_queue_head_init(list);
	}
}

/**
 *	__skb_queue_after - queue a buffer at the list head
 *	@list: list to use
 *	@prev: place after this buffer
 *	@newsk: buffer to queue
 *
 *	Queue a buffer int the middle of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
static inline void __skb_queue_after(struct sk_buff_head *list,
				     struct sk_buff *prev,
				     struct sk_buff *newsk)
{
	__skb_insert(newsk, prev, ((struct sk_buff_list *)prev)->next, list);
}

void skb_append(struct sk_buff *old, struct sk_buff *newsk,
		struct sk_buff_head *list);

static inline void __skb_queue_before(struct sk_buff_head *list,
				      struct sk_buff *next,
				      struct sk_buff *newsk)
{
	__skb_insert(newsk, ((struct sk_buff_list *)next)->prev, next, list);
}

/**
 *	__skb_queue_head - queue a buffer at the list head
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the start of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
static inline void __skb_queue_head(struct sk_buff_head *list,
				    struct sk_buff *newsk)
{
	__skb_queue_after(list, (struct sk_buff *)list, newsk);
}
void skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk);

/**
 *	__skb_queue_tail - queue a buffer at the list tail
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the end of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
static inline void __skb_queue_tail(struct sk_buff_head *list,
				   struct sk_buff *newsk)
{
	__skb_queue_before(list, (struct sk_buff *)list, newsk);
}
void skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk);

/*
 * remove sk_buff from list. _Must_ be called atomically, and with
 * the list known..
 */
void skb_unlink(struct sk_buff *skb, struct sk_buff_head *list);
static inline void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
	struct sk_buff *next, *prev;

	WRITE_ONCE(list->qlen, list->qlen - 1);
	next	   = skb->next;
	prev	   = skb->prev;
	skb->next  = skb->prev = NULL;
	WRITE_ONCE(next->prev, prev);
	WRITE_ONCE(prev->next, next);
}

/**
 *	__skb_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. This function does not take any locks
 *	so must be used with appropriate locks held only. The head item is
 *	returned or %NULL if the list is empty.
 */
static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek(list);
	if (skb)
		__skb_unlink(skb, list);
	return skb;
}
struct sk_buff *skb_dequeue(struct sk_buff_head *list);

/**
 *	__skb_dequeue_tail - remove from the tail of the queue
 *	@list: list to dequeue from
 *
 *	Remove the tail of the list. This function does not take any locks
 *	so must be used with appropriate locks held only. The tail item is
 *	returned or %NULL if the list is empty.
 */
static inline struct sk_buff *__skb_dequeue_tail(struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek_tail(list);
	if (skb)
		__skb_unlink(skb, list);
	return skb;
}
struct sk_buff *skb_dequeue_tail(struct sk_buff_head *list);


static inline bool skb_is_nonlinear(const struct sk_buff *skb)
{
	return skb->data_len;
}

static inline unsigned int skb_headlen(const struct sk_buff *skb)
{
	return skb->len - skb->data_len;
}

void skb_add_rx_frag(struct sk_buff *skb, int i, struct page *page, int off,
		     int size, unsigned int truesize);

void skb_coalesce_rx_frag(struct sk_buff *skb, int i, int size,
			  unsigned int truesize);

#define SKB_LINEAR_ASSERT(skb)  BUG_ON(skb_is_nonlinear(skb))

#ifdef NET_SKBUFF_DATA_USES_OFFSET
static inline unsigned char *skb_tail_pointer(const struct sk_buff *skb)
{
	return skb->head + skb->tail;
}

static inline void skb_reset_tail_pointer(struct sk_buff *skb)
{
	skb->tail = skb->data - skb->head;
}

static inline void skb_set_tail_pointer(struct sk_buff *skb, const int offset)
{
	skb_reset_tail_pointer(skb);
	skb->tail += offset;
}

#else /* NET_SKBUFF_DATA_USES_OFFSET */
static inline unsigned char *skb_tail_pointer(const struct sk_buff *skb)
{
	return skb->tail;
}

static inline void skb_reset_tail_pointer(struct sk_buff *skb)
{
	skb->tail = skb->data;
}

static inline void skb_set_tail_pointer(struct sk_buff *skb, const int offset)
{
	skb->tail = skb->data + offset;
}

#endif /* NET_SKBUFF_DATA_USES_OFFSET */

/*
 *	Add data to an sk_buff
 */
void *pskb_put(struct sk_buff *skb, struct sk_buff *tail, int len);
void *skb_put(struct sk_buff *skb, unsigned int len);
static inline void *__skb_put(struct sk_buff *skb, unsigned int len)
{
	void *tmp = skb_tail_pointer(skb);
	SKB_LINEAR_ASSERT(skb);
	skb->tail += len;
	skb->len  += len;
	return tmp;
}

static inline void __skb_put_u8(struct sk_buff *skb, u8 val)
{
	*(u8 *)__skb_put(skb, 1) = val;
}

static inline void skb_put_u8(struct sk_buff *skb, u8 val)
{
	*(u8 *)skb_put(skb, 1) = val;
}

void *skb_push(struct sk_buff *skb, unsigned int len);
static inline void *__skb_push(struct sk_buff *skb, unsigned int len)
{
	skb->data -= len;
	skb->len  += len;
	return skb->data;
}

void *skb_pull(struct sk_buff *skb, unsigned int len);
static inline void *__skb_pull(struct sk_buff *skb, unsigned int len)
{
	skb->len -= len;
	BUG_ON(skb->len < skb->data_len);
	return skb->data += len;
}

static inline void *skb_pull_inline(struct sk_buff *skb, unsigned int len)
{
	return unlikely(len > skb->len) ? NULL : __skb_pull(skb, len);
}

void *skb_pull_data(struct sk_buff *skb, size_t len);

void *__pskb_pull_tail(struct sk_buff *skb, int delta);

static inline void *__pskb_pull(struct sk_buff *skb, unsigned int len)
{
	if (len > skb_headlen(skb) &&
	    !__pskb_pull_tail(skb, len - skb_headlen(skb)))
		return NULL;
	skb->len -= len;
	return skb->data += len;
}

static inline void *pskb_pull(struct sk_buff *skb, unsigned int len)
{
	return unlikely(len > skb->len) ? NULL : __pskb_pull(skb, len);
}

static inline bool pskb_may_pull(struct sk_buff *skb, unsigned int len)
{
	if (likely(len <= skb_headlen(skb)))
		return true;
	if (unlikely(len > skb->len))
		return false;
	return __pskb_pull_tail(skb, len - skb_headlen(skb)) != NULL;
}

void skb_condense(struct sk_buff *skb);

/**
 *	skb_headroom - bytes at buffer head
 *	@skb: buffer to check
 *
 *	Return the number of bytes of free space at the head of an &sk_buff.
 */
static inline unsigned int skb_headroom(const struct sk_buff *skb)
{
	return skb->data - skb->head;
}

/**
 *	skb_tailroom - bytes at buffer end
 *	@skb: buffer to check
 *
 *	Return the number of bytes of free space at the tail of an sk_buff
 */
static inline int skb_tailroom(const struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) ? 0 : skb->end - skb->tail;
}

/**
 *	skb_availroom - bytes at buffer end
 *	@skb: buffer to check
 *
 *	Return the number of bytes of free space at the tail of an sk_buff
 *	allocated by sk_stream_alloc()
 */
static inline int skb_availroom(const struct sk_buff *skb)
{
	if (skb_is_nonlinear(skb))
		return 0;

	return skb->end - skb->tail - skb->reserved_tailroom;
}

/**
 *	skb_reserve - adjust headroom
 *	@skb: buffer to alter
 *	@len: bytes to move
 *
 *	Increase the headroom of an empty &sk_buff by reducing the tail
 *	room. This is only allowed for an empty buffer.
 */
static inline void skb_reserve(struct sk_buff *skb, int len)
{
	skb->data += len;
	skb->tail += len;
}

/**
 *	skb_tailroom_reserve - adjust reserved_tailroom
 *	@skb: buffer to alter
 *	@mtu: maximum amount of headlen permitted
 *	@needed_tailroom: minimum amount of reserved_tailroom
 *
 *	Set reserved_tailroom so that headlen can be as large as possible but
 *	not larger than mtu and tailroom cannot be smaller than
 *	needed_tailroom.
 *	The required headroom should already have been reserved before using
 *	this function.
 */
static inline void skb_tailroom_reserve(struct sk_buff *skb, unsigned int mtu,
					unsigned int needed_tailroom)
{
	SKB_LINEAR_ASSERT(skb);
	if (mtu < skb_tailroom(skb) - needed_tailroom)
		/* use at most mtu */
		skb->reserved_tailroom = skb_tailroom(skb) - mtu;
	else
		/* use up to all available space */
		skb->reserved_tailroom = needed_tailroom;
}

#define ENCAP_TYPE_ETHER	0
#define ENCAP_TYPE_IPPROTO	1

static inline void skb_set_inner_protocol(struct sk_buff *skb,
					  __be16 protocol)
{
	skb->inner_protocol = protocol;
	skb->inner_protocol_type = ENCAP_TYPE_ETHER;
}

static inline void skb_set_inner_ipproto(struct sk_buff *skb,
					 __u8 ipproto)
{
	skb->inner_ipproto = ipproto;
	skb->inner_protocol_type = ENCAP_TYPE_IPPROTO;
}

static inline void skb_reset_inner_headers(struct sk_buff *skb)
{
	skb->inner_mac_header = skb->mac_header;
	skb->inner_network_header = skb->network_header;
	skb->inner_transport_header = skb->transport_header;
}

static inline void skb_reset_mac_len(struct sk_buff *skb)
{
	skb->mac_len = skb->network_header - skb->mac_header;
}

static inline int skb_inner_transport_offset(const struct sk_buff *skb)
{
	return skb_inner_transport_header(skb) - skb->data;
}

static inline void skb_reset_inner_transport_header(struct sk_buff *skb)
{
	skb->inner_transport_header = skb->data - skb->head;
}

static inline void skb_set_inner_transport_header(struct sk_buff *skb,
						   const int offset)
{
	skb_reset_inner_transport_header(skb);
	skb->inner_transport_header += offset;
}

static inline void skb_reset_inner_network_header(struct sk_buff *skb)
{
	skb->inner_network_header = skb->data - skb->head;
}

static inline void skb_set_inner_network_header(struct sk_buff *skb,
						const int offset)
{
	skb_reset_inner_network_header(skb);
	skb->inner_network_header += offset;
}

static inline unsigned char *skb_inner_mac_header(const struct sk_buff *skb)
{
	return skb->head + skb->inner_mac_header;
}

static inline void skb_reset_inner_mac_header(struct sk_buff *skb)
{
	skb->inner_mac_header = skb->data - skb->head;
}

static inline void skb_set_inner_mac_header(struct sk_buff *skb,
					    const int offset)
{
	skb_reset_inner_mac_header(skb);
	skb->inner_mac_header += offset;
}

static inline bool skb_transport_header_was_set(const struct sk_buff *skb)
{
	return skb->transport_header != (typeof(skb->transport_header))~0U;
}

static inline void skb_reset_transport_header(struct sk_buff *skb)
{
	skb->transport_header = skb->data - skb->head;
}

static inline void skb_set_transport_header(struct sk_buff *skb,
					    const int offset)
{
	skb_reset_transport_header(skb);
	skb->transport_header += offset;
}

static inline void skb_reset_network_header(struct sk_buff *skb)
{
	skb->network_header = skb->data - skb->head;
}

static inline void skb_set_network_header(struct sk_buff *skb, const int offset)
{
	skb_reset_network_header(skb);
	skb->network_header += offset;
}

static inline unsigned char *skb_mac_header(const struct sk_buff *skb)
{
	return skb->head + skb->mac_header;
}

static inline int skb_mac_offset(const struct sk_buff *skb)
{
	return skb_mac_header(skb) - skb->data;
}

static inline u32 skb_mac_header_len(const struct sk_buff *skb)
{
	return skb->network_header - skb->mac_header;
}

static inline int skb_mac_header_was_set(const struct sk_buff *skb)
{
	return skb->mac_header != (typeof(skb->mac_header))~0U;
}

static inline void skb_unset_mac_header(struct sk_buff *skb)
{
	skb->mac_header = (typeof(skb->mac_header))~0U;
}

static inline void skb_reset_mac_header(struct sk_buff *skb)
{
	skb->mac_header = skb->data - skb->head;
}

static inline void skb_set_mac_header(struct sk_buff *skb, const int offset)
{
	skb_reset_mac_header(skb);
	skb->mac_header += offset;
}

static inline void skb_pop_mac_header(struct sk_buff *skb)
{
	skb->mac_header = skb->network_header;
}

static inline int skb_checksum_start_offset(const struct sk_buff *skb)
{
	return skb->csum_start - skb_headroom(skb);
}

static inline unsigned char *skb_checksum_start(const struct sk_buff *skb)
{
	return skb->head + skb->csum_start;
}

static inline int skb_transport_offset(const struct sk_buff *skb)
{
	return skb_transport_header(skb) - skb->data;
}

static inline u32 skb_inner_network_header_len(const struct sk_buff *skb)
{
	return skb->inner_transport_header - skb->inner_network_header;
}

static inline int skb_network_offset(const struct sk_buff *skb)
{
	return skb_network_header(skb) - skb->data;
}

static inline int skb_inner_network_offset(const struct sk_buff *skb)
{
	return skb_inner_network_header(skb) - skb->data;
}

static inline int pskb_network_may_pull(struct sk_buff *skb, unsigned int len)
{
	return pskb_may_pull(skb, skb_network_offset(skb) + len);
}

/*
 * CPUs often take a performance hit when accessing unaligned memory
 * locations. The actual performance hit varies, it can be small if the
 * hardware handles it or large if we have to take an exception and fix it
 * in software.
 *
 * Since an ethernet header is 14 bytes network drivers often end up with
 * the IP header at an unaligned offset. The IP header can be aligned by
 * shifting the start of the packet by 2 bytes. Drivers should do this
 * with:
 *
 * skb_reserve(skb, NET_IP_ALIGN);
 *
 * The downside to this alignment of the IP header is that the DMA is now
 * unaligned. On some architectures the cost of an unaligned DMA is high
 * and this cost outweighs the gains made by aligning the IP header.
 *
 * Since this trade off varies between architectures, we allow NET_IP_ALIGN
 * to be overridden.
 */
#ifndef NET_IP_ALIGN
#define NET_IP_ALIGN	2
#endif

/*
 * The networking layer reserves some headroom in skb data (via
 * dev_alloc_skb). This is used to avoid having to reallocate skb data when
 * the header has to grow. In the default case, if the header has to grow
 * 32 bytes or less we avoid the reallocation.
 *
 * Unfortunately this headroom changes the DMA alignment of the resulting
 * network packet. As for NET_IP_ALIGN, this unaligned DMA is expensive
 * on some architectures. An architecture can override this value,
 * perhaps setting it to a cacheline in size (since that will maintain
 * cacheline alignment of the DMA). It must be a power of 2.
 *
 * Various parts of the networking layer expect at least 32 bytes of
 * headroom, you should not reduce this.
 *
 * Using max(32, L1_CACHE_BYTES) makes sense (especially with RPS)
 * to reduce average number of cache lines per packet.
 * get_rps_cpu() for example only access one 64 bytes aligned block :
 * NET_IP_ALIGN(2) + ethernet_header(14) + IP_header(20/40) + ports(8)
 */
#ifndef NET_SKB_PAD
#define NET_SKB_PAD	max(32, L1_CACHE_BYTES)
#endif

int ___pskb_trim(struct sk_buff *skb, unsigned int len);

static inline void __skb_set_length(struct sk_buff *skb, unsigned int len)
{
	if (WARN_ON(skb_is_nonlinear(skb)))
		return;
	skb->len = len;
	skb_set_tail_pointer(skb, len);
}

static inline void __skb_trim(struct sk_buff *skb, unsigned int len)
{
	__skb_set_length(skb, len);
}

void skb_trim(struct sk_buff *skb, unsigned int len);

static inline int __pskb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->data_len)
		return ___pskb_trim(skb, len);
	__skb_trim(skb, len);
	return 0;
}

static inline int pskb_trim(struct sk_buff *skb, unsigned int len)
{
	return (len < skb->len) ? __pskb_trim(skb, len) : 0;
}

/**
 *	pskb_trim_unique - remove end from a paged unique (not cloned) buffer
 *	@skb: buffer to alter
 *	@len: new length
 *
 *	This is identical to pskb_trim except that the caller knows that
 *	the skb is not cloned so we should never get an error due to out-
 *	of-memory.
 */
static inline void pskb_trim_unique(struct sk_buff *skb, unsigned int len)
{
	int err = pskb_trim(skb, len);
	BUG_ON(err);
}

static inline int __skb_grow(struct sk_buff *skb, unsigned int len)
{
	unsigned int diff = len - skb->len;

	if (skb_tailroom(skb) < diff) {
		int ret = pskb_expand_head(skb, 0, diff - skb_tailroom(skb),
					   GFP_ATOMIC);
		if (ret)
			return ret;
	}
	__skb_set_length(skb, len);
	return 0;
}

/**
 *	skb_orphan - orphan a buffer
 *	@skb: buffer to orphan
 *
 *	If a buffer currently has an owner then we call the owner's
 *	destructor function and make the @skb unowned. The buffer continues
 *	to exist but is no longer charged to its former owner.
 */
static inline void skb_orphan(struct sk_buff *skb)
{
	if (skb->destructor) {
		skb->destructor(skb);
		skb->destructor = NULL;
		skb->sk		= NULL;
	} else {
		BUG_ON(skb->sk);
	}
}

/**
 *	skb_orphan_frags - orphan the frags contained in a buffer
 *	@skb: buffer to orphan frags from
 *	@gfp_mask: allocation mask for replacement pages
 *
 *	For each frag in the SKB which needs a destructor (i.e. has an
 *	owner) create a copy of that frag and release the original
 *	page by calling the destructor.
 */
static inline int skb_orphan_frags(struct sk_buff *skb, gfp_t gfp_mask)
{
	if (likely(!skb_zcopy(skb)))
		return 0;
	if (!skb_zcopy_is_nouarg(skb) &&
	    skb_uarg(skb)->callback == msg_zerocopy_callback)
		return 0;
	return skb_copy_ubufs(skb, gfp_mask);
}

/* Frags must be orphaned, even if refcounted, if skb might loop to rx path */
static inline int skb_orphan_frags_rx(struct sk_buff *skb, gfp_t gfp_mask)
{
	if (likely(!skb_zcopy(skb)))
		return 0;
	return skb_copy_ubufs(skb, gfp_mask);
}

/**
 *	__skb_queue_purge - empty a list
 *	@list: list to empty
 *
 *	Delete all buffers on an &sk_buff list. Each buffer is removed from
 *	the list and one reference dropped. This function does not take the
 *	list lock and the caller must hold the relevant locks to use it.
 */
static inline void __skb_queue_purge(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	while ((skb = __skb_dequeue(list)) != NULL)
		kfree_skb(skb);
}
void skb_queue_purge(struct sk_buff_head *list);

unsigned int skb_rbtree_purge(struct rb_root *root);

void *__netdev_alloc_frag_align(unsigned int fragsz, unsigned int align_mask);

/**
 * netdev_alloc_frag - allocate a page fragment
 * @fragsz: fragment size
 *
 * Allocates a frag from a page for receive buffer.
 * Uses GFP_ATOMIC allocations.
 */
static inline void *netdev_alloc_frag(unsigned int fragsz)
{
	return __netdev_alloc_frag_align(fragsz, ~0u);
}

static inline void *netdev_alloc_frag_align(unsigned int fragsz,
					    unsigned int align)
{
	WARN_ON_ONCE(!is_power_of_2(align));
	return __netdev_alloc_frag_align(fragsz, -align);
}

struct sk_buff *__netdev_alloc_skb(struct net_device *dev, unsigned int length,
				   gfp_t gfp_mask);

/**
 *	netdev_alloc_skb - allocate an skbuff for rx on a specific device
 *	@dev: network device to receive on
 *	@length: length to allocate
 *
 *	Allocate a new &sk_buff and assign it a usage count of one. The
 *	buffer has unspecified headroom built in. Users should allocate
 *	the headroom they think they need without accounting for the
 *	built in space. The built in space is used for optimisations.
 *
 *	%NULL is returned if there is no free memory. Although this function
 *	allocates memory it can be called from an interrupt.
 */
static inline struct sk_buff *netdev_alloc_skb(struct net_device *dev,
					       unsigned int length)
{
	return __netdev_alloc_skb(dev, length, GFP_ATOMIC);
}

/* legacy helper around __netdev_alloc_skb() */
static inline struct sk_buff *__dev_alloc_skb(unsigned int length,
					      gfp_t gfp_mask)
{
	return __netdev_alloc_skb(NULL, length, gfp_mask);
}

/* legacy helper around netdev_alloc_skb() */
static inline struct sk_buff *dev_alloc_skb(unsigned int length)
{
	return netdev_alloc_skb(NULL, length);
}


static inline struct sk_buff *__netdev_alloc_skb_ip_align(struct net_device *dev,
		unsigned int length, gfp_t gfp)
{
	struct sk_buff *skb = __netdev_alloc_skb(dev, length + NET_IP_ALIGN, gfp);

	if (NET_IP_ALIGN && skb)
		skb_reserve(skb, NET_IP_ALIGN);
	return skb;
}

static inline struct sk_buff *netdev_alloc_skb_ip_align(struct net_device *dev,
		unsigned int length)
{
	return __netdev_alloc_skb_ip_align(dev, length, GFP_ATOMIC);
}

struct sk_buff *__napi_alloc_skb(struct napi_struct *napi,
				 unsigned int length, gfp_t gfp_mask);
static inline struct sk_buff *napi_alloc_skb(struct napi_struct *napi,
					     unsigned int length)
{
	return __napi_alloc_skb(napi, length, GFP_ATOMIC);
}
void napi_consume_skb(struct sk_buff *skb, int budget);

void napi_skb_free_stolen_head(struct sk_buff *skb);
void __kfree_skb_defer(struct sk_buff *skb);

bool dev_page_is_reusable(const struct page *page);

static inline struct sk_buff *pskb_copy(struct sk_buff *skb,
					gfp_t gfp_mask)
{
	return __pskb_copy(skb, skb_headroom(skb), gfp_mask);
}


static inline struct sk_buff *pskb_copy_for_clone(struct sk_buff *skb,
						  gfp_t gfp_mask)
{
	return __pskb_copy_fclone(skb, skb_headroom(skb), gfp_mask, true);
}


/**
 *	skb_clone_writable - is the header of a clone writable
 *	@skb: buffer to check
 *	@len: length up to which to write
 *
 *	Returns true if modifying the header part of the cloned buffer
 *	does not requires the data to be copied.
 */
static inline int skb_clone_writable(const struct sk_buff *skb, unsigned int len)
{
	return !skb_header_cloned(skb) &&
	       skb_headroom(skb) + len <= skb->hdr_len;
}

static inline int skb_try_make_writable(struct sk_buff *skb,
					unsigned int write_len)
{
	return skb_cloned(skb) && !skb_clone_writable(skb, write_len) &&
	       pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
}

static inline int __skb_cow(struct sk_buff *skb, unsigned int headroom,
			    int cloned)
{
	int delta = 0;

	if (headroom > skb_headroom(skb))
		delta = headroom - skb_headroom(skb);

	if (delta || cloned)
		return pskb_expand_head(skb, ALIGN(delta, NET_SKB_PAD), 0,
					GFP_ATOMIC);
	return 0;
}

/**
 *	skb_cow - copy header of skb when it is required
 *	@skb: buffer to cow
 *	@headroom: needed headroom
 *
 *	If the skb passed lacks sufficient headroom or its data part
 *	is shared, data is reallocated. If reallocation fails, an error
 *	is returned and original skb is not changed.
 *
 *	The result is skb with writable area skb->head...skb->tail
 *	and at least @headroom of space at head.
 */
static inline int skb_cow(struct sk_buff *skb, unsigned int headroom)
{
	return __skb_cow(skb, headroom, skb_cloned(skb));
}

/**
 *	skb_cow_head - skb_cow but only making the head writable
 *	@skb: buffer to cow
 *	@headroom: needed headroom
 *
 *	This function is identical to skb_cow except that we replace the
 *	skb_cloned check by skb_header_cloned.  It should be used when
 *	you only need to push on some header and do not need to modify
 *	the data.
 */
static inline int skb_cow_head(struct sk_buff *skb, unsigned int headroom)
{
	return __skb_cow(skb, headroom, skb_header_cloned(skb));
}

/**
 *	skb_padto	- pad an skbuff up to a minimal size
 *	@skb: buffer to pad
 *	@len: minimal length
 *
 *	Pads up a buffer to ensure the trailing bytes exist and are
 *	blanked. If the buffer already contains sufficient data it
 *	is untouched. Otherwise it is extended. Returns zero on
 *	success. The skb is freed on error.
 */
static inline int skb_padto(struct sk_buff *skb, unsigned int len)
{
	unsigned int size = skb->len;
	if (likely(size >= len))
		return 0;
	return skb_pad(skb, len - size);
}

/**
 *	__skb_put_padto - increase size and pad an skbuff up to a minimal size
 *	@skb: buffer to pad
 *	@len: minimal length
 *	@free_on_error: free buffer on error
 *
 *	Pads up a buffer to ensure the trailing bytes exist and are
 *	blanked. If the buffer already contains sufficient data it
 *	is untouched. Otherwise it is extended. Returns zero on
 *	success. The skb is freed on error if @free_on_error is true.
 */
static inline int __must_check __skb_put_padto(struct sk_buff *skb,
					       unsigned int len,
					       bool free_on_error)
{
	unsigned int size = skb->len;

	if (unlikely(size < len)) {
		len -= size;
		if (__skb_pad(skb, len, free_on_error))
			return -ENOMEM;
		__skb_put(skb, len);
	}
	return 0;
}

/**
 *	skb_put_padto - increase size and pad an skbuff up to a minimal size
 *	@skb: buffer to pad
 *	@len: minimal length
 *
 *	Pads up a buffer to ensure the trailing bytes exist and are
 *	blanked. If the buffer already contains sufficient data it
 *	is untouched. Otherwise it is extended. Returns zero on
 *	success. The skb is freed on error.
 */
static inline int __must_check skb_put_padto(struct sk_buff *skb, unsigned int len)
{
	return __skb_put_padto(skb, len, true);
}

static inline int __skb_linearize(struct sk_buff *skb)
{
	return __pskb_pull_tail(skb, skb->data_len) ? 0 : -ENOMEM;
}

/**
 *	skb_linearize - convert paged skb to linear one
 *	@skb: buffer to linarize
 *
 *	If there is no free memory -ENOMEM is returned, otherwise zero
 *	is returned and the old skb data released.
 */
static inline int skb_linearize(struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) ? __skb_linearize(skb) : 0;
}

/**
 * skb_has_shared_frag - can any frag be overwritten
 * @skb: buffer to test
 *
 * Return true if the skb has at least one frag that might be modified
 * by an external entity (as in vmsplice()/sendfile())
 */
static inline bool skb_has_shared_frag(const struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) &&
	       skb_shinfo(skb)->flags & SKBFL_SHARED_FRAG;
}

/**
 *	skb_linearize_cow - make sure skb is linear and writable
 *	@skb: buffer to process
 *
 *	If there is no free memory -ENOMEM is returned, otherwise zero
 *	is returned and the old skb data released.
 */
static inline int skb_linearize_cow(struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) || skb_cloned(skb) ?
	       __skb_linearize(skb) : 0;
}

void *skb_pull_rcsum(struct sk_buff *skb, unsigned int len);

int pskb_trim_rcsum_slow(struct sk_buff *skb, unsigned int len);
/**
 *	pskb_trim_rcsum - trim received skb and update checksum
 *	@skb: buffer to trim
 *	@len: new length
 *
 *	This is exactly the same as pskb_trim except that it ensures the
 *	checksum of received packets are still valid after the operation.
 *	It can change skb pointers.
 */

static inline int pskb_trim_rcsum(struct sk_buff *skb, unsigned int len)
{
	if (likely(len >= skb->len))
		return 0;
	return pskb_trim_rcsum_slow(skb, len);
}

static inline int __skb_trim_rcsum(struct sk_buff *skb, unsigned int len)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;
	__skb_trim(skb, len);
	return 0;
}

static inline int __skb_grow_rcsum(struct sk_buff *skb, unsigned int len)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;
	return __skb_grow(skb, len);
}

#define rb_to_skb(rb) rb_entry_safe(rb, struct sk_buff, rbnode)
#define skb_rb_first(root) rb_to_skb(rb_first(root))
#define skb_rb_last(root)  rb_to_skb(rb_last(root))
#define skb_rb_next(skb)   rb_to_skb(rb_next(&(skb)->rbnode))
#define skb_rb_prev(skb)   rb_to_skb(rb_prev(&(skb)->rbnode))

#define skb_queue_walk(queue, skb) \
		for (skb = (queue)->next;					\
		     skb != (struct sk_buff *)(queue);				\
		     skb = skb->next)

#define skb_queue_walk_safe(queue, skb, tmp)					\
		for (skb = (queue)->next, tmp = skb->next;			\
		     skb != (struct sk_buff *)(queue);				\
		     skb = tmp, tmp = skb->next)

#define skb_queue_walk_from(queue, skb)						\
		for (; skb != (struct sk_buff *)(queue);			\
		     skb = skb->next)

#define skb_rbtree_walk(skb, root)						\
		for (skb = skb_rb_first(root); skb != NULL;			\
		     skb = skb_rb_next(skb))

#define skb_rbtree_walk_from(skb)						\
		for (; skb != NULL;						\
		     skb = skb_rb_next(skb))

#define skb_rbtree_walk_from_safe(skb, tmp)					\
		for (; tmp = skb ? skb_rb_next(skb) : NULL, (skb != NULL);	\
		     skb = tmp)

#define skb_queue_walk_from_safe(queue, skb, tmp)				\
		for (tmp = skb->next;						\
		     skb != (struct sk_buff *)(queue);				\
		     skb = tmp, tmp = skb->next)

#define skb_queue_reverse_walk(queue, skb) \
		for (skb = (queue)->prev;					\
		     skb != (struct sk_buff *)(queue);				\
		     skb = skb->prev)

#define skb_queue_reverse_walk_safe(queue, skb, tmp)				\
		for (skb = (queue)->prev, tmp = skb->prev;			\
		     skb != (struct sk_buff *)(queue);				\
		     skb = tmp, tmp = skb->prev)

#define skb_queue_reverse_walk_from_safe(queue, skb, tmp)			\
		for (tmp = skb->prev;						\
		     skb != (struct sk_buff *)(queue);				\
		     skb = tmp, tmp = skb->prev)

static inline bool skb_has_frag_list(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->frag_list != NULL;
}

static inline void skb_frag_list_init(struct sk_buff *skb)
{
	skb_shinfo(skb)->frag_list = NULL;
}

#define skb_walk_frags(skb, iter)	\
	for (iter = skb_shinfo(skb)->frag_list; iter; iter = iter->next)

struct poll_table_struct;

int __skb_wait_for_more_packets(struct sock *sk, struct sk_buff_head *queue,
				int *err, long *timeo_p,
				const struct sk_buff *skb);
struct sk_buff *__skb_try_recv_from_queue(struct sock *sk,
					  struct sk_buff_head *queue,
					  unsigned int flags,
					  int *off, int *err,
					  struct sk_buff **last);
struct sk_buff *__skb_try_recv_datagram(struct sock *sk,
					struct sk_buff_head *queue,
					unsigned int flags, int *off, int *err,
					struct sk_buff **last);
struct sk_buff *__skb_recv_datagram(struct sock *sk,
				    struct sk_buff_head *sk_queue,
				    unsigned int flags, int *off, int *err);
struct sk_buff *skb_recv_datagram(struct sock *sk, unsigned flags, int noblock,
				  int *err);
__poll_t datagram_poll(struct file *file, struct socket *sock,
			   struct poll_table_struct *wait);
int skb_copy_datagram_iter(const struct sk_buff *from, int offset,
			   struct iov_iter *to, int size);
static inline int skb_copy_datagram_msg(const struct sk_buff *from, int offset,
					struct msghdr *msg, int size)
{
	return skb_copy_datagram_iter(from, offset, &msg->msg_iter, size);
}
int skb_copy_and_csum_datagram_msg(struct sk_buff *skb, int hlen,
				   struct msghdr *msg);
int skb_copy_and_hash_datagram_iter(const struct sk_buff *skb, int offset,
			   struct iov_iter *to, int len,
			   struct ahash_request *hash);
int skb_copy_datagram_from_iter(struct sk_buff *skb, int offset,
				 struct iov_iter *from, int len);
int zerocopy_sg_from_iter(struct sk_buff *skb, struct iov_iter *frm);
void skb_free_datagram(struct sock *sk, struct sk_buff *skb);
void __skb_free_datagram_locked(struct sock *sk, struct sk_buff *skb, int len);
static inline void skb_free_datagram_locked(struct sock *sk,
					    struct sk_buff *skb)
{
	__skb_free_datagram_locked(sk, skb, 0);
}
int skb_kill_datagram(struct sock *sk, struct sk_buff *skb, unsigned int flags);
int skb_copy_bits(const struct sk_buff *skb, int offset, void *to, int len);
int skb_store_bits(struct sk_buff *skb, int offset, const void *from, int len);
__wsum skb_copy_and_csum_bits(const struct sk_buff *skb, int offset, u8 *to,
			      int len);
int skb_splice_bits(struct sk_buff *skb, struct sock *sk, unsigned int offset,
		    struct pipe_inode_info *pipe, unsigned int len,
		    unsigned int flags);
int skb_send_sock_locked(struct sock *sk, struct sk_buff *skb, int offset,
			 int len);
int skb_send_sock(struct sock *sk, struct sk_buff *skb, int offset, int len);
void skb_copy_and_csum_dev(const struct sk_buff *skb, u8 *to);
unsigned int skb_zerocopy_headlen(const struct sk_buff *from);
int skb_zerocopy(struct sk_buff *to, struct sk_buff *from,
		 int len, int hlen);
void skb_split(struct sk_buff *skb, struct sk_buff *skb1, const u32 len);
int skb_shift(struct sk_buff *tgt, struct sk_buff *skb, int shiftlen);
void skb_scrub_packet(struct sk_buff *skb, bool xnet);
bool skb_gso_validate_network_len(const struct sk_buff *skb, unsigned int mtu);
bool skb_gso_validate_mac_len(const struct sk_buff *skb, unsigned int len);
struct sk_buff *skb_segment(struct sk_buff *skb, netdev_features_t features);
struct sk_buff *skb_segment_list(struct sk_buff *skb, netdev_features_t features,
				 unsigned int offset);
struct sk_buff *skb_vlan_untag(struct sk_buff *skb);
int skb_ensure_writable(struct sk_buff *skb, int write_len);
int __skb_vlan_pop(struct sk_buff *skb, u16 *vlan_tci);
int skb_vlan_pop(struct sk_buff *skb);
int skb_vlan_push(struct sk_buff *skb, __be16 vlan_proto, u16 vlan_tci);
int skb_eth_pop(struct sk_buff *skb);
int skb_eth_push(struct sk_buff *skb, const unsigned char *dst,
		 const unsigned char *src);
int skb_mpls_push(struct sk_buff *skb, __be32 mpls_lse, __be16 mpls_proto,
		  int mac_len, bool ethernet);
int skb_mpls_pop(struct sk_buff *skb, __be16 next_proto, int mac_len,
		 bool ethernet);
int skb_mpls_update_lse(struct sk_buff *skb, __be32 mpls_lse);
int skb_mpls_dec_ttl(struct sk_buff *skb);
struct sk_buff *pskb_extract(struct sk_buff *skb, int off, int to_copy,
			     gfp_t gfp);

static inline int memcpy_from_msg(void *data, struct msghdr *msg, int len)
{
	return copy_from_iter_full(data, len, &msg->msg_iter) ? 0 : -EFAULT;
}

static inline int memcpy_to_msg(struct msghdr *msg, void *data, int len)
{
	return copy_to_iter(data, len, &msg->msg_iter) == len ? 0 : -EFAULT;
}

struct skb_checksum_ops {
	__wsum (*update)(const void *mem, int len, __wsum wsum);
	__wsum (*combine)(__wsum csum, __wsum csum2, int offset, int len);
};

extern const struct skb_checksum_ops *crc32c_csum_stub __read_mostly;

__wsum __skb_checksum(const struct sk_buff *skb, int offset, int len,
		      __wsum csum, const struct skb_checksum_ops *ops);
__wsum skb_checksum(const struct sk_buff *skb, int offset, int len,
		    __wsum csum);

static inline void * __must_check
__skb_header_pointer(const struct sk_buff *skb, int offset, int len,
		     const void *data, int hlen, void *buffer)
{
	if (likely(hlen - offset >= len))
		return (void *)data + offset;

	if (!skb || unlikely(skb_copy_bits(skb, offset, buffer, len) < 0))
		return NULL;

	return buffer;
}

static inline void * __must_check
skb_header_pointer(const struct sk_buff *skb, int offset, int len, void *buffer)
{
	return __skb_header_pointer(skb, offset, len, skb->data,
				    skb_headlen(skb), buffer);
}

/**
 *	skb_needs_linearize - check if we need to linearize a given skb
 *			      depending on the given device features.
 *	@skb: socket buffer to check
 *	@features: net device features
 *
 *	Returns true if either:
 *	1. skb has frag_list and the device doesn't support FRAGLIST, or
 *	2. skb is fragmented and the device does not support SG.
 */
static inline bool skb_needs_linearize(struct sk_buff *skb,
				       netdev_features_t features)
{
	return skb_is_nonlinear(skb) &&
	       ((skb_has_frag_list(skb) && !(features & NETIF_F_FRAGLIST)) ||
		(skb_shinfo(skb)->nr_frags && !(features & NETIF_F_SG)));
}

void skb_init(void);

static inline ktime_t skb_get_ktime(const struct sk_buff *skb)
{
	return skb->tstamp;
}

/**
 *	skb_get_timestamp - get timestamp from a skb
 *	@skb: skb to get stamp from
 *	@stamp: pointer to struct __kernel_old_timeval to store stamp in
 *
 *	Timestamps are stored in the skb as offsets to a base timestamp.
 *	This function converts the offset back to a struct timeval and stores
 *	it in stamp.
 */
static inline void skb_get_timestamp(const struct sk_buff *skb,
				     struct __kernel_old_timeval *stamp)
{
	*stamp = ns_to_kernel_old_timeval(skb->tstamp);
}

static inline void skb_get_new_timestamp(const struct sk_buff *skb,
					 struct __kernel_sock_timeval *stamp)
{
	struct timespec64 ts = ktime_to_timespec64(skb->tstamp);

	stamp->tv_sec = ts.tv_sec;
	stamp->tv_usec = ts.tv_nsec / 1000;
}

static inline void skb_get_timestampns(const struct sk_buff *skb,
				       struct __kernel_old_timespec *stamp)
{
	struct timespec64 ts = ktime_to_timespec64(skb->tstamp);

	stamp->tv_sec = ts.tv_sec;
	stamp->tv_nsec = ts.tv_nsec;
}

static inline void skb_get_new_timestampns(const struct sk_buff *skb,
					   struct __kernel_timespec *stamp)
{
	struct timespec64 ts = ktime_to_timespec64(skb->tstamp);

	stamp->tv_sec = ts.tv_sec;
	stamp->tv_nsec = ts.tv_nsec;
}

static inline void __net_timestamp(struct sk_buff *skb)
{
	skb->tstamp = ktime_get_real();
}

static inline ktime_t net_timedelta(ktime_t t)
{
	return ktime_sub(ktime_get_real(), t);
}

static inline ktime_t net_invalid_timestamp(void)
{
	return 0;
}

static inline u8 skb_metadata_len(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->meta_len;
}

static inline void *skb_metadata_end(const struct sk_buff *skb)
{
	return skb_mac_header(skb);
}

extern bool skb_metadata_differs(const struct sk_buff *skb_a, const struct sk_buff *skb_b);

static inline void skb_metadata_set(struct sk_buff *skb, u8 meta_len)
{
	skb_shinfo(skb)->meta_len = meta_len;
}

static inline void skb_metadata_clear(struct sk_buff *skb)
{
	skb_metadata_set(skb, 0);
}

struct sk_buff *skb_clone_sk(struct sk_buff *skb);

#ifdef CONFIG_NETWORK_PHY_TIMESTAMPING

void skb_clone_tx_timestamp(struct sk_buff *skb);
bool skb_defer_rx_timestamp(struct sk_buff *skb);

#else /* CONFIG_NETWORK_PHY_TIMESTAMPING */

static inline void skb_clone_tx_timestamp(struct sk_buff *skb)
{
}

static inline bool skb_defer_rx_timestamp(struct sk_buff *skb)
{
	return false;
}

#endif /* !CONFIG_NETWORK_PHY_TIMESTAMPING */

/**
 * skb_complete_tx_timestamp() - deliver cloned skb with tx timestamps
 *
 * PHY drivers may accept clones of transmitted packets for
 * timestamping via their phy_driver.txtstamp method. These drivers
 * must call this function to return the skb back to the stack with a
 * timestamp.
 *
 * @skb: clone of the original outgoing packet
 * @hwtstamps: hardware time stamps
 *
 */
void skb_complete_tx_timestamp(struct sk_buff *skb,
			       struct skb_shared_hwtstamps *hwtstamps);

void __skb_tstamp_tx(struct sk_buff *orig_skb, const struct sk_buff *ack_skb,
		     struct skb_shared_hwtstamps *hwtstamps,
		     struct sock *sk, int tstype);

/**
 * skb_tstamp_tx - queue clone of skb with send time stamps
 * @orig_skb:	the original outgoing packet
 * @hwtstamps:	hardware time stamps, may be NULL if not available
 *
 * If the skb has a socket associated, then this function clones the
 * skb (thus sharing the actual data and optional structures), stores
 * the optional hardware time stamping information (if non NULL) or
 * generates a software time stamp (otherwise), then queues the clone
 * to the error queue of the socket.  Errors are silently ignored.
 */
void skb_tstamp_tx(struct sk_buff *orig_skb,
		   struct skb_shared_hwtstamps *hwtstamps);

/**
 * skb_tx_timestamp() - Driver hook for transmit timestamping
 *
 * Ethernet MAC Drivers should call this function in their hard_xmit()
 * function immediately before giving the sk_buff to the MAC hardware.
 *
 * Specifically, one should make absolutely sure that this function is
 * called before TX completion of this packet can trigger.  Otherwise
 * the packet could potentially already be freed.
 *
 * @skb: A socket buffer.
 */
static inline void skb_tx_timestamp(struct sk_buff *skb)
{
	skb_clone_tx_timestamp(skb);
	if (skb_shinfo(skb)->tx_flags & SKBTX_SW_TSTAMP)
		skb_tstamp_tx(skb, NULL);
}

/**
 * skb_complete_wifi_ack - deliver skb with wifi status
 *
 * @skb: the original outgoing packet
 * @acked: ack status
 *
 */
void skb_complete_wifi_ack(struct sk_buff *skb, bool acked);

__sum16 __skb_checksum_complete_head(struct sk_buff *skb, int len);
__sum16 __skb_checksum_complete(struct sk_buff *skb);

static inline int skb_csum_unnecessary(const struct sk_buff *skb)
{
	return ((skb->ip_summed == CHECKSUM_UNNECESSARY) ||
		skb->csum_valid ||
		(skb->ip_summed == CHECKSUM_PARTIAL &&
		 skb_checksum_start_offset(skb) >= 0));
}

/**
 *	skb_checksum_complete - Calculate checksum of an entire packet
 *	@skb: packet to process
 *
 *	This function calculates the checksum over the entire packet plus
 *	the value of skb->csum.  The latter can be used to supply the
 *	checksum of a pseudo header as used by TCP/UDP.  It returns the
 *	checksum.
 *
 *	For protocols that contain complete checksums such as ICMP/TCP/UDP,
 *	this function can be used to verify that checksum on received
 *	packets.  In that case the function should return zero if the
 *	checksum is correct.  In particular, this function will return zero
 *	if skb->ip_summed is CHECKSUM_UNNECESSARY which indicates that the
 *	hardware has already verified the correctness of the checksum.
 */
static inline __sum16 skb_checksum_complete(struct sk_buff *skb)
{
	return skb_csum_unnecessary(skb) ?
	       0 : __skb_checksum_complete(skb);
}

static inline void __skb_decr_checksum_unnecessary(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
		if (skb->csum_level == 0)
			skb->ip_summed = CHECKSUM_NONE;
		else
			skb->csum_level--;
	}
}

static inline void __skb_incr_checksum_unnecessary(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
		if (skb->csum_level < SKB_MAX_CSUM_LEVEL)
			skb->csum_level++;
	} else if (skb->ip_summed == CHECKSUM_NONE) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->csum_level = 0;
	}
}

static inline void __skb_reset_checksum_unnecessary(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
		skb->ip_summed = CHECKSUM_NONE;
		skb->csum_level = 0;
	}
}

/* Check if we need to perform checksum complete validation.
 *
 * Returns true if checksum complete is needed, false otherwise
 * (either checksum is unnecessary or zero checksum is allowed).
 */
static inline bool __skb_checksum_validate_needed(struct sk_buff *skb,
						  bool zero_okay,
						  __sum16 check)
{
	if (skb_csum_unnecessary(skb) || (zero_okay && !check)) {
		skb->csum_valid = 1;
		__skb_decr_checksum_unnecessary(skb);
		return false;
	}

	return true;
}

/* For small packets <= CHECKSUM_BREAK perform checksum complete directly
 * in checksum_init.
 */
#define CHECKSUM_BREAK 76

/* Unset checksum-complete
 *
 * Unset checksum complete can be done when packet is being modified
 * (uncompressed for instance) and checksum-complete value is
 * invalidated.
 */
static inline void skb_checksum_complete_unset(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;
}

static inline __wsum null_compute_pseudo(struct sk_buff *skb, int proto)
{
	return 0;
}

static inline struct nf_conntrack *skb_nfct(const struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	return (void *)(skb->_nfct & NFCT_PTRMASK);
#else
	return NULL;
#endif
}

static inline unsigned long skb_get_nfct(const struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	return skb->_nfct;
#else
	return 0UL;
#endif
}

static inline void skb_set_nfct(struct sk_buff *skb, unsigned long nfct)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	skb->slow_gro |= !!nfct;
	skb->_nfct = nfct;
#endif
}

#ifdef CONFIG_SKB_EXTENSIONS

struct skb_ext *__skb_ext_alloc(gfp_t flags);
void *__skb_ext_set(struct sk_buff *skb, enum skb_ext_id id,
		    struct skb_ext *ext);
void *skb_ext_add(struct sk_buff *skb, enum skb_ext_id id);
void __skb_ext_del(struct sk_buff *skb, enum skb_ext_id id);
void __skb_ext_put(struct skb_ext *ext);

static inline void skb_ext_put(struct sk_buff *skb)
{
	if (skb->active_extensions)
		__skb_ext_put(skb->extensions);
}

static inline void __skb_ext_copy(struct sk_buff *dst,
				  const struct sk_buff *src)
{
	dst->active_extensions = src->active_extensions;

	if (src->active_extensions) {
		struct skb_ext *ext = src->extensions;

		refcount_inc(&ext->refcnt);
		dst->extensions = ext;
	}
}

static inline void skb_ext_copy(struct sk_buff *dst, const struct sk_buff *src)
{
	skb_ext_put(dst);
	__skb_ext_copy(dst, src);
}

static inline bool __skb_ext_exist(const struct skb_ext *ext, enum skb_ext_id i)
{
	return !!ext->offset[i];
}

static inline bool skb_ext_exist(const struct sk_buff *skb, enum skb_ext_id id)
{
	return skb->active_extensions & (1 << id);
}

static inline void skb_ext_del(struct sk_buff *skb, enum skb_ext_id id)
{
	if (skb_ext_exist(skb, id))
		__skb_ext_del(skb, id);
}

static inline void *skb_ext_find(const struct sk_buff *skb, enum skb_ext_id id)
{
	if (skb_ext_exist(skb, id)) {
		struct skb_ext *ext = skb->extensions;

		return (void *)ext + (ext->offset[id] << 3);
	}

	return NULL;
}

static inline void skb_ext_reset(struct sk_buff *skb)
{
	if (unlikely(skb->active_extensions)) {
		__skb_ext_put(skb->extensions);
		skb->active_extensions = 0;
	}
}

static inline bool skb_has_extensions(struct sk_buff *skb)
{
	return unlikely(skb->active_extensions);
}
#else
static inline void skb_ext_put(struct sk_buff *skb) {}
static inline void skb_ext_reset(struct sk_buff *skb) {}
static inline void skb_ext_del(struct sk_buff *skb, int unused) {}
static inline void __skb_ext_copy(struct sk_buff *d, const struct sk_buff *s) {}
static inline void skb_ext_copy(struct sk_buff *dst, const struct sk_buff *s) {}
static inline bool skb_has_extensions(struct sk_buff *skb) { return false; }
#endif /* CONFIG_SKB_EXTENSIONS */

static inline void ipvs_reset(struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_IP_VS)
	skb->ipvs_property = 0;
#endif
}

#ifdef CONFIG_NETWORK_SECMARK
static inline void skb_copy_secmark(struct sk_buff *to, const struct sk_buff *from)
{
	to->secmark = from->secmark;
}

static inline void skb_init_secmark(struct sk_buff *skb)
{
	skb->secmark = 0;
}
#else
static inline void skb_copy_secmark(struct sk_buff *to, const struct sk_buff *from)
{ }

static inline void skb_init_secmark(struct sk_buff *skb)
{ }
#endif

static inline int secpath_exists(const struct sk_buff *skb)
{
#ifdef CONFIG_XFRM
	return skb_ext_exist(skb, SKB_EXT_SEC_PATH);
#else
	return 0;
#endif
}

static inline bool skb_irq_freeable(const struct sk_buff *skb)
{
	return !skb->destructor &&
		!secpath_exists(skb) &&
		!skb_nfct(skb) &&
		!skb->_skb_refdst &&
		!skb_has_frag_list(skb);
}

static inline void skb_set_queue_mapping(struct sk_buff *skb, u16 queue_mapping)
{
	skb->queue_mapping = queue_mapping;
}

static inline u16 skb_get_queue_mapping(const struct sk_buff *skb)
{
	return skb->queue_mapping;
}

static inline void skb_copy_queue_mapping(struct sk_buff *to, const struct sk_buff *from)
{
	to->queue_mapping = from->queue_mapping;
}

static inline void skb_record_rx_queue(struct sk_buff *skb, u16 rx_queue)
{
	skb->queue_mapping = rx_queue + 1;
}

static inline u16 skb_get_rx_queue(const struct sk_buff *skb)
{
	return skb->queue_mapping - 1;
}

static inline bool skb_rx_queue_recorded(const struct sk_buff *skb)
{
	return skb->queue_mapping != 0;
}

static inline void skb_set_dst_pending_confirm(struct sk_buff *skb, u32 val)
{
	skb->dst_pending_confirm = val;
}

static inline bool skb_get_dst_pending_confirm(const struct sk_buff *skb)
{
	return skb->dst_pending_confirm != 0;
}

static inline struct sec_path *skb_sec_path(const struct sk_buff *skb)
{
#ifdef CONFIG_XFRM
	return skb_ext_find(skb, SKB_EXT_SEC_PATH);
#else
	return NULL;
#endif
}

/* Keeps track of mac header offset relative to skb->head.
 * It is useful for TSO of Tunneling protocol. e.g. GRE.
 * For non-tunnel skb it points to skb_mac_header() and for
 * tunnel skb it points to outer mac header.
 * Keeps track of level of encapsulation of network headers.
 */
struct skb_gso_cb {
	union {
		int	mac_offset;
		int	data_offset;
	};
	int	encap_level;
	__wsum	csum;
	__u16	csum_start;
};
#define SKB_GSO_CB_OFFSET	32
#define SKB_GSO_CB(skb) ((struct skb_gso_cb *)((skb)->cb + SKB_GSO_CB_OFFSET))

static inline int skb_tnl_header_len(const struct sk_buff *inner_skb)
{
	return (skb_mac_header(inner_skb) - inner_skb->head) -
		SKB_GSO_CB(inner_skb)->mac_offset;
}

static inline int gso_pskb_expand_head(struct sk_buff *skb, int extra)
{
	int new_headroom, headroom;
	int ret;

	headroom = skb_headroom(skb);
	ret = pskb_expand_head(skb, extra, 0, GFP_ATOMIC);
	if (ret)
		return ret;

	new_headroom = skb_headroom(skb);
	SKB_GSO_CB(skb)->mac_offset += (new_headroom - headroom);
	return 0;
}

static inline bool skb_is_gso(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_size;
}

/* Note: Should be called only if skb_is_gso(skb) is true */
static inline bool skb_is_gso_v6(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6;
}

/* Note: Should be called only if skb_is_gso(skb) is true */
static inline bool skb_is_gso_sctp(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_type & SKB_GSO_SCTP;
}

/* Note: Should be called only if skb_is_gso(skb) is true */
static inline bool skb_is_gso_tcp(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6);
}

static inline void skb_gso_reset(struct sk_buff *skb)
{
	skb_shinfo(skb)->gso_size = 0;
	skb_shinfo(skb)->gso_segs = 0;
	skb_shinfo(skb)->gso_type = 0;
}

static inline void skb_increase_gso_size(struct skb_shared_info *shinfo,
					 u16 increment)
{
	if (WARN_ON_ONCE(shinfo->gso_size == GSO_BY_FRAGS))
		return;
	shinfo->gso_size += increment;
}

static inline void skb_decrease_gso_size(struct skb_shared_info *shinfo,
					 u16 decrement)
{
	if (WARN_ON_ONCE(shinfo->gso_size == GSO_BY_FRAGS))
		return;
	shinfo->gso_size -= decrement;
}

void __skb_warn_lro_forwarding(const struct sk_buff *skb);

static inline bool skb_warn_if_lro(const struct sk_buff *skb)
{
	/* LRO sets gso_size but not gso_type, whereas if GSO is really
	 * wanted then gso_type will be set. */
	const struct skb_shared_info *shinfo = skb_shinfo(skb);

	if (skb_is_nonlinear(skb) && shinfo->gso_size != 0 &&
	    unlikely(shinfo->gso_type == 0)) {
		__skb_warn_lro_forwarding(skb);
		return true;
	}
	return false;
}

static inline void skb_forward_csum(struct sk_buff *skb)
{
	/* Unfortunately we don't support this one.  Any brave souls? */
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;
}

/**
 * skb_checksum_none_assert - make sure skb ip_summed is CHECKSUM_NONE
 * @skb: skb to check
 *
 * fresh skbs have their ip_summed set to CHECKSUM_NONE.
 * Instead of forcing ip_summed to CHECKSUM_NONE, we can
 * use this helper, to document places where we make this assertion.
 */
static inline void skb_checksum_none_assert(const struct sk_buff *skb)
{
#ifdef DEBUG
	BUG_ON(skb->ip_summed != CHECKSUM_NONE);
#endif
}

bool skb_partial_csum_set(struct sk_buff *skb, u16 start, u16 off);

int skb_checksum_setup(struct sk_buff *skb, bool recalculate);
struct sk_buff *skb_checksum_trimmed(struct sk_buff *skb,
				     unsigned int transport_len,
				     __sum16(*skb_chkf)(struct sk_buff *skb));

/**
 * skb_head_is_locked - Determine if the skb->head is locked down
 * @skb: skb to check
 *
 * The head on skbs build around a head frag can be removed if they are
 * not cloned.  This function returns true if the skb head is locked down
 * due to either being allocated via kmalloc, or by being a clone with
 * multiple references to the head.
 */
static inline bool skb_head_is_locked(const struct sk_buff *skb)
{
	return !skb->head_frag || skb_cloned(skb);
}

static inline bool skb_is_redirected(const struct sk_buff *skb)
{
	return skb->redirected;
}

static inline void skb_set_redirected(struct sk_buff *skb, bool from_ingress)
{
	skb->redirected = 1;
#ifdef CONFIG_NET_REDIRECT
	skb->from_ingress = from_ingress;
	if (skb->from_ingress)
		skb->tstamp = 0;
#endif
}

static inline void skb_reset_redirect(struct sk_buff *skb)
{
	skb->redirected = 0;
}

static inline bool skb_csum_is_sctp(struct sk_buff *skb)
{
	return skb->csum_not_inet;
}

static inline void skb_set_kcov_handle(struct sk_buff *skb,
				       const u64 kcov_handle)
{
#ifdef CONFIG_KCOV
	skb->kcov_handle = kcov_handle;
#endif
}

static inline u64 skb_get_kcov_handle(struct sk_buff *skb)
{
#ifdef CONFIG_KCOV
	return skb->kcov_handle;
#else
	return 0;
#endif
}

#endif	/* _LINUX_SKBUFF_API_H */
