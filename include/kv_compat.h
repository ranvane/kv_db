#ifndef KV_COMPAT_H
#define KV_COMPAT_H

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #include <stdio.h>

    typedef long long ssize_t;

    typedef SRWLOCK kv_rwlock_t;
    #define kv_rwlock_init(l) InitializeSRWLock(l)
    #define kv_rwlock_rdlock(l) AcquireSRWLockShared(l)
    #define kv_rwlock_wrlock(l) AcquireSRWLockExclusive(l)
    #define kv_rwlock_unlock_rd(l) ReleaseSRWLockShared(l)
    #define kv_rwlock_unlock_wr(l) ReleaseSRWLockExclusive(l)
    #define kv_rwlock_destroy(l)

    typedef CRITICAL_SECTION kv_mutex_t;
    #define kv_mutex_init(m) InitializeCriticalSection(m)
    #define kv_mutex_lock(m) EnterCriticalSection(m)
    #define kv_mutex_unlock(m) LeaveCriticalSection(m)
    #define kv_mutex_destroy(m) DeleteCriticalSection(m)

    static inline ssize_t pread(int fd, void *buf, size_t count, long long offset) {
        HANDLE h = (HANDLE)_get_osfhandle(fd);
        OVERLAPPED ov = {0};
        ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
        ov.OffsetHigh = (DWORD)(offset >> 32);
        DWORD read = 0;
        if (!ReadFile(h, buf, (DWORD)count, &read, &ov)) return -1;
        return (ssize_t)read;
    }

    #define mkdir(p, m) _mkdir(p)
    #define fileno _fileno
    #define fsync _commit

#else
    #include <pthread.h>
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>

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

#endif

#endif