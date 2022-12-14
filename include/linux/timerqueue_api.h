/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMERQUEUE_API_H
#define _LINUX_TIMERQUEUE_API_H

#include <linux/rbtree_api.h>
#include <linux/timerqueue_types.h>
#include <linux/rbtree.h>
#include <linux/bug.h>

extern bool timerqueue_add(struct timerqueue_head *head,
			   struct timerqueue_node *node);
extern bool timerqueue_del(struct timerqueue_head *head,
			   struct timerqueue_node *node);
extern struct timerqueue_node *timerqueue_iterate_next(
						struct timerqueue_node *node);

/**
 * timerqueue_getnext - Returns the timer with the earliest expiration time
 *
 * @head: head of timerqueue
 *
 * Returns a pointer to the timer node that has the earliest expiration time.
 */
static inline
struct timerqueue_node *timerqueue_getnext(struct timerqueue_head *head)
{
	struct rb_node *leftmost = rb_first_cached(&head->rb_root);

	return rb_entry(leftmost, struct timerqueue_node, node);
}

static inline void timerqueue_init(struct timerqueue_node *node)
{
	RB_CLEAR_NODE(&node->node);
}

static inline bool timerqueue_node_queued(struct timerqueue_node *node)
{
	return !RB_EMPTY_NODE(&node->node);
}

static inline bool timerqueue_node_expires(struct timerqueue_node *node)
{
	return node->expires;
}

static inline void timerqueue_init_head(struct timerqueue_head *head)
{
	head->rb_root = RB_ROOT_CACHED;
}
#endif /* _LINUX_TIMERQUEUE_API_H */
