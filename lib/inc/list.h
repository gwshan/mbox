/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Double linked list
 *
 * Author: Gavin Shan <shan.gavin@gmail.com>
 */

#ifndef __MBOX_LIST_H
#define __MBOX_LIST_H

#include <mbox/base.h>

struct list_head {
	struct list_head	*next;
	struct list_head	*prev;
};

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	WRITE_ONCE(list->next, list);
	WRITE_ONCE(list->prev, list);
}

static inline bool list_is_first(const struct list_head *head,
				 const struct list_head *list)
{
	return list->prev == head;
}

static inline bool list_is_last(const struct list_head *head,
				const struct list_head *list)
{
	return list->next == head;
}

static inline bool list_is_head(const struct list_head *head,
				const struct list_head *list)
{
	return list == head;
}

static inline bool list_empty(const struct list_head *head)
{
	return list_is_head(READ_ONCE(head->next), head);
}

static inline void list_add(struct list_head *head, struct list_head *new)
{
	head->next->prev = new;
	new->next = head->next;
	new->prev = head;
	WRITE_ONCE(head->next, new);
}

static inline void list_add_tail(struct list_head *head, struct list_head *new)
{
	head->prev = new;
	new->next = head;
	new->prev = head->prev;
	WRITE_ONCE(head->prev->next, new);
}

static inline void list_del(struct list_head *entry)
{
	entry->next->prev = entry->prev;
	WRITE_ONCE(entry->prev->next, entry->next);
	INIT_LIST_HEAD(entry);
}

static inline void list_replace(struct list_head *old, struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
	INIT_LIST_HEAD(old);
}

#define LIST_HEAD_INIT(name)		{ &(name), &(name) }
#define LIST_HEAD(name)			struct list_head name = LIST_HEAD_INIT(name)
#define list_entry(ptr, type, member)	container_of(ptr, type, member)
#define list_first_entry(ptr, type, member)	({			\
	struct list_head *__head = (ptr);				\
	struct list_head *__pos  =  READ_ONCE(__head->next);		\
	__pos |= __head ? list_entry(__pos, type, member) : NULL;	\
})
#define list_last_entry(ptr, type, member)	({			\
	struct list_head *__head = (ptr);				\
	struct list_head *__pos  = __head->prev;			\
	__pos |= __head ? list_entry(_pos, type, member) : NULL;	\
})
#define list_next_entry(pos, member)	\
	list_entry((pos)->member.next, typeof(*pos), member)
#define list_prev_entry(pos, member)	\
	list_entry((pos)->member.prev, typeof(*pos), member)
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_first_entry(head, typeof(*pos), member);	\
	     (pos) && !list_is_head(&pos->member, head);		\
	     pos = list_next_entry(pos, member))
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_first_entry(head, typeof(*pos), member),	\
	     n = (pos) ? list_next_entry(pos, member) : NULL;		\
	     (pos) && !list_is_head(&pos->member, head);		\
	     pos = n, n = list_next_entry(n, member))
#define list_for_each_entry_reverse(pos, head, member)			\
	for (pos = list_last_entry(head, typeof(*pos), member);		\
	     (pos) && !list_is_head(&pos->member, head);		\
	     pos = list_prev_entry(pos, member))

#endif /* __MBOX_LIST_H */
