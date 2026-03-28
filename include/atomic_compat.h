// atomic_compat.h - 完整修复版本
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef _MSC_VER
#include <intrin.h>
#include <windows.h>

// ===== 基础类型映射 =====
#define _Atomic(T) volatile T
#define atomic_int _Atomic(int)
#define atomic_long _Atomic(long)
#define atomic_char _Atomic(char)
#define atomic_bool _Atomic(int)

// ===== 修复：添加缺失的原子类型定义 =====
#define atomic_size_t _Atomic(size_t)
#define atomic_uintptr_t _Atomic(uintptr_t)
#define atomic_intptr_t _Atomic(intptr_t)

// 64 位支持
#define atomic_long_long _Atomic(long long)
#define atomic_int64_t _Atomic(int64_t)
#define atomic_uint64_t _Atomic(uint64_t)

// ===== 内存序宏 =====
#define memory_order_relaxed 0
#define memory_order_acquire 1
#define memory_order_release 2
#define memory_order_acq_rel 3
#define memory_order_seq_cst 4

// ===== 辅助宏：根据大小选择正确的 Interlocked 函数 =====
#define _ATOMIC_PTR_CAST(ptr) ((LONG*)(ptr))
#define _ATOMIC_VAL_CAST(val) ((LONG)(val))

// ===== 原子操作宏 (MSVC) =====
#define atomic_load_explicit(ptr, order) \
    ((order) == memory_order_acquire ? (_ReadBarrier(), *(ptr)) : *(ptr))

#define atomic_store_explicit(ptr, val, order) \
    do { if ((order) == memory_order_release) _WriteBarrier(); *(ptr) = (val); } while(0)

#define atomic_compare_exchange_weak_explicit(ptr, expected, desired, succ, fail) \
    (_InterlockedCompareExchange(_ATOMIC_PTR_CAST(ptr), _ATOMIC_VAL_CAST(desired), *(expected)) == *(expected))

#define atomic_fetch_add_explicit(ptr, val, order) \
    (_InterlockedExchangeAdd(_ATOMIC_PTR_CAST(ptr), _ATOMIC_VAL_CAST(val)))

#define atomic_fetch_sub_explicit(ptr, val, order) \
    (_InterlockedExchangeAdd(_ATOMIC_PTR_CAST(ptr), -_ATOMIC_VAL_CAST(val)))

#define atomic_thread_fence(order) \
    _MemoryBarrier()

// ===== 64 位原子操作 (用于 size_t 在 x64 平台) =====
#ifdef _WIN64
#define atomic_load_64(ptr) (_InterlockedCompareExchange64((LONGLONG*)(ptr), 0, 0))
#define atomic_store_64(ptr, val) (_InterlockedExchange64((LONGLONG*)(ptr), (LONGLONG)(val)))
#define atomic_fetch_add_64(ptr, val) (_InterlockedExchangeAdd64((LONGLONG*)(ptr), (LONGLONG)(val)))
#endif

#else
// ===== 非 MSVC 编译器使用标准 C11 =====
#include <stdatomic.h>
#endif