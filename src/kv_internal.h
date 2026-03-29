#ifndef KV_INTERNAL_H
#define KV_INTERNAL_H

#include "kv_store.h"
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #include <sys/types.h>
    
    /* 定义 ssize_t 如果编译器没有提供 */
    #ifndef _SSIZE_T_DEFINED
    #define _SSIZE_T_DEFINED
    #undef ssize_t
    #ifdef _WIN64
      typedef __int64 ssize_t;
    #else
      typedef int ssize_t;
    #endif
    #endif

    /* --- MinGW/Windows 兼容补丁 --- */
    
    /* 1. 锁抽象 (映射至 Win32 原生锁) */
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

    /* 2. 原子操作 (使用 Interlocked API) */
    typedef LONG64 kv_atomic_t;
    /* 64位原子读取：使用 InterlockedOr64(p, 0) 确保原子性 */
    #define kv_atomic_load(p) InterlockedOr64((volatile LONG64*)p, 0)
    /* 64位原子写入：使用 InterlockedExchange64 */
    #define kv_atomic_store(p, v) InterlockedExchange64((volatile LONG64*)p, (v))
    #define kv_atomic_inc(p) InterlockedIncrement64((volatile LONG64*)p)
    #define kv_atomic_dec(p) InterlockedDecrement64((volatile LONG64*)p)
    #define kv_atomic_add(p, v) InterlockedAdd64((volatile LONG64*)p, (v))

    /* 3. 模拟 POSIX pread (原子偏移读取) */
    static inline ssize_t kv_pread(int fd, void *buf, size_t count, long long offset) {
        HANDLE h = (HANDLE)_get_osfhandle(fd);
        if (h == INVALID_HANDLE_VALUE) return -1;
        OVERLAPPED ov = {0};
        ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
        ov.OffsetHigh = (DWORD)(offset >> 32);
        DWORD read = 0;
        if (!ReadFile(h, buf, (DWORD)count, &read, &ov)) {
            if (GetLastError() == ERROR_HANDLE_EOF) return 0;
            return -1;
        }
        return (ssize_t)read;
    }

    /* 2. 适配 Windows mkdir (单参数) */
    #define kv_mkdir(p, m) _mkdir(p)
    
    /* 3. 其他文件操作映射 */
    #define kv_fileno _fileno
    #define kv_fsync _commit
    #define kv_sleep_ms(ms) Sleep(ms)

#else
    #ifndef _POSIX_C_SOURCE
        #define _POSIX_C_SOURCE 200809L
    #endif
    #include <stdatomic.h>
    #include <pthread.h>
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <fcntl.h>

    /* --- 锁抽象 (POSIX) --- */
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

    /* --- 原子操作 (C11 Standard) --- */
    typedef _Atomic long long kv_atomic_t;
    #define kv_atomic_load(p) atomic_load(p)
    #define kv_atomic_store(p, v) atomic_store(p, v)
    #define kv_atomic_inc(p) atomic_fetch_add(p, 1)
    #define kv_atomic_dec(p) atomic_fetch_sub(p, 1)
    #define kv_atomic_add(p, v) atomic_fetch_add(p, v)

    /* --- 文件与系统操作 (POSIX) --- */
    #define kv_pread pread
    #define kv_mkdir mkdir
    #define kv_fileno fileno
    #define kv_fsync fsync
    #define kv_sleep_ms(ms) usleep((ms) * 1000)
#endif

/* ============================================================================
 * 内部数据结构
 * ============================================================================ */

/**
 * @brief 内存索引项，记录数据在文件中的位置
 */
typedef struct {
    uint32_t file_id;    
    uint64_t offset;     
    uint32_t size;       
    uint32_t timestamp;  
} kv_index_entry_t;

/**
 * @brief 数据库主句柄结构
 */
typedef struct kv_db {
    char *path;                  
    FILE *active_file;           
    uint32_t active_file_id;     
    uint64_t current_offset;     

    /* 索引系统 */
    struct index_node *index;    
    kv_rwlock_t index_lock;      

    /* 持久化配置与状态 */
    uint32_t persist_time_sec;      
    size_t persist_size_threshold;  

    kv_atomic_t unpersisted_size; 

    time_t last_persist_time;       

    /* 并发控制与事务支持 */
    kv_mutex_t write_lock;      
    bool in_transaction;        
    void *txn_log;              

    /* 压缩配置 */
    bool compression_enabled;   

    /* 恢复状态 */
    bool recovering;            
} kv_db_t;

/**
 * @brief 磁盘存储记录的头部结构 (Header)
 */
typedef struct {
    uint32_t key_len;    
    uint32_t val_len;    
    uint32_t type;       
    uint32_t timestamp;  
    uint32_t checksum;   
} kv_record_header_t;

#endif // KV_INTERNAL_H
