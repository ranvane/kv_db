#ifndef KV_COMPAT_H
#define KV_COMPAT_H

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <io.h>
    #include <direct.h>

    // 读写锁映射到 Windows SRWLock
    typedef SRWLOCK kv_rwlock_t;
    #define kv_rwlock_init(l) InitializeSRWLock(l)
    #define kv_rwlock_rdlock(l) AcquireSRWLockShared(l)
    #define kv_rwlock_wrlock(l) AcquireSRWLockExclusive(l)
    #define kv_rwlock_unlock_rd(l) ReleaseSRWLockShared(l)
    #define kv_rwlock_unlock_wr(l) ReleaseSRWLockExclusive(l)
    #define kv_rwlock_destroy(l) // SRWLock 不需要手动销毁

    // 互斥锁映射到 Windows CRITICAL_SECTION
    typedef CRITICAL_SECTION kv_mutex_t;
    #define kv_mutex_init(m) InitializeCriticalSection(m)
    #define kv_mutex_lock(m) EnterCriticalSection(m)
    #define kv_mutex_unlock(m) LeaveCriticalSection(m)
    #define kv_mutex_destroy(m) DeleteCriticalSection(m)

    // 文件操作映射
    #define pread _pread_compat
    static inline int _pread_compat(int fd, void *buf, unsigned int count, long long offset) {
        HANDLE h = (HANDLE)_get_osfhandle(fd);
        OVERLAPPED ov = {0};
        ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
        ov.OffsetHigh = (DWORD)(offset >> 32);
        DWORD read = 0;
        if (!ReadFile(h, buf, count, &read, &ov)) return -1;
        return (int)read;
    }

    #define mkdir(p, m) _mkdir(p)
    #define fileno _fileno
    #define fsync _commit

#else
    #include <pthread.h>
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>

    // 读写锁映射到 POSIX pthread_rwlock
    typedef pthread_rwlock_t kv_rwlock_t;
    #define kv_rwlock_init(l) pthread_rwlock_init(l, NULL)
    #define kv_rwlock_rdlock(l) pthread_rwlock_rdlock(l)
    #define kv_rwlock_wrlock(l) pthread_rwlock_wrlock(l)
    #define kv_rwlock_unlock_rd(l) pthread_rwlock_unlock(l)
    #define kv_rwlock_unlock_wr(l) pthread_rwlock_unlock(l)
    #define kv_rwlock_destroy(l) pthread_rwlock_destroy(l)

    // 互斥锁映射到 POSIX pthread_mutex
    typedef pthread_mutex_t kv_mutex_t;
    #define kv_mutex_init(m) pthread_mutex_init(m, NULL)
    #define kv_mutex_lock(m) pthread_mutex_lock(m)
    #define kv_mutex_unlock(m) pthread_mutex_unlock(m)
    #define kv_mutex_destroy(m) pthread_mutex_destroy(m)

#endif

#endif // KV_COMPAT_H
