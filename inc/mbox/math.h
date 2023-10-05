/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Math functions
 *
 * Author: Gavin Shan <shan.gavin@gmail.com>
 */

#ifndef __MBOX_MATH_H
#define __MBOX_MATH_H

#include <mbox/base.h>

/* Find first bit in word */
static __always_inline unsigned long __ffs(unsigned long word)
{
	int num = 0;

	if ((word & 0xffffffff) == 0) {
		num += 32;
		word >>= 32;
	}

	if ((word & 0xffff) == 0) {
		num += 16;
		word >>= 16;
	}

	if ((word & 0xff) == 0) {
		num += 8;
		word >>= 8;
	}

	if ((word & 0xf) == 0) {
		num += 4;
		word >>= 4;
	}

	if ((word & 0x3) == 0) {
		num += 2;
		word >>= 2;
	}

	if ((word & 0x1) == 0)
		num += 1;

	return num;
}

#endif /* __MBOX_MATH_H */

