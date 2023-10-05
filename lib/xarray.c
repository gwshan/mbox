/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * eXtensible Array
 *
 * FIXME:
 * a) Support test_bit() and its variant;
 */

#include <mbox/xarray.h>

/************************* Helpers ************************/

static inline void xa_lock(struct xarray *xa)
{
	sem_wait(&xa->sem);
}

static inline void xa_unlock(struct xarray *xa)
{
	sem_post(&xa->sem);
}

static inline bool xa_track_free(const struct xarray *xa)
{
	return xa->xa_flags & XA_FLAGS_TRACK_FREE;
}

static inline bool xa_zero_busy(const struct xarray *xa)
{
	return xa->xa_flags & XA_FLAGS_ZERO_BUSY;
}

static inline bool xa_marked(const struct xarray *xa, xa_mark_t mark)
{
	return xa->xa_flags & XA_FLAGS_MARK(mark);
}

static inline void xa_mark_set(struct xarray *xa, xa_mark_t mark)
{
	if (!(xa->xa_flags & XA_FLAGS_MARK(mark)))
		xa->xa_flags |= XA_FLAGS_MARK(mark);
}

static inline void xa_mark_clear(struct xarray *xa, xa_mark_t mark)
{
	if (xa->xa_flags & XA_FLAGS_MARK(mark))
		xa->xa_flags &= ~(XA_FLAGS_MARK(mark));
}

static inline unsigned long *node_marks(struct xa_node *node, xa_mark_t mark)
{
	return node->marks[(__force unsigned)mark];
}

static inline bool node_get_mark(struct xa_node *node,
				 unsigned int offset, xa_mark_t mark)
{
	// return test_bit(offset, node_marks(node, mark));
	return false;
}

static inline bool node_set_mark(struct xa_node *node,
				 unsigned int offset, xa_mark_t mark)
{
	// return __test_and_set_bit(offset, node_marks(node, mark));
	return false;
}

static inline bool node_clear_mark(struct xa_node *node,
				   unsigned int offset, xa_mark_t mark)
{
	// return __test_and_clear_bit(offset, node_marks(node, mark));
	return true;
}

static inline bool node_any_mark(struct xa_node *node, xa_mark_t mark)
{
	// return !bitmap_empty(node_marks(node, mark), XA_CHUNK_SIZE);
	return false;
}

static inline void node_mark_all(struct xa_node *node, xa_mark_t mark)
{
	// bitmap_fill(node_marks(node, mark), XA_CHUNK_SIZE);
}

#define mark_inc(mark) do { \
        mark = (__force xa_mark_t)((__force unsigned)(mark) + 1); \
} while (0)

static unsigned int node_get_marks(struct xa_node *node, unsigned int offset)
{
	unsigned int marks = 0;
	xa_mark_t mark = XA_MARK_0;

	for (;;) {
		if (node_get_mark(node, offset, mark))
			marks |= 1 << (__force unsigned int)mark;
		if (mark == XA_MARK_MAX)
			break;
		mark_inc(mark);
	}

	return marks;
}

static void node_set_marks(struct xa_node *node, unsigned int offset,
			   struct xa_node *child, unsigned int marks)
{
	xa_mark_t mark = XA_MARK_0;

	for (;;) {
		if (marks & (1 << (__force unsigned int)mark)) {
			node_set_mark(node, offset, mark);
			if (child)
				node_mark_all(child, mark);
		}
		if (mark == XA_MARK_MAX)
			break;
		mark_inc(mark);
	}
}

static void xa_node_free(struct xa_node *node)
{
	free(node);
}

static void xas_squash_marks(const struct xa_state *xas)
{
#if 0 /* TODO */
	unsigned int mark = 0;
	unsigned int limit = xas->xa_offset + xas->xa_sibs + 1;
	unsigned long *marks;

	if (!xas->xa_sibs)
		return;

	do {
		marks = xas->xa_node->marks[mark];
		if (find_next_bit(marks, limit, xas->xa_offset + 1) == limit)
			continue;

		__set_bit(xas->xa_offset, marks);
                bitmap_clear(marks, xas->xa_offset + 1, xas->xa_sibs);
        } while (mark++ != (__force unsigned)XA_MARK_MAX);
#endif
}

static inline unsigned int get_offset(struct xa_node *node,
				      unsigned long index)
{
	return (index >> node->shift) & XA_CHUNK_MASK;
}

static inline void *set_bounds(struct xa_state *xas)
{
	xas->xa_node = XAS_BOUNDS;
	return NULL;
}

static unsigned long max_index(void *entry)
{
	if (!xa_is_node(entry))
		return 0;

	return (XA_CHUNK_SIZE << xa_to_node(entry)->shift) - 1;
}

static unsigned long xas_size(const struct xa_state *xas)
{
	return (xas->xa_sibs + 1UL) << xas->xa_shift;
}

static unsigned long xas_max(struct xa_state *xas)
{
	unsigned long max = xas->xa_index;
	unsigned long mask;

	if (xas->xa_shift || xas->xa_sibs) {
		mask = xas_size(xas) - 1;
		max |= mask;
		if (mask == max)
			max++;
	}

	return max;
}

static void xas_set_offset(struct xa_state *xas)
{
	xas->xa_offset = get_offset(xas->xa_node, xas->xa_index);
}

static void xas_move_index(struct xa_state *xas, unsigned long offset)
{
	unsigned int shift = xas->xa_node->shift;

	xas->xa_index &= ~XA_CHUNK_MASK << shift;
	xas->xa_index += offset << shift;
}

static void xas_next_offset(struct xa_state *xas)
{
	xas->xa_offset++;
	xas_move_index(xas, xas->xa_offset);
}

static unsigned int xas_find_chunk(struct xa_state *xas,
				   bool advance, xa_mark_t mark)
{
	unsigned long *addr = xas->xa_node->marks[(__force unsigned)mark];
	unsigned int offset = xas->xa_offset;
	unsigned long data;

	if (advance)
		offset++;
	if (XA_CHUNK_SIZE == BITS_PER_LONG) {
		if (offset < XA_CHUNK_SIZE) {
			data = *addr & (~0UL << offset);
			if (data)
				return __ffs(data);
		}

		return XA_CHUNK_SIZE;
	}

	// return find_next_bit(addr, XA_CHUNK_SIZE, offset);
	return offset;
}

static bool xas_is_sibling(struct xa_state *xas)
{
	struct xa_node *node = xas->xa_node;
	unsigned long mask;

	if (!node)
		return false;

	mask = (XA_CHUNK_SIZE << node->shift) - 1;
	return (xas->xa_index & mask) >
		((unsigned long)xas->xa_offset << node->shift);
}

static void *xas_result(struct xa_state *xas, void *curr)
{
	if (xa_is_zero(curr))
		return NULL;

	if (xas_error(xas))
		curr = xas->xa_node;

	return curr;
}

static void xas_update(struct xa_state *xas, struct xa_node *node)
{
	if (xas->xa_update)
		xas->xa_update(node);
}

void xas_pause(struct xa_state *xas)
{
	struct xa_node *node = xas->xa_node;
	unsigned long offset;

	if (xas_invalid(xas))
		return;

	xas->xa_node = XAS_RESTART;
	if (node) {
		offset = xas->xa_offset;
		while (++offset < XA_CHUNK_SIZE) {
			if (!xa_is_sibling(xa_entry(xas->xa, node, offset)))
				break;
		}

		xas->xa_index += (offset - xas->xa_offset) << node->shift;
		if (xas->xa_index == 0)
			xas->xa_node = XAS_BOUNDS;
	} else {
		xas->xa_index++;
	}
}

bool xas_nomem(struct xa_state *xas)
{
	if (xas->xa_node != XA_ERROR(-ENOMEM)) {
		xas_destroy(xas);
		return false;
	}

	xas->xa_alloc = (struct xa_node *)malloc(sizeof(struct xa_node));
	if (!xas->xa_alloc)
		return false;

	xas->xa_alloc->parent = NULL;
	xas->xa_node = XAS_RESTART;
	return true;
}

void xas_destroy(struct xa_state *xas)
{
	struct xa_node *next, *node = xas->xa_alloc;

	while (node) {
		next = node->parent;
		free(node);
		xas->xa_alloc = node = next;
        }
}

static void xas_shrink(struct xa_state *xas)
{
	struct xarray *xa = xas->xa;
	struct xa_node *node = xas->xa_node;
	void *entry;

        for (;;) {
		if (node->count != 1)
			break;
		entry = xa_entry(xa, node, 0);
		if (!entry)
			break;
		if (!xa_is_node(entry) && node->shift)
			break;
		if (xa_is_zero(entry) && xa_zero_busy(xa))
			entry = NULL;
		xas->xa_node = XAS_BOUNDS;

		xa->xa_head = entry;
		if (xa_track_free(xa) && !node_get_mark(node, 0, XA_FREE_MARK))
			xa_mark_clear(xa, XA_FREE_MARK);

		node->count = 0;
		node->nr_values = 0;
		if (!xa_is_node(entry))
			node->slots[0] = XA_RETRY_ENTRY;
		xas_update(xas, node);
		xa_node_free(node);
		if (!xa_is_node(entry))
			break;
		node = xa_to_node(entry);
		node->parent = NULL;
	}
}

static void xas_delete_node(struct xa_state *xas)
{
        struct xa_node *node = xas->xa_node;

        for (;;) {
                struct xa_node *parent;

                if (node->count)
                        break;

                parent = xa_parent(xas->xa, node);
                xas->xa_node = parent;
                xas->xa_offset = node->offset;
                xa_node_free(node);

		if (!parent) {
			xas->xa->xa_head = NULL;
			xas->xa_node = XAS_BOUNDS;
			return;
		}

		parent->slots[xas->xa_offset] = NULL;
		parent->count--;
		node = parent;
		xas_update(xas, node);
	}

	if (!node->parent)
		xas_shrink(xas);
}

static void update_node(struct xa_state *xas,
			struct xa_node *node,
			int count, int values)
{
	if (!node || (!count && !values))
		return;

	node->count += count;
	node->nr_values += values;
	xas_update(xas, node);
	if (count < 0)
		xas_delete_node(xas);
}

static void *xas_alloc(struct xa_state *xas, unsigned int shift)
{
	struct xa_node *parent = xas->xa_node;
	struct xa_node *node = xas->xa_alloc;

	if (xas_invalid(xas))
		return NULL;

	if (node) {
		xas->xa_alloc = NULL;
        } else {
		node = malloc(sizeof(*node));
		if (!node) {
			xas_set_err(xas, -ENOMEM);
			return NULL;
		}
	}

        if (parent) {
		node->offset = xas->xa_offset;
		parent->count++;
		xas_update(xas, parent);
	}

	node->shift = shift;
	node->count = 0;
	node->nr_values = 0;
	node->parent = xas->xa_node;
	node->array = xas->xa;

	return node;
}

static void xas_free_nodes(struct xa_state *xas, struct xa_node *top)
{
	unsigned int offset = 0;
	struct xa_node *parent, *node = top;
	void *entry;

        for (;;) {
		entry = xa_entry(xas->xa, node, offset);
		if (node->shift && xa_is_node(entry)) {
			node = xa_to_node(entry);
			offset = 0;
			continue;
		}

		if (entry)
			node->slots[offset] = XA_RETRY_ENTRY;

		offset++;
		while (offset == XA_CHUNK_SIZE) {
			parent = xa_parent(xas->xa, node);
			offset = node->offset + 1;
			node->count = 0;
			node->nr_values = 0;
			xas_update(xas, node);
			xa_node_free(node);
			if (node == top)
				return;

			node = parent;
		}
	}
}

static void *xas_descend(struct xa_state *xas, struct xa_node *node)
{
	unsigned int offset = get_offset(node, xas->xa_index);
	void *entry = xa_entry(xas->xa, node, offset);

	xas->xa_node = node;
	while (xa_is_sibling(entry)) {
		offset = xa_to_sibling(entry);
		entry = xa_entry(xas->xa, node, offset);
		if (node->shift && xa_is_node(entry))
			entry = XA_RETRY_ENTRY;
	}

	xas->xa_offset = offset;
	return entry;
}

static inline void *xas_reload(struct xa_state *xas)
{
	struct xa_node *node = xas->xa_node;
	void *entry;
	char offset;

	if (!node)
		return xa_head(xas->xa);

	offset = (xas->xa_index >> node->shift) & XA_CHUNK_MASK;
	entry = xa_entry(xas->xa, node, offset);
	if (!xa_is_sibling(entry))
		return entry;

	offset = xa_to_sibling(entry);
	return xa_entry(xas->xa, node, offset);
}

static void *xas_start(struct xa_state *xas)
{
	void *entry;

	if (xas_valid(xas))
		return xas_reload(xas);

	if (xas_error(xas))
		return NULL;

	entry = xa_head(xas->xa);
	if (!xa_is_node(entry)) {
		if (xas->xa_index)
			return set_bounds(xas);
	} else {
		if ((xas->xa_index >> xa_to_node(entry)->shift) > XA_CHUNK_MASK)
			return set_bounds(xas);
	}

	xas->xa_node = NULL;
	return entry;
}

void *xas_load(struct xa_state *xas)
{
	struct xa_node *node;
	void *entry = xas_start(xas);

	while (xa_is_node(entry)) {
		node = xa_to_node(entry);

		if (xas->xa_shift > node->shift)
			break;
		entry = xas_descend(xas, node);
		if (node->shift == 0)
			break;
	}

	return entry;
}

static int xas_expand(struct xa_state *xas, void *head)
{
	struct xarray *xa = xas->xa;
	struct xa_node *node = NULL;
	unsigned int shift = 0;
	unsigned long max = xas_max(xas);
	xa_mark_t mark;

	if (!head) {
		if (max == 0)
			return 0;
		while ((max >> shift) >= XA_CHUNK_SIZE)
			shift += XA_CHUNK_SHIFT;
		return shift + XA_CHUNK_SHIFT;
        }

	if (xa_is_node(head)) {
		node = xa_to_node(head);
		shift = node->shift + XA_CHUNK_SHIFT;
	}

	xas->xa_node = NULL;
	while (max > max_index(head)) {
                mark = 0;

		node = xas_alloc(xas, shift);
		if (!node)
			return -ENOMEM;

		node->count = 1;
		if (xa_is_value(head))
			node->nr_values = 1;
		node->slots[0] = head;

		/* Propagate the aggregated mark info to the new child */
		for (;;) {
			if (xa_track_free(xa) && mark == XA_FREE_MARK) {
				node_mark_all(node, XA_FREE_MARK);
				if (!xa_marked(xa, XA_FREE_MARK)) {
					node_clear_mark(node, 0, XA_FREE_MARK);
					xa_mark_set(xa, XA_FREE_MARK);
				}
			} else if (xa_marked(xa, mark)) {
				node_set_mark(node, 0, mark);
			}

			if (mark == XA_MARK_MAX)
				break;

			mark_inc(mark);
		}

		/*
		 * Now that the new node is fully initialised, we can add
		 * it to the tree
		 */
		if (xa_is_node(head)) {
			xa_to_node(head)->offset = 0;
			xa_to_node(head)->parent = node;
		}

		head = xa_mk_node(node);
		xa->xa_head = head;
		xas_update(xas, node);

		shift += XA_CHUNK_SHIFT;
	}

	xas->xa_node = node;
	return shift;
}

static void *xas_create(struct xa_state *xas, bool allow_root)
{
	struct xarray *xa = xas->xa;
	struct xa_node *node = xas->xa_node;
	unsigned int offset, order = xas->xa_shift;
	void *entry, **slot;
	int shift;

	if (xas_top(node)) {
		entry = xa_head(xa);
		xas->xa_node = NULL;
		if (!entry && xa_zero_busy(xa))
			entry = XA_ZERO_ENTRY;
		shift = xas_expand(xas, entry);
		if (shift < 0)
			return NULL;
		if (!shift && !allow_root)
			shift = XA_CHUNK_SHIFT;
		entry = xa_head(xa);
		slot = &xa->xa_head;
	} else if (xas_error(xas)) {
		return NULL;
	} else if (node) {
		offset = xas->xa_offset;

		shift = node->shift;
		entry = xa_entry(xa, node, offset);
		slot = &node->slots[offset];
	} else {
		shift = 0;
		entry = xa_head(xa);
		slot = &xa->xa_head;
	}

	while (shift > order) {
		shift -= XA_CHUNK_SHIFT;
		if (!entry) {
			node = xas_alloc(xas, shift);
			if (!node)
				break;

			if (xa_track_free(xa))
				node_mark_all(node, XA_FREE_MARK);
			*slot = xa_mk_node(node);
		} else if (xa_is_node(entry)) {
			node = xa_to_node(entry);
		} else {
			break;
		}

		entry = xas_descend(xas, node);
		slot = &node->slots[xas->xa_offset];
	}

	return entry;
}

void *xas_store(struct xa_state *xas, void *entry)
{
	struct xa_node *node;
	void **slot = &xas->xa->xa_head;
	unsigned int offset, max;
	int count = 0;
	int values = 0;
	void *first, *next;
	bool value = xa_is_value(entry);
	bool allow_root;

	if (entry) {
		allow_root = !xa_is_node(entry) && !xa_is_zero(entry);
		first = xas_create(xas, allow_root);
	} else {
		first = xas_load(xas);
	}

	if (xas_invalid(xas))
		return first;

	node = xas->xa_node;
	if (node && (xas->xa_shift < node->shift))
		xas->xa_sibs = 0;
	if ((first == entry) && !xas->xa_sibs)
		return first;

	next = first;
	offset = xas->xa_offset;
	max = xas->xa_offset + xas->xa_sibs;
	if (node) {
		slot = &node->slots[offset];
		if (xas->xa_sibs)
			xas_squash_marks(xas);
        }
        if (!entry)
                xas_init_marks(xas);

	for (;;) {
		/*
		 * Must clear the marks before setting the entry to NULL,
		 * otherwise xas_for_each_marked may find a NULL entry and
		 * stop early.  rcu_assign_pointer contains a release barrier
		 * so the mark clearing will appear to happen before the
		 * entry is set to NULL.
		 */
		*slot = entry;
		if (xa_is_node(next) && (!node || node->shift))
			xas_free_nodes(xas, xa_to_node(next));
		if (!node)
			break;

		count += !next - !entry;
		values += !xa_is_value(first) - !value;
		if (entry) {
			if (offset == max)
				break;
			if (!xa_is_sibling(entry))
				entry = xa_mk_sibling(xas->xa_offset);
		} else {
			if (offset == XA_CHUNK_MASK)
				break;
		}
		next = xa_entry(xas->xa, node, ++offset);
		if (!xa_is_sibling(next)) {
			if (!entry && (offset > max))
				break;
			first = next;
		}
		slot++;
	}

	update_node(xas, node, count, values);
	return first;
}

void xas_create_range(struct xa_state *xas)
{
	struct xa_node *node;
	unsigned long index = xas->xa_index;
	unsigned char shift = xas->xa_shift;
	unsigned char sibs = xas->xa_sibs;

	xas->xa_index |= ((sibs + 1UL) << shift) - 1;
	if (xas_is_node(xas) && xas->xa_node->shift == xas->xa_shift)
		xas->xa_offset |= sibs;
	xas->xa_shift = 0;
	xas->xa_sibs = 0;

	for (;;) {
		xas_create(xas, true);
		if (xas_error(xas))
			goto restore;
		if (xas->xa_index <= (index | XA_CHUNK_MASK))
			goto success;
		xas->xa_index -= XA_CHUNK_SIZE;

		for (;;) {
			node = xas->xa_node;
			if (node->shift >= shift)
				break;
			xas->xa_node = xa_parent(xas->xa, node);
			xas->xa_offset = node->offset - 1;
			if (node->offset != 0)
				break;
		}
	}

restore:
	xas->xa_shift = shift;
	xas->xa_sibs = sibs;
	xas->xa_index = index;
	return;
success:
	xas->xa_index = index;
	if (xas->xa_node)
		xas_set_offset(xas);
}

void xas_split(struct xa_state *xas, void *entry, unsigned int order)
{
	unsigned int sibs = (1 << (order % XA_CHUNK_SHIFT)) - 1;
	unsigned int offset, marks, canon;
	struct xa_node *node, *child;
	void *curr = xas_load(xas);
	int values = 0;

	node = xas->xa_node;
	if (xas_top(node))
		return;

	marks = node_get_marks(node, xas->xa_offset);
	offset = xas->xa_offset + sibs;
	do {
		if (xas->xa_shift < node->shift) {
			child = xas->xa_alloc;
                        xas->xa_alloc = child->parent;
			child->shift = node->shift - XA_CHUNK_SHIFT;
			child->offset = offset;
			child->count = XA_CHUNK_SIZE;
			child->nr_values = xa_is_value(entry) ?
					   XA_CHUNK_SIZE : 0;
			child->parent = node;
			node_set_marks(node, offset, child, marks);
			node->slots[offset] = xa_mk_node(child);
			if (xa_is_value(curr))
				values--;
			xas_update(xas, child);
		} else {
			canon = offset - xas->xa_sibs;
			node_set_marks(node, canon, NULL, marks);
			node->slots[canon] = entry;
			while (offset > canon)
				node->slots[offset--] = xa_mk_sibling(canon);
			values += (xa_is_value(entry) - xa_is_value(curr)) *
				  (xas->xa_sibs + 1);
		}
	} while (offset-- > xas->xa_offset);

	node->nr_values += values;
	xas_update(xas, node);
}

void xas_split_alloc(struct xa_state *xas, void *entry, unsigned int order)
{
	unsigned int sibs = (1 << (order % XA_CHUNK_SHIFT)) - 1;
	unsigned int mask = xas->xa_sibs;

	/* XXX: no support for splitting really large entries yet */
	if (xas->xa_shift + 2 * XA_CHUNK_SHIFT < order) {
		fprintf(stdout, "%s: %d + 2 * %d < %d\n",
			__func__, xas->xa_shift, XA_CHUNK_SHIFT, order);
		goto nomem;
	}

	if (xas->xa_shift + XA_CHUNK_SHIFT > order)
		return;

	do {
		unsigned int i;
		void *sibling = NULL;
		struct xa_node *node;

		node = malloc(sizeof(*node));
		if (!node)
			goto nomem;

		node->array = xas->xa;
		for (i = 0; i < XA_CHUNK_SIZE; i++) {
			if ((i & mask) == 0) {
				node->slots[i] = entry;
				sibling = xa_mk_sibling(i);
			} else {
				node->slots[i] = sibling;
			}
		}

		node->parent = xas->xa_alloc;
		xas->xa_alloc = node;
	} while (sibs-- > 0);

	return;
nomem:
	xas_destroy(xas);
	xas_set_err(xas, -ENOMEM);
}

static void xas_set_range(struct xa_state *xas,
			  unsigned long first,
			  unsigned long last)
{
	unsigned int shift = 0;
	unsigned long sibs = last - first;
	unsigned int offset = XA_CHUNK_MASK;

	xas_set(xas, first);

	while ((first & XA_CHUNK_MASK) == 0) {
		if (sibs < XA_CHUNK_MASK)
			break;
		if ((sibs == XA_CHUNK_MASK) && (offset < XA_CHUNK_MASK))
			break;
		shift += XA_CHUNK_SHIFT;
		if (offset == XA_CHUNK_MASK)
			offset = sibs & XA_CHUNK_MASK;
		sibs >>= XA_CHUNK_SHIFT;
		first >>= XA_CHUNK_SHIFT;
	}

	offset = first & XA_CHUNK_MASK;
	if (offset + sibs > XA_CHUNK_MASK)
		sibs = XA_CHUNK_MASK - offset;
	if ((((first + sibs + 1) << shift) - 1) > last)
		sibs -= 1;

	xas->xa_shift = shift;
	xas->xa_sibs = sibs;
}

void *xas_find(struct xa_state *xas, unsigned long max)
{
	void *entry;

	if (xas_error(xas) || xas->xa_node == XAS_BOUNDS)
		return NULL;
	if (xas->xa_index > max)
		return set_bounds(xas);

	if (!xas->xa_node) {
		xas->xa_index = 1;
		return set_bounds(xas);
        }

	if (xas->xa_node == XAS_RESTART) {
		entry = xas_load(xas);
		if (entry || xas_not_node(xas->xa_node))
			return entry;
        }

	if (!xas->xa_node->shift &&
	    xas->xa_offset != (xas->xa_index & XA_CHUNK_MASK))
                xas->xa_offset = ((xas->xa_index - 1) & XA_CHUNK_MASK) + 1;

	xas_next_offset(xas);
	while (xas->xa_node && (xas->xa_index <= max)) {
		if (xas->xa_offset == XA_CHUNK_SIZE) {
			xas->xa_offset = xas->xa_node->offset + 1;
			xas->xa_node = xa_parent(xas->xa, xas->xa_node);
			continue;
		}

		entry = xa_entry(xas->xa, xas->xa_node, xas->xa_offset);
		if (xa_is_node(entry)) {
			xas->xa_node = xa_to_node(entry);
			xas->xa_offset = 0;
			continue;
		}

		if (entry && !xa_is_sibling(entry))
			return entry;

		xas_next_offset(xas);
	}

	if (!xas->xa_node)
		xas->xa_node = XAS_BOUNDS;

	return NULL;
}

void *xas_find_conflict(struct xa_state *xas)
{
	struct xa_node *node;
	void *curr;

	if (xas_error(xas) || !xas->xa_node)
		return NULL;

	if (xas_top(xas->xa_node)) {
		curr = xas_start(xas);
		if (!curr)
			return NULL;

		while (xa_is_node(curr)) {
			node = xa_to_node(curr);
			curr = xas_descend(xas, node);
		}

		if (curr)
			return curr;
	}

	if (xas->xa_node->shift > xas->xa_shift)
		return NULL;

	for (;;) {
		if (xas->xa_node->shift == xas->xa_shift) {
			if ((xas->xa_offset & xas->xa_sibs) == xas->xa_sibs)
				break;
		} else if (xas->xa_offset == XA_CHUNK_MASK) {
			xas->xa_offset = xas->xa_node->offset;
			xas->xa_node = xa_parent(xas->xa, xas->xa_node);
			if (!xas->xa_node)
				break;
			continue;
		}

		curr = xa_entry(xas->xa, xas->xa_node, ++xas->xa_offset);
		if (xa_is_sibling(curr))
			continue;

		while (xa_is_node(curr)) {
			xas->xa_node = xa_to_node(curr);
			xas->xa_offset = 0;
			curr = xa_entry(xas->xa, xas->xa_node, 0);
		}

		if (curr)
			return curr;
	}

	xas->xa_offset -= xas->xa_sibs;
	return NULL;
}

void xas_init_marks(const struct xa_state *xas)
{
	xa_mark_t mark = 0;

	for (;;) {
		if (xa_track_free(xas->xa) && mark == XA_FREE_MARK)
			xas_set_mark(xas, mark);
		else
			xas_clear_mark(xas, mark);
		if (mark == XA_MARK_MAX)
			break;

		mark_inc(mark);
	}
}

bool xas_get_mark(const struct xa_state *xas, xa_mark_t mark)
{
	if (xas_invalid(xas))
		return false;

	if (!xas->xa_node)
		return xa_marked(xas->xa, mark);

	return node_get_mark(xas->xa_node, xas->xa_offset, mark);
}

void xas_set_mark(const struct xa_state *xas, xa_mark_t mark)
{
	struct xa_node *node = xas->xa_node;
	unsigned int offset = xas->xa_offset;

	if (xas_invalid(xas))
		return;

	while (node) {
		if (node_set_mark(node, offset, mark))
			return;

		offset = node->offset;
		node = xa_parent(xas->xa, node);
	}

	if (!xa_marked(xas->xa, mark))
		xa_mark_set(xas->xa, mark);
}

void xas_clear_mark(const struct xa_state *xas, xa_mark_t mark)
{
	struct xa_node *node = xas->xa_node;
	unsigned int offset = xas->xa_offset;

	if (xas_invalid(xas))
		return;

	while (node) {
		if (!node_clear_mark(node, offset, mark))
			return;

		if (node_any_mark(node, mark))
			return;

		offset = node->offset;
		node = xa_parent(xas->xa, node);
	}

	if (xa_marked(xas->xa, mark))
		xa_mark_clear(xas->xa, mark);
}

void *xas_find_marked(struct xa_state *xas, unsigned long max, xa_mark_t mark)
{
	bool advance = true;
	unsigned int offset;
	void *entry;

	if (xas_error(xas))
		return NULL;

	if (xas->xa_index > max)
		goto max;

	if (!xas->xa_node) {
		xas->xa_index = 1;
		goto out;
	}

	if (xas_top(xas->xa_node)) {
		advance = false;
		entry = xa_head(xas->xa);
		xas->xa_node = NULL;
		if (xas->xa_index > max_index(entry))
			goto out;

		if (!xa_is_node(entry)) {
			if (xa_marked(xas->xa, mark))
				return entry;
			xas->xa_index = 1;
			goto out;
		}

		xas->xa_node = xa_to_node(entry);
		xas->xa_offset = xas->xa_index >> xas->xa_node->shift;
	}

	while (xas->xa_index <= max) {
		if (xas->xa_offset == XA_CHUNK_SIZE) {
			xas->xa_offset = xas->xa_node->offset + 1;
			xas->xa_node = xa_parent(xas->xa, xas->xa_node);
			if (!xas->xa_node)
				break;
			advance = false;
			continue;
		}

		if (!advance) {
			entry = xa_entry(xas->xa, xas->xa_node, xas->xa_offset);
			if (xa_is_sibling(entry)) {
				xas->xa_offset = xa_to_sibling(entry);
				xas_move_index(xas, xas->xa_offset);
			}
		}

		offset = xas_find_chunk(xas, advance, mark);
		if (offset > xas->xa_offset) {
			advance = false;
			xas_move_index(xas, offset);
			/* Mind the wrap */
			if ((xas->xa_index - 1) >= max)
				goto max;

			xas->xa_offset = offset;
			if (offset == XA_CHUNK_SIZE)
				continue;
		}

		entry = xa_entry(xas->xa, xas->xa_node, xas->xa_offset);
		if (!entry && !(xa_track_free(xas->xa) && mark == XA_FREE_MARK))
			continue;

		if (!xa_is_node(entry))
			return entry;

		xas->xa_node = xa_to_node(entry);
		xas_set_offset(xas);
	}

out:
	if (xas->xa_index > max)
		goto max;
	return set_bounds(xas);
max:
	xas->xa_node = XAS_RESTART;
	return NULL;
}

/******************* XArray public APIs */

void xa_init(struct xarray *xa)
{
	sem_init(&xa->sem, 0, 1);
	xa->xa_head = NULL;
}

void *xa_load(struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	void *entry;

	xa_lock(xa);

	do {
		entry = xas_load(&xas);
		if (xa_is_zero(entry))
			entry = NULL;
	} while (xas_retry(&xas, entry));

	xa_unlock(xa);

	return entry;
}

void *xa_store(struct xarray *xa, unsigned long index, void *entry)
{
	XA_STATE(xas, xa, index);
	void *curr;

	if (xa_is_advanced(entry))
		return XA_ERROR(-EINVAL);

	if (xa_track_free(xa) && !entry)
		entry = XA_ZERO_ENTRY;

	xa_lock(xa);

	do {
		curr = xas_store(&xas, entry);
		if (xa_track_free(xa))
			xas_clear_mark(&xas, XA_FREE_MARK);
	} while (xas_nomem(&xas));

	xa_unlock(xa);

	return xas_result(&xas, curr);
}

void *xa_store_range(struct xarray *xa, unsigned long first,
		     unsigned long last, void *entry)
{
	XA_STATE(xas, xa, 0);
	unsigned int order;

	if (xa_is_internal(entry))
		return XA_ERROR(-EINVAL);
	if (last < first)
		return XA_ERROR(-EINVAL);

	do {
		xa_lock(xa);

		if (entry) {
			order = BITS_PER_LONG;
			if (last + 1)
				order = __ffs(last + 1);
			xas_set_order(&xas, last, order);
			xas_create(&xas, true);
			if (xas_error(&xas))
				goto unlock;
		}

		do {
			xas_set_range(&xas, first, last);
			xas_store(&xas, entry);
			if (xas_error(&xas))
				goto unlock;
			first += xas_size(&xas);
		} while (first <= last);
unlock:
		xa_unlock(xa);
	} while (xas_nomem(&xas));

	return xas_result(&xas, NULL);
}

void *xa_find(struct xarray *xa, unsigned long *indexp,
	      unsigned long max, xa_mark_t filter)
{
	XA_STATE(xas, xa, *indexp);
	void *entry;

	xa_lock(xa);

        do {
		if ((__force unsigned int)filter < XA_MAX_MARKS)
			entry = xas_find_marked(&xas, max, filter);
		else
			entry = xas_find(&xas, max);
	} while (xas_retry(&xas, entry));

	xa_unlock(xa);

	if (entry)
		*indexp = xas.xa_index;
	return entry;
}

void *xa_find_after(struct xarray *xa, unsigned long *indexp,
		    unsigned long max, xa_mark_t filter)
{
	XA_STATE(xas, xa, *indexp + 1);
	void *entry;

	if (xas.xa_index == 0)
		return NULL;

	xa_lock(xa);

	for (;;) {
		if ((__force unsigned int)filter < XA_MAX_MARKS)
			entry = xas_find_marked(&xas, max, filter);
		else
			entry = xas_find(&xas, max);

		if (xas_invalid(&xas))
			break;
		if (xas_is_sibling(&xas))
			continue;
		if (!xas_retry(&xas, entry))
			break;
	}

	xa_unlock(xa);

	if (entry)
		*indexp = xas.xa_index;
	return entry;
}

int xa_get_order(struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	void *entry;
	int order = 0;
	unsigned int slot;

	xa_lock(xa);

	entry = xas_load(&xas);
	if (!entry)
		goto unlock;

	if (!xas.xa_node)
		goto unlock;

	for (;;) {
		slot = xas.xa_offset + (1 << order);

		if (slot >= XA_CHUNK_SIZE)
			break;
		if (!xa_is_sibling(xas.xa_node->slots[slot]))
			break;
		order++;
	}

	order += xas.xa_node->shift;
unlock:
	xa_unlock(xa);

	return order;
}

void *xa_erase(struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	void *entry;

	sem_wait(&xa->sem);

	entry = xas_result(&xas, xas_store(&xas, NULL));

	sem_post(&xa->sem);

	return entry;
}

bool xa_get_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	XA_STATE(xas, xa, index);
	void *entry;

	xa_lock(xa);

	entry = xas_start(&xas);
	while (xas_get_mark(&xas, mark)) {
		if (!xa_is_node(entry)) {
			xa_unlock(xa);
			return true;
		}

		entry = xas_descend(&xas, xa_to_node(entry));
	}

	xa_unlock(xa);

        return false;
}

void xa_set_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	XA_STATE(xas, xa, index);
	void *entry;

        xa_lock(xa);

	entry = xas_load(&xas);
	if (entry)
		xas_set_mark(&xas, mark);

	xa_unlock(xa);
}

void xa_clear_mark(struct xarray *xa, unsigned long index, xa_mark_t mark)
{
	XA_STATE(xas, xa, index);
	void *entry;

	xa_lock(xa);

	entry = xas_load(&xas);
	if (entry)
		xas_clear_mark(&xas, mark);

	xa_unlock(xa);
}
