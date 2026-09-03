/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * eXtensible Array. The implementation has been referred to Linux's
 * implementatoin significantly. However, the moderate adaption is
 * needed when it's ported to userspace. For example, we're always
 * able to sleep on allocating memory.
 *
 * Author: Gavin Shan <shan.gavin@gmail.com>
 */

#ifndef __MBOX_XARRAY_H
#define __MBOX_XARRAY_H

#include <mbox/base.h>
#include <mbox/math.h>
#include <semaphore.h>

#define XA_CHUNK_SHIFT		4
#define XA_CHUNK_SIZE		(1UL << XA_CHUNK_SHIFT)
#define XA_CHUNK_MASK		(XA_CHUNK_SIZE - 1)
#define XA_MAX_MARKS		3
#define XA_MARK_LONGS           DIV_ROUND_UP(XA_CHUNK_SIZE, BITS_PER_LONG)

#define XA_FLAGS_TRACK_FREE	1U
#define XA_FLAGS_ZERO_BUSY	2U
#define XA_FLAGS_ALLOC_WRAPPED	4U
#define XA_FLAGS_ACCOUNT	8U
#define XA_FLAGS_MARK(mark)	((1U << 4) << (mark))

struct xarray {
	sem_t		sem;		/* Semaphore */
	unsigned long	xa_flags;	/* Flags */
	void		*xa_head;	/* Head node */
};

typedef unsigned __bitwise xa_mark_t;
#define XA_MARK_0		((__force xa_mark_t)0U)
#define XA_MARK_1		((__force xa_mark_t)1U)
#define XA_MARK_2		((__force xa_mark_t)2U)
#define XA_PRESENT		((__force xa_mark_t)8U)
#define XA_MARK_MAX		XA_MARK_2
#define XA_FREE_MARK		XA_MARK_0

struct xa_node {
	unsigned char	shift;		/* Bits remaining in each slot */
	unsigned char	offset;		/* Slot offset in parent */
	unsigned char	count;		/* Total entry count */
	unsigned char	nr_values;	/* Value entry count */
	struct xa_node	*parent;	/* NULL at top of tree */
	struct xarray	*array;		/* The xarray it belongs to */
	/* struct list_head private_list */
	void		*slots[XA_CHUNK_SIZE];
	union {
		unsigned long	tags[XA_MAX_MARKS][XA_MARK_LONGS];
		unsigned long	marks[XA_MAX_MARKS][XA_MARK_LONGS];
	};
};

typedef void (*xa_update_node_t)(struct xa_node *node);

struct xa_state {
	struct xarray		*xa;
	unsigned long		xa_index;
	unsigned char		xa_shift;
	unsigned char		xa_sibs;
	unsigned char		xa_offset;
	unsigned char		xa_pad;
	struct xa_node		*xa_node;
	struct xa_node		*xa_alloc;
	xa_update_node_t	xa_update;
	/* struct list_lru	*xa_lru */
};

#define XA_ERROR(errno)	((struct xa_node *)(((unsigned long)errno << 2) | 2UL))
#define XAS_BOUNDS	((struct xa_node *)1UL)
#define XAS_RESTART	((struct xa_node *)3UL)

#define __XA_STATE(array, index, shift, sibs)  {	\
        .xa = array,					\
        .xa_index = index,				\
        .xa_shift = shift,				\
        .xa_sibs = sibs,				\
        .xa_offset = 0,					\
        .xa_pad = 0,					\
        .xa_node = XAS_RESTART,				\
        .xa_alloc = NULL,				\
        .xa_update = NULL,				\
}

#define XA_STATE(name, array, index)			\
	struct xa_state name = __XA_STATE(array, index, 0, 0)
#define XA_STATE_ORDER(name, array, index, order)	\
	struct xa_state name = __XA_STATE(array,	\
		(index >> order) << order,		\
		order - (order % XA_CHUNK_SHIFT),	\
		(1U << (order % XA_CHUNK_SHIFT)) - 1)

/*
 * The least two bits of the entry determine its type:
 *
 * 00: Pointer entry
 * 10: Internal entry
 * x1: Value entry or tagged pointer
 *
 * The internal entry is usually the parent node of its descendent entries.
 * However, the following internal entries have special usages:
 *
 * 0 to 62        Sibling entries
 * 256            Retry entry
 * 257            Zero entry
 * -2 to -4094    Erroneous entries
 */

static inline bool xa_is_value(const void *entry)
{
	return (unsigned long)entry & 1;
}

static inline unsigned long xa_to_value(const void *entry)
{
	return (unsigned long)entry >> 1;
}

static inline void *xa_mk_value(unsigned long v)
{
	return (void *)((v << 1) | 1);
}

static inline unsigned int xa_pointer_tag(void *entry)
{
	return (unsigned long)entry & 3UL;
}

static inline void *xa_untag_pointer(void *entry)
{
	return (void *)((unsigned long)entry & ~3UL);
}

static inline void *xa_tag_pointer(void *p, unsigned long tag)
{
	return (void *)((unsigned long)p | tag);
}

static inline bool xa_is_internal(const void *entry)
{
	return ((unsigned long)entry & 3) == 2;
}

static inline unsigned long xa_to_internal(const void *entry)
{
	return (unsigned long)entry >> 2;
}

static inline void *xa_mk_internal(unsigned long v)
{
	return (void *)((v << 2) | 2);
}

static inline bool xa_is_node(const void *entry)
{
	return xa_is_internal(entry) && (unsigned long)entry > 4096;
}

static inline struct xa_node *xa_to_node(const void *entry)
{
	return (struct xa_node *)((unsigned long)entry - 2);
}

static inline void *xa_mk_node(const struct xa_node *node)
{
	return (void *)((unsigned long)node | 2);
}

static inline void *xa_mk_sibling(unsigned int offset)
{
	return xa_mk_internal(offset);
}

static inline bool xa_is_sibling(const void *entry)
{
        return xa_is_internal(entry) && (entry < xa_mk_sibling(XA_CHUNK_SIZE - 1));
}

static inline unsigned long xa_to_sibling(const void *entry)
{
	return xa_to_internal(entry);
}

#define XA_RETRY_ENTRY	xa_mk_internal(256)
#define XA_ZERO_ENTRY	xa_mk_internal(257)

static inline bool xa_is_retry(const void *entry)
{
	return entry == XA_RETRY_ENTRY;
}

static inline bool xa_is_zero(const void *entry)
{
	return entry == XA_ZERO_ENTRY;
}

static inline bool xa_is_advanced(const void *entry)
{
	return xa_is_internal(entry) && (entry <= XA_RETRY_ENTRY);
}

static inline bool xa_is_err(const void *entry)
{
	return xa_is_internal(entry) && entry >= xa_mk_internal(-4095);
}

static inline int xa_err(void *entry)
{
	if (xa_is_err(entry))
		return (long)entry >> 2;
	return 0;
}

/* XArray helpers */
static inline void *xa_head(const struct xarray *xa)
{
	return xa->xa_head;
}

static inline void *xa_entry(const struct xarray *xa,
			     const struct xa_node *node,
			     unsigned int offset)
{
	return node->slots[offset];
}

static inline struct xa_node *xa_parent(const struct xarray *xa,
					const struct xa_node *node)
{

	return node->parent;
}

/* XArray state helpers */
static inline void xas_set(struct xa_state *xas, unsigned long index)
{
	xas->xa_index = index;
	xas->xa_node = XAS_RESTART;
}

static inline void xas_set_order(struct xa_state *xas,
				 unsigned long index,
				 unsigned int order)
{
	xas->xa_index = order < BITS_PER_LONG ? (index >> order) << order : 0;
	xas->xa_shift = order - (order % XA_CHUNK_SHIFT);
	xas->xa_sibs = (1 << (order % XA_CHUNK_SHIFT)) - 1;
	xas->xa_node = XAS_RESTART;
}

static inline int xas_error(const struct xa_state *xas)
{
	return xa_err(xas->xa_node);
}

static inline void xas_set_err(struct xa_state *xas, long err)
{
        xas->xa_node = XA_ERROR(err);
}

static inline bool xas_invalid(const struct xa_state *xas)
{
	return (unsigned long)xas->xa_node & 3;
}

static inline bool xas_valid(const struct xa_state *xas)
{
	return !xas_invalid(xas);
}

static inline bool xas_is_node(const struct xa_state *xas)
{
	return xas_valid(xas) && xas->xa_node;
}

static inline bool xas_not_node(struct xa_node *node)
{
	return ((unsigned long)node & 3) || !node;
}

static inline bool xas_top(struct xa_node *node)
{
	return node <= XAS_RESTART;
}

static inline bool xas_frozen(struct xa_node *node)
{
	return (unsigned long)node & 2;
}

static inline void xas_reset(struct xa_state *xas)
{
	xas->xa_node = XAS_RESTART;
}

static inline bool xas_retry(struct xa_state *xas, const void *entry)
{
	if (xa_is_zero(entry))
		return true;
	if (!xa_is_retry(entry))
		return false;

	xas_reset(xas);
	return true;
}

/* Public APIs */
void xa_init(struct xarray *xa);
void *xa_load(struct xarray *xa, unsigned long index);
void *xa_store(struct xarray *xa, unsigned long index, void *entry);
void *xa_store_range(struct xarray *xa, unsigned long first,
                     unsigned long last, void *entry);
void *xa_find(struct xarray *xa, unsigned long *indexp,
	      unsigned long max, xa_mark_t filter);
void *xa_find_after(struct xarray *xa, unsigned long *indexp,
		    unsigned long max, xa_mark_t filter);
int xa_get_order(struct xarray *, unsigned long index);
void *xa_erase(struct xarray *xa, unsigned long index);
bool xa_get_mark(struct xarray *xa, unsigned long index, xa_mark_t mark);
void xa_set_mark(struct xarray *xa, unsigned long index, xa_mark_t mark);
void xa_clear_mark(struct xarray *xa, unsigned long index, xa_mark_t mark);
#if 0
unsigned int xa_extract(struct xarray *, void **dst, unsigned long start,
			unsigned long max, unsigned int n, xa_mark_t);
void xa_destroy(struct xarray *);
#endif

void *xas_load(struct xa_state *xas);
void *xas_store(struct xa_state *xas, void *entry);
void xas_create_range(struct xa_state *xas);
void xas_split(struct xa_state *, void *entry, unsigned int order);
void xas_split_alloc(struct xa_state *, void *entry, unsigned int order);
void *xas_find(struct xa_state *, unsigned long max);
void *xas_find_conflict(struct xa_state *xas);

void xas_init_marks(const struct xa_state *xas);
bool xas_get_mark(const struct xa_state *xas, xa_mark_t mark);
void xas_set_mark(const struct xa_state *xas, xa_mark_t mark);
void xas_clear_mark(const struct xa_state *xas, xa_mark_t mark);
void *xas_find_marked(struct xa_state *xas, unsigned long max, xa_mark_t mark);

void xas_pause(struct xa_state *xas);
bool xas_nomem(struct xa_state *xas);
void xas_destroy(struct xa_state *xas);

#define xas_for_each_conflict(xas, entry) \
	while ((entry = xas_find_conflict(xas)))

#endif /* __MBOX_XARRAY_H */
