#ifndef KV_INTERNAL_H
#define KV_INTERNAL_H

#include "kv_store.h"
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

/**
 * @file kv_internal.h
 * @brief KV 数据库内部结构与 POSIX 标准实现定义
 * 
 * 本文件定义了存储引擎核心数据结构、内存索引项以及磁盘记录格式。
 * 本项目现已完全切换为 POSIX 标准实现，不再支持 Windows/MSVC。
 */

/* ============================================================================
 * POSIX 标准兼容层
 * ============================================================================ */

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
