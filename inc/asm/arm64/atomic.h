/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * ARM64 atomic operations. When LSE (Large System Extension) is supported,
 * the extended instructions like 'stadd' can be used to support the atomic
 * operations. Otherwise, the target memory needs to be reserved and released
 * explicitly by 'ldxr', 'stxr' and their variants to support the atomic
 * operations. Here the later case is only supported since we're not aware
 * if LSE is supported from the hardware level.
 *
 * Author: Gavin Shan <shan.gavin@gmail.com>
 */

#ifndef __MBOX_ARM64_ATOMIC_H
#define __MBOX_ARM64_ATOMIC_H

#include <mbox/base.h>

#define ATOMIC_OP(op, asm_op, constraint)				\
static __always_inline void						\
arch_atomic_##op(atomic_t *v, int i)					\
{									\
	unsigned long tmp;						\
	int result;							\
									\
	asm volatile("// arch_atomic_" #op "\n"				\
	"	prfm	pstl1strm, %2\n"				\
        "1:	ldxr	%w0, %2\n"					\
	"	" #asm_op "	%w0, %w0, %w3\n"			\
	"	stxr	%w1, %w0, %2\n"					\
	"	cbnz	%w1, 1b\n"					\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: __stringify(constraint) "r" (i));				\
}

#define ATOMIC_OP_RETURN(op, asm_op, constraint, name, mb, acq, rel, cl)\
static __always_inline int						\
arch_atomic_##op##_return##name(atomic_t *v, int i)			\
{									\
	unsigned long tmp;						\
	int result;							\
									\
	asm volatile("// arch_atomic_" #op "_return" #name "\n"		\
	"	prfm    pstl1strm, %2\n"				\
	"1:	ld" #acq "xr	%w0, %2\n"				\
	"	" #asm_op "	%w0, %w0, %w3\n"			\
	"	st" #rel "xr	%w1, %w0, %2\n"				\
	"	cbnz    %w1, 1b\n"					\
	"	" #mb							\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: __stringify(constraint) "r" (i)				\
	: cl);								\
									\
	return result;							\
}

#define ATOMIC_FETCH_OP(op, asm_op, constraint, name, mb, acq, rel, cl)	\
static __always_inline int						\
arch_atomic_fetch_##op##name(atomic_t *v, int i)			\
{									\
	unsigned long tmp;						\
	int val, result;						\
									\
	asm volatile("// arch_atomic_fetch_" #op #name "\n"		\
	"	prfm	pstl1strm, %3\n"				\
	"1:	ld" #acq "xr	%w0, %3\n"				\
	"	" #asm_op "	%w1, %w0, %w4\n"			\
	"	st" #rel "xr	%w2, %w1, %3\n"				\
	"	cbnz	%w2, 1b\n"					\
	"	" #mb							\
	: "=&r" (result), "=&r" (val), "=&r" (tmp), "+Q" (v->counter)	\
	: __stringify(constraint) "r" (i)				\
	: cl);								\
									\
	return result;							\
}

#define arch_atomic_read(v)		READ_ONCE((v)->counter)
#define arch_atomic_set(v, i)		WRITE_ONCE(((v)->counter), (i))
ATOMIC_OP       (   add, add, I)
ATOMIC_OP_RETURN(   add, add, I,         , dmb ish,  , l, "memory")
ATOMIC_OP_RETURN(   add, add, I, _relaxed,        ,  ,  ,         )
ATOMIC_OP_RETURN(   add, add, I, _acquire,        , a,  , "memory")
ATOMIC_OP_RETURN(   add, add, I, _release,        ,  , l, "memory")
ATOMIC_FETCH_OP (   add, add, I,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (   add, add, I, _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (   add, add, I, _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (   add, add, I, _release,        ,  , l, "memory")
ATOMIC_OP       (   sub, sub, J)
ATOMIC_OP_RETURN(   sub, sub, J,         , dmb ish,  , l, "memory")
ATOMIC_OP_RETURN(   sub, sub, J, _relaxed,        ,  ,  ,         )
ATOMIC_OP_RETURN(   sub, sub, J, _acquire,        , a,  , "memory")
ATOMIC_OP_RETURN(   sub, sub, J, _release,        ,  , l, "memory")
ATOMIC_FETCH_OP (   sub, sub, J,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (   sub, sub, J, _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (   sub, sub, J, _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (   sub, sub, J, _release,        ,  , l, "memory")
ATOMIC_OP       (   and, and,  )
ATOMIC_FETCH_OP (   and, and,  ,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (   and, and,  , _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (   and, and,  , _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (   and, and,  , _release,        ,  , l, "memory")
ATOMIC_OP       (    or, orr,  )
ATOMIC_FETCH_OP (    or, orr,  ,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (    or, orr,  , _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (    or, orr,  , _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (    or, orr,  , _release,        ,  , l, "memory")
ATOMIC_OP       (   xor, err,  )
ATOMIC_FETCH_OP (   xor, err,  ,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (   xor, err,  , _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (   xor, err,  , _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (   xor, err,  , _release,        ,  , l, "memory")
ATOMIC_OP       (andnot, bic,  )
ATOMIC_FETCH_OP (andnot, bic,  ,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (andnot, bic,  , _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (andnot, bic,  , _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (andnot, bic,  , _release,        ,  , l, "memory")
#undef ATOMIC_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_FETCH_OP

#define ATOMIC_OP(op, asm_op, constraint)				\
static __always_inline void						\
arch_atomic64_##op(atomic64_t *v, long i)				\
{									\
	long result;							\
	unsigned long tmp;						\
									\
	asm volatile("// arch_atomic64_" #op "\n"			\
	"	prfm	pstl1strm, %2\n"				\
	"1:	ldxr	%0, %2\n"					\
	"	" #asm_op "	%0, %0, %3\n"				\
	"	stxr	%w1, %0, %2\n"					\
	"	cbnz	%w1, 1b"					\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: __stringify(constraint) "r" (i));				\
}

#define ATOMIC_OP_RETURN(op, asm_op, constraint, name, mb, acq, rel, cl)\
static __always_inline long						\
arch_atomic64_##op##_return##name(atomic64_t *v, long i)		\
{									\
	long result;							\
	unsigned long tmp;						\
									\
	asm volatile("// arch_atomic64_" #op "_return" #name "\n"	\
	"	prfm	pstl1strm, %2\n"				\
	"1:	ld" #acq "xr	%0, %2\n"				\
	"	" #asm_op "	%0, %0, %3\n"				\
	"	st" #rel "xr	%w1, %0, %2\n"				\
	"	cbnz	%w1, 1b\n"					\
	"	" #mb							\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: __stringify(constraint) "r" (i)				\
	: cl);								\
									\
	return result;							\
}

#define ATOMIC_FETCH_OP(op, asm_op, constraint, name, mb, acq, rel, cl)	\
static __always_inline long						\
arch_atomic64_fetch_##op##name(atomic64_t *v, long i)			\
{									\
	long result, val;						\
	unsigned long tmp;						\
									\
	asm volatile("// arch_atomic64_fetch_" #op #name "\n"		\
	"	prfm    pstl1strm, %3\n"				\
	"1:	ld" #acq "xr	%0, %3\n"				\
	"	" #asm_op "	%1, %0, %4\n"				\
	"	st" #rel "xr	%w2, %1, %3\n"				\
	"	cbnz	%w2, 1b\n"					\
	"	" #mb							\
	: "=&r" (result), "=&r" (val), "=&r" (tmp), "+Q" (v->counter)	\
	: __stringify(constraint) "r" (i)				\
	: cl);								\
									\
	return result;							\
}

#define arch_atomic64_read(v)		READ_ONCE((v)->counter)
#define arch_atomic64_set(v, i)		WRITE_ONCE(((v)->counter), (i))
ATOMIC_OP       (   add, add, I)
ATOMIC_OP_RETURN(   add, add, I,         , dmb ish,  , l, "memory")
ATOMIC_OP_RETURN(   add, add, I, _relaxed,        ,  ,  ,         )
ATOMIC_OP_RETURN(   add, add, I, _acquire,        , a,  , "memory")
ATOMIC_OP_RETURN(   add, add, I, _release,        ,  , l, "memory")
ATOMIC_FETCH_OP (   add, add, I,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (   add, add, I, _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (   add, add, I, _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (   add, add, I, _release,        ,  , l, "memory")
ATOMIC_OP       (   sub, sub, J)
ATOMIC_OP_RETURN(   sub, sub, J,         , dmb ish,  , l, "memory")
ATOMIC_OP_RETURN(   sub, sub, J, _relaxed,        ,  ,  ,         )
ATOMIC_OP_RETURN(   sub, sub, J, _acquire,        , a,  , "memory")
ATOMIC_OP_RETURN(   sub, sub, J, _release,        ,  , l, "memory")
ATOMIC_FETCH_OP (   sub, sub, J,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (   sub, sub, J, _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (   sub, sub, J, _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (   sub, sub, J, _release,        ,  , l, "memory")
ATOMIC_OP       (   and, and, L)
ATOMIC_FETCH_OP (   and, and, L,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (   and, and, L, _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (   and, and, L, _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (   and, and, L, _release,        ,  , l, "memory")
ATOMIC_OP       (    or, orr, L)
ATOMIC_FETCH_OP (    or, orr, L,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (    or, orr, L, _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (    or, orr, L, _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (    or, orr, L, _release,        ,  , l, "memory")
ATOMIC_OP       (   xor, err, L)
ATOMIC_FETCH_OP (   xor, err, L,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (   xor, err, L, _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (   xor, err, L, _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (   xor, err, L, _release,        ,  , l, "memory")
ATOMIC_OP       (andnot, bic,  )
ATOMIC_FETCH_OP (andnot, bic,  ,         , dmb ish,  , l, "memory")
ATOMIC_FETCH_OP (andnot, bic,  , _relaxed,        ,  ,  ,         )
ATOMIC_FETCH_OP (andnot, bic,  , _acquire,        , a,  , "memory")
ATOMIC_FETCH_OP (andnot, bic,  , _release,        ,  , l, "memory")
#undef ATOMIC_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_FETCH_OP

static __always_inline long
arch_atomic64_dec_if_positive(atomic64_t *v)
{
	long result;
	unsigned long tmp;

	asm volatile("// arch_atomic64_dec_if_positive\n"
	"	prfm	pstl1strm, %2\n"
	"1:	ldxr	%0, %2\n"
	"	subs	%0, %0, #1\n"
	"	b.lt	2f\n"
	"	stlxr	%w1, %0, %2\n"
	"	cbnz	%w1, 1b\n"
	"	dmb	ish\n"
	"2:"
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)
	:
	: "cc", "memory");

	return result;
}

#define XCHG_CASE(w, sfx, name, sz, mb, nop_lse, acq, acq_lse, rel, cl)	\
static __always_inline u##sz						\
arch_xchg_case_##name##sz(u##sz x, volatile void *ptr)			\
{									\
	u##sz ret;							\
	unsigned long tmp;						\
									\
	asm volatile("// arch_xchg_case_" #name #sz "\n"		\
	"	prfm	pstl1strm, %2\n"				\
	"1:	ld" #acq "xr" #sfx "\t%" #w "0, %2\n"			\
	"	st" #rel "xr" #sfx "\t%w1, %" #w "3, %2\n"		\
	"	cbnz	%w1, 1b\n"					\
	"	" #mb							\
	: "=&r" (ret), "=&r" (tmp), "+Q" (*(u##sz *)ptr)		\
	: "r" (x)							\
	: cl);								\
									\
	return ret;							\
}

#define XCHG_GEN(sfx)							\
static __always_inline unsigned long					\
arch_xchg##sfx(unsigned long x, volatile void *ptr, int size)		\
{									\
        switch (size) {							\
        case 1:								\
                return arch_xchg_case##sfx##_8(x, ptr);			\
        case 2:								\
                return arch_xchg_case##sfx##_16(x, ptr);		\
        case 4:								\
                return arch_xchg_case##sfx##_32(x, ptr);		\
        case 8:								\
                return arch_xchg_case##sfx##_64(x, ptr);		\
        }								\
}

#define xchg_wrapper(sfx, ptr, x)	({				\
	__typeof__(*(ptr)) __ret;					\
	__ret = (__typeof__(*(ptr)))					\
	    arch_xchg##sfx((unsigned long)(x), (ptr), sizeof(*(ptr)));	\
	__ret;								\
})

XCHG_CASE(w, b,     ,  8,        ,    ,  ,  ,  ,         )
XCHG_CASE(w, h,     , 16,        ,    ,  ,  ,  ,         )
XCHG_CASE(w,  ,     , 32,        ,    ,  ,  ,  ,         )
XCHG_CASE( ,  ,     , 64,        ,    ,  ,  ,  ,         )
XCHG_CASE(w, b, acq_,  8,        ,    , a, a,  , "memory")
XCHG_CASE(w, h, acq_, 16,        ,    , a, a,  , "memory")
XCHG_CASE(w,  , acq_, 32,        ,    , a, a,  , "memory")
XCHG_CASE( ,  , acq_, 64,        ,    , a, a,  , "memory")
XCHG_CASE(w, b, rel_,  8,        ,    ,  ,  , l, "memory")
XCHG_CASE(w, h, rel_, 16,        ,    ,  ,  , l, "memory")
XCHG_CASE(w,  , rel_, 32,        ,    ,  ,  , l, "memory")
XCHG_CASE( ,  , rel_, 64,        ,    ,  ,  , l, "memory")
XCHG_CASE(w, b,  mb_,  8, dmb ish, nop,  , a, l, "memory")
XCHG_CASE(w, h,  mb_, 16, dmb ish, nop,  , a, l, "memory")
XCHG_CASE(w,  ,  mb_, 32, dmb ish, nop,  , a, l, "memory")
XCHG_CASE( ,  ,  mb_, 64, dmb ish, nop,  , a, l, "memory")
XCHG_GEN()
XCHG_GEN(_acq)
XCHG_GEN(_rel)
XCHG_GEN(_mb)
#undef XCHG_CASE
#undef XCHG_GEN

#define arch_xchg_relaxed(...)	xchg_wrapper(    , __VA_ARGS__)
#define arch_xchg_acquire(...)	xchg_wrapper(_acq, __VA_ARGS__)
#define arch_xchg_release(...)	xchg_wrapper(_rel, __VA_ARGS__)
#define arch_xchg(...)		xchg_wrapper( _mb, __VA_ARGS__)

#define CMPXCHG_CASE(w, sfx, name, sz, mb, acq, rel, cl, constraint)	\
static __always_inline u##sz						\
arch_cmpxchg_case_##name##sz(volatile void *ptr,			\
			     unsigned long old, u##sz new)		\
{									\
	unsigned long tmp;						\
	u##sz oldval;							\
									\
	/*								\
	 * Sub-word sizes require explicit casting so that the compare	\
	 * part of the cmpxchg doesn't end up interpreting non-zero	\
	 * upper bits of the register containing "old".			\
	 */								\
	if (sz < 32)							\
		old = (u##sz)old;					\
									\
	asm volatile("// arch_cmpxchg_case_" #name #sz "\n"		\
	"       prfm    pstl1strm, %[v]\n"				\
	"1:     ld" #acq "xr" #sfx "\t%" #w "[oldval], %[v]\n"		\
	"       eor     %" #w "[tmp], %" #w "[oldval], %" #w "[old]\n"	\
	"       cbnz    %" #w "[tmp], 2f\n"				\
	"       st" #rel "xr" #sfx "\t%w[tmp], %" #w "[new], %[v]\n"	\
	"       cbnz    %w[tmp], 1b\n"					\
	"       " #mb "\n"						\
	"2:"								\
	: [tmp] "=&r" (tmp), [oldval] "=&r" (oldval),			\
	  [v] "+Q" (*(u##sz *)ptr)					\
	: [old] __stringify(constraint) "r" (old), [new] "r" (new)	\
	: cl);								\
									\
	return oldval;							\
}

#define CMPXCHG_GEN(sfx)						\
static __always_inline unsigned long					\
arch_cmpxchg##sfx(volatile void *ptr, unsigned long old,		\
		   unsigned long new, int size)				\
{									\
	switch (size) {							\
	case 1:								\
		return arch_cmpxchg_case##sfx##_8(ptr, old, new);	\
	case 2:								\
		return arch_cmpxchg_case##sfx##_16(ptr, old, new);	\
	case 4:								\
		return arch_cmpxchg_case##sfx##_32(ptr, old, new);	\
	case 8:								\
		return arch_cmpxchg_case##sfx##_64(ptr, old, new);	\
	}								\
}

#define cmpxchg_wrapper(sfx, ptr, o, n) ({				\
	__typeof__(*(ptr)) __ret;					\
	__ret = (__typeof__(*(ptr)))					\
		arch_cmpxchg##sfx((ptr), (unsigned long)(o),		\
				  (unsigned long)(n), sizeof(*(ptr)));	\
	__ret;								\
})

CMPXCHG_CASE(w, b,     ,  8,        ,  ,  ,         ,  )
CMPXCHG_CASE(w, h,     , 16,        ,  ,  ,         ,  )
CMPXCHG_CASE(w,  ,     , 32,        ,  ,  ,         ,  )
CMPXCHG_CASE( ,  ,     , 64,        ,  ,  ,         , L)
CMPXCHG_CASE(w, b, acq_,  8,        , a,  , "memory",  )
CMPXCHG_CASE(w, h, acq_, 16,        , a,  , "memory",  )
CMPXCHG_CASE(w,  , acq_, 32,        , a,  , "memory",  )
CMPXCHG_CASE( ,  , acq_, 64,        , a,  , "memory", L)
CMPXCHG_CASE(w, b, rel_,  8,        ,  , l, "memory",  )
CMPXCHG_CASE(w, h, rel_, 16,        ,  , l, "memory",  )
CMPXCHG_CASE(w,  , rel_, 32,        ,  , l, "memory",  )
CMPXCHG_CASE( ,  , rel_, 64,        ,  , l, "memory", L)
CMPXCHG_CASE(w, b,  mb_,  8, dmb ish,  , l, "memory",  )
CMPXCHG_CASE(w, h,  mb_, 16, dmb ish,  , l, "memory",  )
CMPXCHG_CASE(w,  ,  mb_, 32, dmb ish,  , l, "memory",  )
CMPXCHG_CASE( ,  ,  mb_, 64, dmb ish,  , l, "memory", L)
CMPXCHG_GEN()
CMPXCHG_GEN(_acq)
CMPXCHG_GEN(_rel)
CMPXCHG_GEN(_mb)
#undef CMPXCHG_CASE
#undef CMPXCHG_GEN

#define arch_cmpxchg_relaxed(...)	cmpxchg_wrapper(    , __VA_ARGS__)
#define arch_cmpxchg_acquire(...)	cmpxchg_wrapper(_acq, __VA_ARGS__)
#define arch_cmpxchg_release(...)	cmpxchg_wrapper(_rel, __VA_ARGS__)
#define arch_cmpxchg(...)		cmpxchg_wrapper( _mb, __VA_ARGS__)
#define arch_cmpxchg_local		arch_cmpxchg_relaxed

#endif /* __MBOX_ARM64_ATOMIC_H */
