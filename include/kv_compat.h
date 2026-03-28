#ifndef KV_COMPAT_H
#define KV_COMPAT_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <io.h>
#include <direct.h>
#include <stdio.h>
#include <BaseTsd.h>

typedef SSIZE_T ssize_t;

/* ========================
 * 锁
 * ======================== */

typedef SRWLOCK kv_rwlock_t;
#define kv_rwlock_init(l) InitializeSRWLock(l)
#define kv_rwlock_rdlock(l) AcquireSRWLockShared(l)
#define kv_rwlock_wrlock(l) AcquireSRWLockExclusive(l)
#define kv_rwlock_unlock_rd(l) ReleaseSRWLockShared(l)
#define kv_rwlock_unlock_wr(l) ReleaseSRWLockExclusive(l)
#define kv_rwlock_destroy(l) ((void)0)

typedef CRITICAL_SECTION kv_mutex_t;
#define kv_mutex_init(m) InitializeCriticalSection(m)
#define kv_mutex_lock(m) EnterCriticalSection(m)
#define kv_mutex_unlock(m) LeaveCriticalSection(m)
#define kv_mutex_destroy(m) DeleteCriticalSection(m)

/* ========================
 * 原子操作（关键）
 * ======================== */

typedef LONG kv_atomic_int;

#define kv_atomic_load(p) InterlockedCompareExchange(p, 0, 0)
#define kv_atomic_store(p, v) InterlockedExchange(p, v)
#define kv_atomic_inc(p) InterlockedIncrement(p)
#define kv_atomic_dec(p) InterlockedDecrement(p)

/* ========================
 * 文件 IO
 * ======================== */

static inline ssize_t kv_pread(int fd, void *buf, size_t count, long long offset) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    OVERLAPPED ov;
    ZeroMemory(&ov, sizeof(ov));

    ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
    ov.OffsetHigh = (DWORD)(offset >> 32);

    DWORD read = 0;

    BOOL ok = ReadFile(h, buf, (DWORD)count, &read, &ov);

    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_HANDLE_EOF) {
            return -1;
        }
    }

    return (ssize_t)read;
}

static inline ssize_t kv_pwrite(int fd, const void *buf, size_t count, long long offset) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    OVERLAPPED ov;
    ZeroMemory(&ov, sizeof(ov));

    ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
    ov.OffsetHigh = (DWORD)(offset >> 32);

    DWORD written = 0;

    BOOL ok = WriteFile(h, buf, (DWORD)count, &written, &ov);

    if (!ok) {
        return -1;
    }

    return (ssize_t)written;
}

/* ========================
 * 文件系统
 * ======================== */

#define kv_mkdir(p, m) _mkdir(p)
#define kv_fileno _fileno
#define kv_fsync _commit

/* ========================
 * 其他
 * ======================== */

#define kv_sleep_ms(ms) Sleep(ms)

#else  /* POSIX ========================= */

#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdatomic.h>

/* ========================
 * 锁
 * ======================== */

typedef pthread_rwlock_t kv_rwlock_t;
#define kv_rwlock_init(l) pthread_rwlock_init(l, NULL)
#define kv_rwlock_rdlock(l) pthread_rwlock_rdlock(l)
#define kv_rwlock_wrlock(l) pthread_rwlock_wrlock(l)
#define kv_rwlock_unlock_rd(l) pthread_rwlock_unlock(l)
#define kv_rwlock_unlock_wr(l) pthread_rwlock_unlock(l)
#define kv_rwlock_destroy(l) pthread_rwlock_destroy(l)

typedef pthread_mutex_t kv_mutex_t;
#define kv_mutex_init(m) pthread_mutex_init(m, NULL)
#define kv_mutex_lock(m) pthread_mutex_lock(m)
#define kv_mutex_unlock(m) pthread_mutex_unlock(m)
#define kv_mutex_destroy(m) pthread_mutex_destroy(m)

/* ========================
 * 原子操作（POSIX）
 * ======================== */

typedef _Atomic int kv_atomic_int;

#define kv_atomic_load(p) atomic_load(p)
#define kv_atomic_store(p, v) atomic_store(p, v)
#define kv_atomic_inc(p) atomic_fetch_add(p, 1)
#define kv_atomic_dec(p) atomic_fetch_sub(p, 1)

/* ========================
 * 文件 IO
 * ======================== */

#define kv_pread pread
#define kv_pwrite pwrite

/* ========================
 * 文件系统
 * ======================== */

#define kv_mkdir mkdir
#define kv_fileno fileno
#define kv_fsync fsync

/* ========================
 * 其他
 * ======================== */

#define kv_sleep_ms(ms) usleep((ms) * 1000)

#endif

#endif /* KV_COMPAT_H */