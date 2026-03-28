#ifndef KV_INTERNAL_H
#define KV_INTERNAL_H

#include "kv_store.h"
#include "kv_compat.h"
#include "atomic_compat.h"
#include <stdio.h>

/**
 * @file kv_internal.h
 * @brief KV 数据库内部结构与状态定义
 * 
 * 本文件定义了存储引擎核心数据结构、内存索引项以及磁盘记录格式。
 * 同时包含了用于跨平台兼容性的锁抽象。
 */

/* --- 内部数据结构 --- */

/**
 * @brief 内存索引项，记录数据在文件中的位置
 * 
 * 每个键在内存中都有一个对应的索引项，以便 O(1) 查找。
 */
typedef struct {
    uint32_t file_id;    // 数据文件 ID（目前主要使用单文件 active.dat）
    uint64_t offset;     // 在文件中的起始偏移量（字节）
    uint32_t size;       // 记录的总大小（Header + Key + Value）
    uint32_t timestamp;  // 记录写入时的时间戳
} kv_index_entry_t;

/**
 * @brief 数据库主句柄结构
 * 
 * 维护数据库运行时的所有上下文信息，包括文件句柄、索引、锁及配置。
 */
typedef struct kv_db {
    char *path;                  // 数据库存储目录的路径
    FILE *active_file;           // 当前活跃的数据写入文件句柄 (FILE*)
    uint32_t active_file_id;     // 活跃文件 ID (目前固定为 0)
    uint64_t current_offset;     // 当前活跃文件末尾的偏移量

    /* 索引系统 */
    struct index_node *index;    // 内存哈希表索引根节点 (uthash)
    kv_rwlock_t index_lock;      // 索引读写锁，支持多线程并发读

    /* 持久化配置与状态 */
    uint32_t persist_time_sec;      // 自动同步的时间间隔（秒）
    size_t persist_size_threshold;  // 自动同步的数据量阈值（字节）

    // 修复：确保 atomic_size_t 已定义
    #ifdef _MSC_VER
    volatile size_t unpersisted_size;  // MSVC 兼容写法
    #else
    atomic_size_t unpersisted_size;    // 标准 C11
    #endif

    time_t last_persist_time;       // 上次执行同步操作的时间戳

    /* 并发控制与事务支持 */
    kv_mutex_t write_lock;      // 写锁，确保多线程下顺序写入活跃文件
    bool in_transaction;        // 当前线程/会话是否处于事务状态
    void *txn_log;              // 事务临时缓冲区 (txn_buffer_t)

    /* 压缩配置 */
    bool compression_enabled;   // 是否对存储的 Value 启用 zlib 压缩

    /* 恢复状态 */
    bool recovering;            // 标记是否处于启动时的索引重建阶段
} kv_db_t;

/**
 * @brief 磁盘存储记录的头部结构 (Header)
 * 
 * 固定大小的头部，存储在每个 Key-Value 对之前。
 */
typedef struct {
    uint32_t key_len;    // 键的原始长度
    uint32_t val_len;    // 值的存储长度（若是压缩则为压缩后的长度）
    uint32_t type;       // 数据类型 (详见 kv_type_t)
    uint32_t timestamp;  // 记录生成时间
    uint32_t checksum;   // 目前用于存储原始值的长度，以便解压
} kv_record_header_t;

#endif // KV_INTERNAL_H
