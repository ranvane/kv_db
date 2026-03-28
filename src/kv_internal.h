#ifndef KV_INTERNAL_H
#define KV_INTERNAL_H

/**
 * @file kv_internal.h
 * @brief KV 数据库内部数据结构定义
 * 
 * 本文件定义了 KV 数据库的核心内部数据结构，包括：
 * - 内存索引结构：用于快速查找键值对的位置信息
 * - 数据库主结构：管理数据库状态、配置和并发控制
 * - 磁盘记录头：定义数据在磁盘上的存储格式
 * 
 * @note 本文件为内部实现细节，外部模块不应直接依赖这些结构
 * @author KVDB Team
 * @version 1.0
 * @date 2024
 */

#include "kv_store.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

/* ============================================================================
 * 内部数据结构定义
 * ============================================================================ */

/**
 * @brief 内存索引项结构
 * 
 * 该结构用于在内存中维护键值对的位置信息，实现快速查找。
 * 每个索引项对应一个键值对，记录其在磁盘文件中的物理位置。
 * 
 * @note 索引项本身不包含键值数据，仅包含位置元数据
 * @note 使用 uthash 库将该结构组织成哈希表
 * 
 * @see kv_db_t::index - 数据库主结构中的索引哈希表
 */
typedef struct {
    uint32_t file_id;    ///< 数据文件 ID，标识键值对存储在哪一个数据文件中（目前主要使用单文件模式）
    uint64_t offset;     ///< 在数据文件中的起始偏移量（字节），用于定位记录位置
    uint32_t size;       ///< 记录总大小（字节），包含：记录头 + 键数据 + 值数据
    uint32_t timestamp;  ///< 写入时间戳（Unix 时间），用于版本控制和冲突检测
} kv_index_entry_t;

/**
 * @brief KV 数据库主结构体
 * 
 * 该结构是数据库的核心控制结构，管理数据库的所有状态、配置和资源。
 * 包含文件管理、索引系统、持久化配置、并发控制和事务处理等模块。
 * 
 * @par 线程安全说明:
 * - 读操作：通过 [index_lock](file:///home/ranvane/WorkSpace/kv_db/src/kv_internal.h#L32-L32) 读写锁实现并发读取
 * - 写操作：通过 [write_lock](file:///home/ranvane/WorkSpace/kv_db/src/kv_internal.h#L45-L45) 互斥锁确保顺序写入
 * - 原子计数：[unpersisted_size](file:///home/ranvane/WorkSpace/kv_db/src/kv_internal.h#L41-L41) 使用原子操作保证线程安全
 * 
 * @par 生命周期:
 * - 创建：调用 `kv_db_open()` 初始化
 * - 使用：通过 [kv_set()](file:///home/ranvane/WorkSpace/kv_db/include/kv_store.h#L60-L60), [kv_get()](file:///home/ranvane/WorkSpace/kv_db/include/kv_store.h#L67-L67) 等接口操作
 * - 销毁：调用 `kv_db_close()` 释放资源
 * 
 * @see kv_db_open() - 打开/创建数据库
 * @see kv_db_close() - 关闭数据库
 */
typedef struct kv_db {
    /* ==================== 基础配置 ==================== */
    char *path;                  ///< 数据库存储目录路径（绝对或相对路径）
    FILE *active_file;           ///< 当前活跃的数据文件句柄，用于写入新记录
    uint32_t active_file_id;     ///< 活跃文件 ID，用于日志文件轮转和恢复
    uint64_t current_offset;     ///< 当前文件写入偏移量，下一条记录的写入位置

    /* ==================== 索引系统 ==================== */
    /**
     * @brief 内存哈希表索引
     * @note 使用 uthash 库实现，键为字符串，值为 kv_index_entry_t*
     */
    struct index_node *index;    
    /**
     * @brief 索引读写锁
     * @note 支持多线程并发读，写操作独占锁
     */
    pthread_rwlock_t index_lock; 

    /* ==================== 持久化配置 ==================== */
    uint32_t persist_time_sec;      ///< 自动同步到磁盘的时间间隔（秒），0 表示禁用定时同步
    size_t persist_size_threshold;  ///< 触发自动同步的未持久化数据量阈值（字节）
    atomic_size_t unpersisted_size; ///< 未同步到磁盘的数据量计数器（原子操作，线程安全）
    time_t last_persist_time;       ///< 上次执行同步操作的时间戳（Unix 时间）

    /* ==================== 并发控制与事务 ==================== */
    pthread_mutex_t write_lock; ///< 写操作互斥锁，确保同一时间只有一个线程写入活跃文件
    bool in_transaction;        ///< 当前线程是否处于事务中，事务期间延迟持久化
    void *txn_log;              ///< 事务临时缓冲区指针，存储未提交的事务操作

    /* ==================== 压缩配置 ==================== */
    bool compression_enabled;   ///< 是否启用 zlib 压缩，启用后值数据会被压缩存储

    /* ==================== 恢复状态 ==================== */
    bool recovering;            ///< 是否处于启动恢复/索引重建阶段，恢复期间跳过某些检查
} kv_db_t;

/**
 * @brief 磁盘存储记录头部结构
 * 
 * 该结构定义每条记录在磁盘文件中的头部格式，紧随其后的是键数据和值数据。
 * 所有字段采用固定长度，便于快速解析和定位。
 * 
 * @par 磁盘布局:
 * @code
 * | 记录头 (20 字节) | 键数据 (key_len 字节) | 值数据 (val_len 字节) |
 * @endcode
 * 
 * @par 字节序:
 * - 所有整数字段采用主机字节序（小端序）
 * - 跨平台使用时需注意字节序转换
 * 
 * @note [checksum](file:///home/ranvane/WorkSpace/kv_db/src/kv_internal.h#L63-L63) 字段当前版本用于存储未压缩长度，便于解压时分配缓冲区
 * 
 * @see kv_record_header_t - 记录头结构
 * @see kv_index_entry_t - 索引项结构（记录该头在文件中的位置）
 */
typedef struct {
    uint32_t key_len;    ///< 键长度（字节），用于读取时确定键数据的边界
    uint32_t val_len;    ///< 值长度（字节），如果启用压缩则为压缩后的长度
    uint32_t type;       ///< 值类型标识（[kv_type_t](file:///home/ranvane/WorkSpace/kv_db/src/kv_store.h) 枚举），支持多种数据类型
    uint32_t timestamp;  ///< 写入时间戳（Unix 时间），用于版本控制和过期检测
    uint32_t checksum;   ///< 校验和/元数据：
                         ///< - 当前版本：存储未压缩前的原始值长度，便于解压时分配缓冲区
                         ///< - 未来版本：可扩展为 CRC32 校验和，用于数据完整性验证
    // 紧接着是 Key 和 Value 的字节数据（变长部分，不在此结构中）
} kv_record_header_t;

/* ============================================================================
 * 宏定义和常量
 * ============================================================================ */

/**
 * @brief 记录头部的固定大小（字节）
 * @note 用于计算记录总大小和文件偏移量
 */
#define KV_RECORD_HEADER_SIZE sizeof(kv_record_header_t)

/**
 * @brief 默认持久化时间间隔（秒）
 * @note 超过此时间未同步则触发自动同步
 */
#define KV_DEFAULT_PERSIST_INTERVAL 60

/**
 * @brief 默认持久化大小阈值（字节）
 * @note 未持久化数据超过此阈值则触发自动同步
 */
#define KV_DEFAULT_PERSIST_THRESHOLD (1024 * 1024)  // 1MB

/* ============================================================================
 * 内部函数声明（仅供内部模块使用）
 * ============================================================================ */

/**
 * @brief 初始化数据库结构体
 * @param db 数据库指针
 * @param path 存储路径
 * @return 0 成功，-1 失败
 */
int kv_db_init(kv_db_t *db, const char *path);

/**
 * @brief 释放数据库资源
 * @param db 数据库指针
 */
void kv_db_destroy(kv_db_t *db);

/**
 * @brief 从磁盘文件恢复索引
 * @param db 数据库指针
 * @return 0 成功，-1 失败
 */
int kv_db_recover_index(kv_db_t *db);

/**
 * @brief 将内存索引同步到磁盘（检查点）
 * @param db 数据库指针
 * @return 0 成功，-1 失败
 */
int kv_db_persist_index(kv_db_t *db);

#endif // KV_INTERNAL_H