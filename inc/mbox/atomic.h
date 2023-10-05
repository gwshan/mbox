/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Atomic operations
 *
 * Author: Gavin Shan <shan.gavin@gmail.com>
 */

#ifndef __MBOX_ATOMIC_H
#define __MBOX_ATOMIC_H

#include <mbox/base.h>
#include <asm/arm64/atomic.h>

static __always_inline int
atomic_read(const atomic_t *v)
{
	return arch_atomic_read(v);
}

/* GAVIN: barrier needs to be in position */

#endif /* __MBOX_LIST_H */
