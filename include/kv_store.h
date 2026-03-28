#ifndef KV_STORE_H
#define KV_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --- 数据类型定义 --- */

/**
 * @brief 支持的值类型枚举
 */
typedef enum {
    KV_TYPE_INT,    // 64位整数
    KV_TYPE_FLOAT,  // 双精度浮点数
    KV_TYPE_BOOL,   // 布尔值
    KV_TYPE_STRING, // 字符串
    KV_TYPE_JSON    // JSON格式字符串
} kv_type_t;

/**
 * @brief 键值对中的值结构体
 */
typedef struct {
    kv_type_t type;    // 值类型
    union {
        int64_t i;     // 整数值
        double f;      // 浮点值
        bool b;        // 布尔值
        char *s;       // 字符串或JSON指针（堆内存）
    } value;
    size_t length;     // 字符串或JSON的长度（不含\0）
} kv_value_t;

/* --- 核心数据库句柄 --- */
typedef struct kv_db kv_db_t;

/* --- 公共 API 接口 --- */

/**
 * @brief 打开或创建一个键值数据库
 * @param path 数据库文件存储目录路径
 * @return 数据库句柄指针，失败返回 NULL
 */
kv_db_t* kv_open(const char *path);

/**
 * @brief 关闭数据库并释放所有相关资源
 * @param db 数据库句柄
 */
void kv_close(kv_db_t *db);

/**
 * @brief 基础 CRUD 操作
 */

/**
 * @brief 插入或更新键值对
 * @return 成功返回 true，失败返回 false
 */
bool kv_set(kv_db_t *db, const char *key, kv_value_t value);

/**
 * @brief 根据键获取对应的值
 * @param value 输出参数，用于存储获取到的值。调用者负责释放字符串类型内存。
 * @return 找到返回 true，未找到返回 false
 */
bool kv_get(kv_db_t *db, const char *key, kv_value_t *value);

/**
 * @brief 根据键删除键值对
 */
bool kv_delete(kv_db_t *db, const char *key);

/**
 * @brief 更新已存在的键值对（内部逻辑与 kv_set 类似）
 */
bool kv_update(kv_db_t *db, const char *key, kv_value_t value);

/**
 * @brief 批量操作
 */
typedef struct {
    const char *key;
    kv_value_t value;
} kv_pair_t;

/**
 * @brief 批量插入键值对
 */
bool kv_batch_set(kv_db_t *db, kv_pair_t *pairs, size_t count);

/**
 * @brief 事务支持
 */

/**
 * @brief 开启一个新的事务
 */
bool kv_begin(kv_db_t *db);

/**
 * @brief 提交当前事务中的所有更改
 */
bool kv_commit(kv_db_t *db);

/**
 * @brief 回滚当前事务，撤销所有未提交的更改
 */
bool kv_rollback(kv_db_t *db);

/**
 * @brief 持久化策略配置
 */

/**
 * @brief 设置基于时间的自动同步周期（秒）
 */
void kv_set_persistence_time(kv_db_t *db, uint32_t seconds);

/**
 * @brief 设置基于数据量的自动同步阈值（字节）
 */
void kv_set_persistence_size(kv_db_t *db, size_t bytes);

/**
 * @brief 备份与恢复
 */

/**
 * @brief 将当前数据库状态备份到指定路径
 */
bool kv_backup(kv_db_t *db, const char *backup_path);

/**
 * @brief 从备份文件恢复数据库
 */
bool kv_restore(kv_db_t *db, const char *backup_path);

/**
 * @brief 工具函数：释放 kv_value_t 内部分配的堆内存（如字符串）
 */
void kv_value_free(kv_value_t *val);

#endif // KV_STORE_H
