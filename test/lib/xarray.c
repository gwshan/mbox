/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * eXtensible Array
 */

#include <mbox/base.h>
#include <mbox/xarray.h>

static void dump_one_node(struct xa_node *node, int level)
{
	int offset;

	fprintf(stdout, "---------- node (%d) ----------\n", level);
	fprintf(stdout, "\n");
	fprintf(stdout, "shift:            %d\n",    node->shift);
	fprintf(stdout, "offset:           %d\n",    node->offset);
	fprintf(stdout, "count:            %d\n",    node->count);
	fprintf(stdout, "nr_values:        %d\n",    node->nr_values);
	fprintf(stdout, "parent:           0x%lx\n", (uint64_t)(node->parent));
	fprintf(stdout, "array:            0x%lx\n", (uint64_t)(node->array));
	for (offset = 0; offset < XA_CHUNK_SIZE; offset++) {
		if (!node->slots[offset])
			continue;

		fprintf(stdout, "slots[%02d]:        0x%lx\n",
			offset, (uint64_t)(node->slots[offset]));
	}
	fprintf(stdout, "\n");
}

static void dump_subordinate_nodes(struct xa_node *node, int level)
{
	struct xa_node *n;
	int offset;

	dump_one_node(node, level);

	for (offset = 0; offset <= XA_CHUNK_SIZE; offset++) {
		if (!xa_is_node(node->slots[offset]))
			continue;

		n = xa_to_node(node->slots[offset]);
		dump_subordinate_nodes(n, level + 1);
	}
}

static void dump(struct xarray *xa)
{
	struct xa_node *node;
	int offset, level = 0;

	fprintf(stdout, "---------- xarray ----------\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "xa_flags:         0x%lx\n", xa->xa_flags);
	fprintf(stdout, "xa_head:          0x%lx\n", (uint64_t)(xa->xa_head));
	fprintf(stdout, "\n");

	if (!xa_is_node(xa->xa_head))
		return;

	node = xa_to_node(xa->xa_head);
	dump_subordinate_nodes(node, 0);
}

static void add_entry(struct xarray *xa, unsigned long index,
		      unsigned int order, void *entry)
{
	XA_STATE(xas, xa, index);

	xas_set_order(&xas, index, order);

	xas_store(&xas, entry);
}

static void split_and_add_entry(struct xarray *xa, unsigned long index,
				unsigned int order, void *new_entry)
{
	XA_STATE(xas, xa, index);
	void *entry;

	xas_set_order(&xas, index, order);

	do {
		unsigned int old_order = xa_get_order(xa, index);
		void *enry, *old = NULL;

		if (old_order > order)
			xas_split_alloc(&xas, xa_load(xa, index), old_order);

		xas_for_each_conflict(&xas, entry) {
			old = entry;
#if 0
			if (!xa_is_value(entry)) {
				xas_set_err(&xas, -EEXIST);
				goto next;
			}
#endif
		}

		if (old) {
			old_order = xa_get_order(xas.xa, xas.xa_index);
			if (old_order > order) {
				xas_split(&xas, old, order);
				xas_reset(&xas);
			}
		}

		xas_store(&xas, new_entry);
	} while (xas_nomem(&xas));	
}

void test_lib_xarray(void)
{
	struct xarray xa;
	void *value = (void *)0x00ffff00;

	xa_init(&xa);
	xa_store(&xa,  0, value);
	xa_store(&xa,  1, value);
	xa_store(&xa,  2, value);
	xa_store(&xa,  3, value);
	xa_store(&xa, 16, value);
	dump(&xa);

	xa_erase(&xa, 16);
	dump(&xa);

	// add_entry(&xa, 0x000, 9, (void *)&values[0]);
	// add_entry(&xa, 0x200, 9, (void *)&values[1]);
	// dump(&xa);

	// split_and_add_entry(&xa, 0x200, 8, (void *)&values[2]);
	// split_and_add_entry(&xa, 0x200, 1, (void *)&values[2]);
	// split_and_add_entry(&xa, 0x200, 0, (void *)&values[2]);
	// dump(&xa);
}
