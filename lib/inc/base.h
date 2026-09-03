/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Base functions
 *
 * Author: Gavin Shan <shan.gavin@gmail.com>
 */

#ifndef __MBOX_BASE_H
#define __MBOX_BASE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;
typedef int64_t   s64;

typedef struct {
	int counter;
} atomic_t;

typedef struct {
	long counter;
} atomic64_t;

/* compiler */
// #define __bitwise		__attribute__((bitwise))
#define __bitwise
//#define __force		__attribute__((force))
#define __force

#define BITS_PER_LONG		64
#define __stringify(x)		#x
#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))

/* Alignment */
#define IS_ALIGNED(x, a)	(((x) & ((typeof(x))(a) - 1)) == 0)
#define ALIGN_DOWN(x, a)	((x) & ~((typeof(x))(a) - 1))
#define ALIGN_UP(x, a)		(((x) + (typeof(x))(a) - 1) & ~((typeof(x))(a) - 1))
#define PTR_ALIGN_DOWN(p, a)	((typeof(p))ALIGN_DOWN((unsigned long)(p), (a)))
#define PTR_ALIGN_UP(p, a)	((typeof(p))ALIGN_UP((unsigned long)(p), (a)))

/* container */
#define typeof_member(type, member)	typeof(((type *)0)->member)
#define sizeof_field(type, member)	sizeof(((type *)0)->member)
#define offsetof(type, member)		((size_t)(&(((type *)0)->member)))
#define offsetofend(type, member)	\
	(offsetof(type, member) + typeof_member(type, member))
#define container_of(ptr, type, member)	({		\
	void *__mptr = (void *)(ptr);			\
	((type *)(__mptr - offsetof(type, member))); })

/* Read/write once */
#define READ_ONCE(x)		(*((const volatile typeof(x) *)&(x)))
#define WRITE_ONCE(x, val)	*((volatile typeof(x) *)&(x)) = (val)

#endif /* __MBOX_BASE_H */
