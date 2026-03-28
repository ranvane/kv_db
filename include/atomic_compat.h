// atomic_compat.h
#pragma once

#ifdef _MSC_VER
#include <intrin.h>
#include <windows.h>

// 类型映射
#define _Atomic(T) volatile T
#define atomic_int _Atomic(int)
#define atomic_long _Atomic(long)

// 内存序宏模拟
#define memory_order_relaxed 0
#define memory_order_acquire 1
#define memory_order_release 2
#define memory_order_seq_cst 3

// 操作映射示例
#define atomic_load_explicit(ptr, order) \
    ((order) == memory_order_acquire ? (_ReadBarrier(), *(ptr)) : *(ptr))

#define atomic_store_explicit(ptr, val, order) \
    do { if ((order) == memory_order_release) _WriteBarrier(); *(ptr) = (val); } while(0)

#define atomic_compare_exchange_weak_explicit(ptr, expected, desired, succ, fail) \
    (_InterlockedCompareExchange((LONG*)(ptr), (LONG)(desired), *(expected)) == *(expected))

#define atomic_fetch_add_explicit(ptr, val, order) \
    (_InterlockedExchangeAdd((LONG*)(ptr), (LONG)(val)))

#define atomic_thread_fence(order) \
    _MemoryBarrier()

#else
#include <stdatomic.h>
#endif