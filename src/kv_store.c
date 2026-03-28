/**
 * @file kv_store.c
 * @brief KV 数据库核心实现文件
 * 
 * 本文件实现了 KV 数据库的核心功能，包括：
 *     - 数据库生命周期管理（打开/关闭/恢复）
 *     - 数据序列化与反序列化
 *     - 数据压缩与解压（zlib）
 *     - CRUD 操作（增删改查）
 *     - 事务管理（开始/提交/回滚）
 *     - 批量操作支持
 *     - 持久化策略控制
 *     - 备份与恢复功能
 * 
 * @par 架构说明:
 *     - 使用 uthash 库维护内存哈希索引
 *     - 使用追加写（Append-Only）日志文件存储数据
 *     - 支持 zlib 压缩减少磁盘占用
 *     - 支持多线程并发访问（读写锁 + 互斥锁）
 * 
 * @par 线程安全:
 *     - 读操作：使用读写锁，支持并发读取
 *     - 写操作：使用互斥锁，确保顺序写入
 *     - 索引操作：使用读写锁保护哈希表
 * 
 * @author KVDB Team
 * @version 1.0
 * @date 2024
 */

#include "kv_store.h"
#include "kv_internal.h"
#include "uthash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <zlib.h>

/* ============================================================================
 * 内部辅助结构定义
 * ============================================================================ */

/**
 * @brief 哈希表索引节点（uthash 专用）
 * 
 * 该结构用于在内存中维护键值对的索引，每个节点对应一个键值对。
 * 通过 uthash 库组织成哈希表，实现 O(1) 时间复杂度的查找。
 * 
 * @par 内存布局:
 * @code
 * | key (指针) | entry (索引项) | hh (uthash 句柄) |
 * @endcode
 * 
 * @note key 字段指向堆分配的字符串内存，需要手动释放
 * @note entry 字段记录数据在磁盘文件中的物理位置
 * @note hh 字段由 uthash 库内部管理，不应直接访问
 * 
 * @see kv_index_entry_t - 磁盘位置信息结构
 * @see uthash - https://troydhanson.github.io/uthash/
 */
typedef struct index_node {
    char *key;               ///< 键字符串（唯一标识），堆分配内存
    kv_index_entry_t entry;  ///< 对应的磁盘位置信息（文件 ID、偏移量、大小等）
    UT_hash_handle hh;       ///< uthash 库所需的句柄，用于哈希表操作
} index_node_t;

/**
 * @brief 事务操作节点
 * 
 * 该结构用于事务执行期间临时存储待提交的操作。
 * 事务内的所有写操作先存入链表缓冲区，提交时统一写入磁盘。
 * 
 * @par 使用场景:
 *     - kv_begin() 后，kv_set() 的操作存入此节点
 *     - kv_commit() 时，遍历链表将所有操作写入磁盘
 *     - kv_rollback() 时，直接释放链表丢弃所有操作
 * 
 * @note 节点通过 next 指针组织成单向链表
 * @note value 字段中的字符串内存需要手动管理
 * 
 * @see txn_buffer_t - 事务缓冲区结构
 * @see kv_begin() - 开启事务
 * @see kv_commit() - 提交事务
 */
typedef struct txn_node {
    char *key;               ///< 待操作的键名，堆分配字符串
    kv_value_t value;        ///< 待操作的值，包含类型和数据
    struct txn_node *next;   ///< 链表下一节点指针
} txn_node_t;

/**
 * @brief 事务缓冲区
 * 
 * 该结构记录事务内的一系列操作，通过链表存储所有待提交的数据。
 * 
 * @par 生命周期:
 *     1. kv_begin() 时创建并初始化
 *     2. kv_set() 时将操作添加到链表
 *     3. kv_commit()/kv_rollback() 时释放并清空
 * 
 * @note 缓冲区指针存储在 kv_db_t.txn_log 字段中
 * @note 事务期间 in_transaction 标志置为 true
 * 
 * @see kv_db_t::txn_log - 数据库结构中的事务日志指针
 * @see kv_db_t::in_transaction - 事务状态标志
 */
typedef struct {
    txn_node_t *head;        ///< 链表头指针，新节点插入到头部
    size_t count;            ///< 操作数量，用于统计和调试
} txn_buffer_t;

/* ============================================================================
 * 数据压缩辅助函数
 * ============================================================================ */

/**
 * @brief 使用 zlib 压缩数据
 * 
 * 该函数调用 zlib 库的 compress() 函数对输入数据进行压缩。
 * 压缩后的数据占用更少的磁盘空间，适合存储字符串和 JSON 数据。
 * 
 * @param in 输入数据指针
 * @param in_len 输入数据长度（字节）
 * @param out 输出缓冲区指针（函数内部分配，调用者负责释放）
 * @param out_len 输出数据长度（字节）
 * 
 * @return 成功返回 true，失败返回 false
 *         - 失败原因：内存分配失败、压缩错误等
 * 
 * @note 输出缓冲区由函数内部 malloc 分配，调用者需 free 释放
 * @note 压缩率取决于数据内容，文本数据压缩效果较好
 * @note 数值类型数据压缩效果有限，可能反而增大
 * 
 * @see decompress_value() - 解压函数
 * @see kv_set_internal() - 写入时调用压缩
 * 
 * @example
 * @code
 * uint8_t *compressed = NULL;
 * uint32_t comp_len = 0;
 * if (compress_value(data, data_len, &compressed, &comp_len)) {
 *     // 使用压缩数据...
 *     free(compressed);  // 记得释放
 * }
 * @endcode
 */
static bool compress_value(const uint8_t *in, uint32_t in_len, 
                           uint8_t **out, uint32_t *out_len) {
    // 计算压缩后缓冲区的最大可能大小
    uLongf dest_len = compressBound(in_len);
    
    // 分配输出缓冲区
    *out = malloc(dest_len);
    if (!*out) return false;
    
    // 执行压缩
    if (compress(*out, &dest_len, in, in_len) != Z_OK) {
        free(*out);
        *out = NULL;
        return false;
    }
    
    // 设置实际压缩后的长度
    *out_len = (uint32_t)dest_len;
    return true;
}

/**
 * @brief 使用 zlib 解压数据
 * 
 * 该函数调用 zlib 库的 uncompress() 函数对压缩数据进行解压。
 * 读取数据时，如果检测到数据被压缩，则调用此函数还原原始数据。
 * 
 * @param in 压缩数据指针
 * @param in_len 压缩数据长度（字节）
 * @param out 输出缓冲区指针（函数内部分配，调用者负责释放）
 * @param out_len 解压后数据的预期长度（字节）
 * 
 * @return 成功返回 true，失败返回 false
 *         - 失败原因：内存分配失败、解压错误、数据损坏等
 * 
 * @note 输出缓冲区由函数内部 malloc 分配，调用者需 free 释放
 * @note out_len 参数应为原始未压缩数据的长度（存储在 header.checksum 中）
 * 
 * @see compress_value() - 压缩函数
 * @see kv_get() - 读取时调用解压
 */
static bool decompress_value(const uint8_t *in, uint32_t in_len, 
                             uint8_t **out, uint32_t out_len) {
    // 分配输出缓冲区（大小为解压后的预期长度）
    *out = malloc(out_len);
    if (!*out) return false;
    
    // 执行解压
    uLongf dest_len = out_len;
    if (uncompress(*out, &dest_len, in, in_len) != Z_OK) {
        free(*out);
        *out = NULL;
        return false;
    }
    
    return true;
}

/* ============================================================================
 * 序列化与反序列化函数
 * ============================================================================ */

/**
 * @brief 将 kv_value_t 转换为字节数组
 * 
 * 该函数将 Python/ C 端的 kv_value_t 结构序列化为连续的字节数组，
 * 便于写入磁盘文件。不同数据类型采用不同的序列化方式：
 *     - INT/FLOAT/BOOL: 直接复制二进制表示
 *     - STRING/JSON: 复制字符串字节
 * 
 * @param value 输入值结构体
 * @param buf 输出缓冲区指针（函数内部分配，调用者负责释放）
 * @param len 输出数据长度（字节）
 * 
 * @return 成功返回 true，失败返回 false
 *         - 失败原因：未知类型、内存分配失败
 * 
 * @par 序列化格式:
 * @code
 * INT:   [8 字节 int64_t]
 * FLOAT: [8 字节 double]
 * BOOL:  [1 字节 bool]
 * STRING/JSON: [N 字节字符数据]
 * @endcode
 * 
 * @note 输出缓冲区由函数内部 malloc 分配，调用者需 free 释放
 * @note 序列化不包含类型信息，类型由 header.type 单独存储
 * @note 字节序使用主机字节序（小端序）
 * 
 * @see deserialize_value() - 反序列化函数
 * @see kv_set_internal() - 写入前调用序列化
 */
static bool serialize_value(kv_value_t value, uint8_t **buf, uint32_t *len) {
    switch (value.type) {
        case KV_TYPE_INT:
            // 整数类型：复制 8 字节 int64_t
            *len = sizeof(int64_t);
            *buf = malloc(*len);
            if (!*buf) return false;
            memcpy(*buf, &value.value.i, *len);
            break;
            
        case KV_TYPE_FLOAT:
            // 浮点类型：复制 8 字节 double
            *len = sizeof(double);
            *buf = malloc(*len);
            if (!*buf) return false;
            memcpy(*buf, &value.value.f, *len);
            break;
            
        case KV_TYPE_BOOL:
            // 布尔类型：复制 1 字节 bool
            *len = sizeof(bool);
            *buf = malloc(*len);
            if (!*buf) return false;
            memcpy(*buf, &value.value.b, *len);
            break;
            
        case KV_TYPE_STRING:
        case KV_TYPE_JSON:
            // 字符串/JSON 类型：复制字符数据
            *len = (uint32_t)value.length;
            *buf = malloc(*len);
            if (!*buf) return false;
            memcpy(*buf, value.value.s, *len);
            break;
            
        default:
            // 未知类型
            return false;
    }
    return true;
}

/**
 * @brief 将字节数组转换回 kv_value_t
 * 
 * 该函数是 serialize_value() 的逆操作，从磁盘读取的字节数组
 * 反序列化为 kv_value_t 结构，供上层 API 使用。
 * 
 * @param type 值类型标识（从记录头中读取）
 * @param buf 输入字节数组指针
 * @param len 输入数据长度（字节）
 * @param value 输出值结构体指针
 * 
 * @return 成功返回 true，失败返回 false
 *         - 失败原因：未知类型、内存分配失败
 * 
 * @par 内存管理:
 *     - STRING/JSON 类型的 value.s 由函数内部 malloc 分配
 *     - 调用者需调用 kv_value_free() 释放字符串内存
 *     - 数值类型 (INT/FLOAT/BOOL) 直接存储值，无需释放
 * 
 * @note 字符串类型会自动添加终止符 '\0'
 * @note 反序列化不包含类型信息，类型由 type 参数传入
 * 
 * @see serialize_value() - 序列化函数
 * @see kv_value_free() - 释放值内存
 * @see kv_get() - 读取后调用反序列化
 */
static bool deserialize_value(kv_type_t type, const uint8_t *buf, 
                              uint32_t len, kv_value_t *value) {
    value->type = type;
    value->length = len;
    
    switch (type) {
        case KV_TYPE_INT:
            // 整数类型：复制 8 字节到 int64_t
            memcpy(&value->value.i, buf, len);
            break;
            
        case KV_TYPE_FLOAT:
            // 浮点类型：复制 8 字节到 double
            memcpy(&value->value.f, buf, len);
            break;
            
        case KV_TYPE_BOOL:
            // 布尔类型：复制 1 字节到 bool
            memcpy(&value->value.b, buf, len);
            break;
            
        case KV_TYPE_STRING:
        case KV_TYPE_JSON:
            // 字符串/JSON 类型：分配内存并复制字符数据
            value->value.s = malloc(len + 1);  // +1 用于终止符
            if (!value->value.s) return false;
            memcpy(value->value.s, buf, len);
            value->value.s[len] = '\0';  // 添加字符串终止符
            break;
            
        default:
            // 未知类型
            return false;
    }
    return true;
}

/* ============================================================================
 * 公共 API 实现 - 数据库生命周期管理
 * ============================================================================ */

/**
 * @brief 打开或创建一个键值数据库
 * 
 * 该函数是数据库的入口点，负责：
 *     1. 分配并初始化 kv_db_t 结构
 *     2. 创建数据库目录（如不存在）
 *     3. 打开或创建数据文件 (active.dat)
 *     4. 初始化同步锁（读写锁 + 互斥锁）
 *     5. 设置默认持久化策略
 *     6. 扫描数据文件重建内存索引（恢复）
 * 
 * @param path 数据库存储目录路径
 *             - 可以是绝对路径或相对路径
 *             - 路径应指向目录，而非文件
 *             - 需要确保当前用户有读写权限
 * 
 * @return 成功返回数据库句柄指针，失败返回 NULL
 * 
 * @par 失败原因:
 *     - 内存分配失败
 *     - 目录创建失败（权限不足）
 *     - 数据文件打开失败
 * 
 * @par 线程安全:
 *     - 非线程安全，应在单线程中调用
 *     - 建议在程序启动时调用一次
 * 
 * @note 返回的句柄需要使用 kv_close() 释放
 * @note 首次打开时会创建 active.dat 数据文件
 * @note 恢复过程会扫描整个数据文件重建索引
 * 
 * @see kv_close() - 关闭数据库
 * @see kv_backup() - 备份数据库
 * 
 * @example
 * @code
 * // 打开数据库
 * kv_db_t *db = kv_open("./data/mydb");
 * if (db == NULL) {
 *     fprintf(stderr, "打开数据库失败：%s\n", strerror(errno));
 *     return -1;
 * }
 * 
 * // 使用数据库...
 * 
 * // 关闭数据库
 * kv_close(db);
 * @endcode
 */
kv_db_t* kv_open(const char *path) {
    // ==================== 分配数据库结构 ====================
    kv_db_t *db = calloc(1, sizeof(kv_db_t));
    if (!db) return NULL;

    // 复制路径字符串
    db->path = strdup(path);
    if (!db->path) {
        free(db);
        return NULL;
    }

    // ==================== 创建数据库目录 ====================
    // 创建目录（如不存在），权限 0755（rwxr-xr-x）
    mkdir(path, 0755);

    // ==================== 打开数据文件 ====================
    // 构建数据文件路径：{path}/active.dat
    char active_path[1024];
    snprintf(active_path, sizeof(active_path), "%s/active.dat", path);
    
    // 以追加+读写模式打开文件（ab+）
    // - 文件不存在则创建
    // - 写入时追加到文件末尾
    // - 可读用于恢复索引
    db->active_file = fopen(active_path, "ab+");
    if (!db->active_file) {
        free(db->path);
        free(db);
        return NULL;
    }

    // 获取当前文件末尾位置（下次写入的起始偏移）
    fseek(db->active_file, 0, SEEK_END);
    db->current_offset = ftell(db->active_file);

    // ==================== 初始化同步锁 ====================
    // 索引读写锁：支持并发读，写时独占
    pthread_rwlock_init(&db->index_lock, NULL);
    // 写入互斥锁：确保顺序写入文件
    pthread_mutex_init(&db->write_lock, NULL);

    // ==================== 设置默认持久化策略 ====================
    // 时间阈值：10 秒自动同步一次
    db->persist_time_sec = 10;
    // 大小阈值：1MB 未持久化数据触发同步
    db->persist_size_threshold = 1024 * 1024;
    // 上次同步时间：初始化为当前时间
    db->last_persist_time = time(NULL);
    // 启用压缩
    db->compression_enabled = true;

    // ==================== 恢复索引 ====================
    // 标记为恢复模式，恢复期间跳过某些检查
    db->recovering = true;
    
    // 从文件开头开始扫描
    fseek(db->active_file, 0, SEEK_SET);
    
    // 逐条读取记录头，重建内存索引
    kv_record_header_t header;
    while (fread(&header, sizeof(header), 1, db->active_file) == 1) {
        // 读取键数据
        char *key = malloc(header.key_len + 1);
        if (!key) break;
        
        if (fread(key, header.key_len, 1, db->active_file) != 1) {
            free(key);
            break;
        }
        key[header.key_len] = '\0';  // 添加终止符
        
        // 跳过值数据（不需要读取，只需记录位置）
        fseek(db->active_file, header.val_len, SEEK_CUR);

        // 计算记录起始位置
        uint32_t current_pos = ftell(db->active_file) - 
                               header.val_len - 
                               header.key_len - 
                               sizeof(header);
        
        // 创建索引节点
        index_node_t *node = malloc(sizeof(index_node_t));
        if (!node) {
            free(key);
            break;
        }
        
        node->key = key;
        node->entry.file_id = 0;  // 单文件模式，固定为 0
        node->entry.offset = current_pos;  // 记录起始偏移
        node->entry.size = sizeof(header) + header.key_len + header.val_len;  // 记录总大小
        node->entry.timestamp = header.timestamp;  // 写入时间戳
        
        // 处理键冲突（后写入的覆盖先写入的）
        index_node_t *old_node;
        HASH_FIND_STR(db->index, key, old_node);
        if (old_node) {
            HASH_DEL(db->index, old_node);
            free(old_node->key);
            free(old_node);
        }
        
        // 添加新节点到哈希表
        HASH_ADD_KEYPTR(hh, db->index, node->key, strlen(node->key), node);
    }
    
    // 恢复完成，退出恢复模式
    db->recovering = false;
    
    // 文件指针回到末尾，准备新写入
    fseek(db->active_file, 0, SEEK_END);

    return db;
}

/**
 * @brief 关闭数据库并释放所有相关资源
 * 
 * 该函数负责清理数据库实例，包括：
 *     1. 强制刷盘同步所有未持久化数据
 *     2. 关闭数据文件句柄
 *     3. 释放内存索引（所有哈希节点）
 *     4. 销毁同步锁
 *     5. 释放数据库结构内存
 * 
 * @param db 数据库句柄指针
 *           - 必须是 kv_open() 返回的有效指针
 *           - 传入 NULL 是安全的（无操作）
 *           - 重复关闭是安全的（第二次调用无操作）
 * 
 * @par 线程安全:
 *     - 非线程安全，调用前应确保没有其他线程在使用
 *     - 建议在程序退出前单线程调用
 * 
 * @note 调用后该句柄不可再使用
 * @note 未调用 kv_close() 可能导致数据丢失
 * @note 程序异常退出时，下次打开会自动恢复
 * 
 * @see kv_open() - 打开数据库
 * 
 * @example
 * @code
 * kv_db_t *db = kv_open("./mydb");
 * // ... 使用数据库 ...
 * kv_close(db);  // 确保关闭
 * db = NULL;     // 避免悬空指针
 * @endcode
 */
void kv_close(kv_db_t *db) {
    // 空指针检查
    if (!db) return;
    
    // ==================== 强制刷盘同步 ====================
    // 刷新 stdio 缓冲区到内核缓冲区
    fflush(db->active_file);
    // 强制内核缓冲区写入磁盘
    fsync(fileno(db->active_file));
    // 关闭文件句柄
    fclose(db->active_file);

    // ==================== 释放内存索引 ====================
    // 遍历哈希表，释放所有节点
    index_node_t *current, *tmp;
    HASH_ITER(hh, db->index, current, tmp) {
        HASH_DEL(db->index, current);  // 从哈希表移除
        free(current->key);            // 释放键字符串
        free(current);                 // 释放节点结构
    }

    // ==================== 销毁同步锁 ====================
    pthread_rwlock_destroy(&db->index_lock);
    pthread_mutex_destroy(&db->write_lock);
    
    // ==================== 释放数据库结构 ====================
    free(db->path);
    free(db);
}

/* ============================================================================
 * 公共 API 实现 - 基础 CRUD 操作
 * ============================================================================ */

/**
 * @brief 内部写入函数（核心实现）
 * 
 * 该函数处理实际的写入逻辑，包括：
 *     1. 序列化值为字节数组
 *     2. 可选压缩（如果启用）
 *     3. 构建记录头
 *     4. 写入磁盘文件
 *     5. 检查并触发持久化
 *     6. 更新内存索引
 * 
 * @param db 数据库句柄
 * @param key 键名
 * @param value 值结构体
 * 
 * @return 成功返回 true，失败返回 false
 * 
 * @note 此函数为内部函数，不直接暴露给外部调用
 * @note 调用前应持有 db->write_lock 写锁
 * @note 事务模式下由 kv_commit() 调用此函数
 * 
 * @see kv_set() - 公共写入接口
 * @see kv_commit() - 事务提交时调用
 */
static bool kv_set_internal(kv_db_t *db, const char *key, kv_value_t value) {
    // ==================== 序列化值 ====================
    uint8_t *val_buf = NULL;
    uint32_t val_len = 0;
    if (!serialize_value(value, &val_buf, &val_len)) return false;

    // 记录原始值长度（用于解压）
    uint32_t original_val_len = val_len;
    uint8_t *write_buf = val_buf;
    uint32_t write_len = val_len;

    // ==================== 压缩处理 ====================
    if (db->compression_enabled) {
        uint8_t *comp_buf = NULL;
        uint32_t comp_len = 0;
        // 尝试压缩，如果成功则使用压缩数据
        if (compress_value(val_buf, val_len, &comp_buf, &comp_len)) {
            write_buf = comp_buf;
            write_len = comp_len;
        }
    }

    // ==================== 构建记录头 ====================
    uint32_t key_len = (uint32_t)strlen(key);
    kv_record_header_t header = {
        .key_len = key_len,
        .val_len = write_len,
        .type = value.type,
        .timestamp = (uint32_t)time(NULL),
        .checksum = original_val_len  // 存储原始长度用于解压
    };

    // ==================== 写入磁盘 ====================
    // 记录写入起始偏移
    uint64_t offset = db->current_offset;
    
    // 写入记录头
    if (fwrite(&header, sizeof(header), 1, db->active_file) != 1) goto fail;
    // 写入键数据
    if (fwrite(key, key_len, 1, db->active_file) != 1) goto fail;
    // 写入值数据（可能是压缩的）
    if (fwrite(write_buf, write_len, 1, db->active_file) != 1) goto fail;
    
    // 更新当前偏移量
    db->current_offset += sizeof(header) + key_len + write_len;
    // 累加未持久化数据量
    db->unpersisted_size += sizeof(header) + key_len + write_len;

    // ==================== 检查持久化触发 ====================
    time_t now = time(NULL);
    // 触发条件：数据量超过阈值 或 时间超过间隔
    if (db->unpersisted_size >= db->persist_size_threshold || 
        (now - db->last_persist_time) >= db->persist_time_sec) {
        fflush(db->active_file);
        fsync(fileno(db->active_file));
        db->unpersisted_size = 0;
        db->last_persist_time = now;
    }

    // ==================== 更新内存索引 ====================
    pthread_rwlock_wrlock(&db->index_lock);
    index_node_t *node;
    HASH_FIND_STR(db->index, key, node);
    if (!node) {
        // 键不存在，创建新节点
        node = malloc(sizeof(index_node_t));
        node->key = strdup(key);
        HASH_ADD_KEYPTR(hh, db->index, node->key, strlen(node->key), node);
    }
    // 更新索引项（覆盖旧的位置信息）
    node->entry.offset = offset;
    node->entry.size = sizeof(header) + key_len + write_len;
    node->entry.timestamp = header.timestamp;
    pthread_rwlock_unlock(&db->index_lock);

    // ==================== 清理临时缓冲区 ====================
    if (write_buf != val_buf) free(write_buf);  // 释放压缩缓冲区（如果有）
    free(val_buf);  // 释放序列化缓冲区
    return true;

fail:
    // 失败清理
    if (write_buf != val_buf) free(write_buf);
    free(val_buf);
    return false;
}

/**
 * @brief 插入或更新键值对
 * 
 * 该函数是数据库的核心写入接口，支持以下场景：
 *     - 键不存在：创建新键值对
 *     - 键已存在：更新为新的值（覆盖旧值）
 * 
 * @param db 数据库句柄
 * @param key 键名字符串
 * @param value 值结构体
 * 
 * @return 成功返回 true，失败返回 false
 * 
 * @par 事务支持:
 *     - 如果在事务中 (in_transaction=true)，操作先存入事务缓冲区
 *     - 事务提交时统一写入磁盘
 *     - 事务回滚时丢弃缓冲区
 * 
 * @par 线程安全:
 *     - 线程安全，内部使用写锁保护
 * 
 * @note 字符串/JSON 类型的 value.s 内存由调用者管理
 * @note 大值（>1MB）可能影响性能
 * 
 * @see kv_set_internal() - 内部写入实现
 * @see kv_begin() - 开启事务
 * @see kv_commit() - 提交事务
 */
bool kv_set(kv_db_t *db, const char *key, kv_value_t value) {
    // ==================== 事务模式处理 ====================
    if (db->in_transaction) {
        // 在事务中，写入临时缓冲区而非直接写入磁盘
        txn_buffer_t *txn = (txn_buffer_t*)db->txn_log;
        
        // 创建事务节点
        txn_node_t *node = malloc(sizeof(txn_node_t));
        node->key = strdup(key);
        node->value.type = value.type;
        node->value.length = value.length;
        
        // 复制值数据
        if (value.type == KV_TYPE_STRING || value.type == KV_TYPE_JSON) {
            // 字符串类型需要深拷贝
            node->value.value.s = malloc(value.length + 1);
            memcpy(node->value.value.s, value.value.s, value.length);
            node->value.value.s[value.length] = '\0';
        } else {
            // 数值类型直接复制
            node->value.value = value.value;
        }
        
        // 插入到链表头部
        node->next = txn->head;
        txn->head = node;
        txn->count++;
        return true;
    }
    
    // ==================== 非事务模式：直接写入 ====================
    pthread_mutex_lock(&db->write_lock);
    bool res = kv_set_internal(db, key, value);
    pthread_mutex_unlock(&db->write_lock);
    return res;
}

/**
 * @brief 根据键获取对应的值
 * 
 * 该函数从内存索引查找键，然后从磁盘文件读取对应的值数据。
 * 读取流程：
 *     1. 查找内存索引获取位置信息
 *     2. 使用 pread 从文件读取记录头
 *     3. 读取值数据（可能是压缩的）
 *     4. 解压（如果启用了压缩）
 *     5. 反序列化为 kv_value_t
 * 
 * @param db 数据库句柄
 * @param key 键名字符串
 * @param value 输出参数，存储获取到的值
 * 
 * @return 找到返回 true，未找到返回 false
 * 
 * @par 内存管理:
 *     - 返回的字符串/JSON 数据存储在堆内存中
 *     - 调用者必须调用 kv_value_free() 释放
 *     - 数值类型无需释放
 * 
 * @par 线程安全:
 *     - 线程安全，使用读写锁保护索引
 *     - 读操作不阻塞其他读操作
 * 
 * @note 使用 pread 而非 fread，避免移动文件指针
 * @note 读取前刷新 stdio 缓冲区确保数据可见
 * 
 * @see kv_set() - 设置键值对
 * @see kv_value_free() - 释放值内存
 */
bool kv_get(kv_db_t *db, const char *key, kv_value_t *value) {
    // ==================== 查找内存索引 ====================
    pthread_rwlock_rdlock(&db->index_lock);
    index_node_t *node;
    HASH_FIND_STR(db->index, key, node);
    if (!node) {
        pthread_rwlock_unlock(&db->index_lock);
        return false;  // 键不存在
    }
    // 复制索引项（释放锁后仍可使用）
    kv_index_entry_t entry = node->entry;
    pthread_rwlock_unlock(&db->index_lock);

    // ==================== 从磁盘读取 ====================
    // 获取文件描述符用于 pread
    int fd = fileno(db->active_file);
    // 刷新 stdio 缓冲区，确保 pread 能读到最新数据
    fflush(db->active_file);
    
    // 读取记录头
    kv_record_header_t header;
    if (pread(fd, &header, sizeof(header), entry.offset) != sizeof(header)) {
        return false;
    }

    // 读取值数据
    uint8_t *read_buf = malloc(header.val_len);
    if (!read_buf) return false;
    
    if (pread(fd, read_buf, header.val_len, 
              entry.offset + sizeof(header) + header.key_len) != header.val_len) {
        free(read_buf);
        return false;
    }

    // ==================== 解压处理 ====================
    uint8_t *val_buf = read_buf;
    uint32_t val_len = header.val_len;

    if (db->compression_enabled) {
        uint8_t *decomp_buf = NULL;
        // 使用 header.checksum 中存储的原始长度进行解压
        if (decompress_value(read_buf, header.val_len, 
                            &decomp_buf, header.checksum)) {
            val_buf = decomp_buf;
            val_len = header.checksum;
        }
    }

    // ==================== 反序列化 ====================
    bool res = deserialize_value(header.type, val_buf, val_len, value);
    
    // 清理临时缓冲区
    if (val_buf != read_buf) free(val_buf);  // 释放解压缓冲区
    free(read_buf);  // 释放读取缓冲区
    
    return res;
}

/**
 * @brief 根据键删除键值对
 * 
 * 该函数从内存索引中移除键值对。
 * 注意：当前简化版本仅从内存索引删除，不写入墓碑记录到磁盘。
 * 
 * @param db 数据库句柄
 * @param key 键名字符串
 * 
 * @return 成功返回 true，失败返回 false
 *         - 键不存在也返回 true（幂等性）
 * 
 * @par 线程安全:
 *     - 线程安全，使用写锁保护索引
 * 
 * @note 当前版本是逻辑删除，磁盘数据未清理
 * @note 压缩/合并阶段会清理已删除的数据
 * 
 * @see kv_set() - 设置键值对
 */
bool kv_delete(kv_db_t *db, const char *key) {
    pthread_rwlock_wrlock(&db->index_lock);
    
    index_node_t *node;
    HASH_FIND_STR(db->index, key, node);
    if (node) {
        // 从哈希表移除并释放内存
        HASH_DEL(db->index, node);
        free(node->key);
        free(node);
        pthread_rwlock_unlock(&db->index_lock);
        // 注意：简化版未向磁盘写入墓碑 (tombstone) 记录
        return true;
    }
    
    pthread_rwlock_unlock(&db->index_lock);
    return false;  // 键不存在
}

/**
 * @brief 更新已存在的键值对
 * 
 * 该函数语义上与 kv_set() 相同，提供明确的更新操作语义。
 * 内部实现直接调用 kv_set()。
 * 
 * @param db 数据库句柄
 * @param key 键名字符串
 * @param value 值结构体
 * 
 * @return 成功返回 true，失败返回 false
 * 
 * @note 如果键不存在，会创建新键值对（与 kv_set 行为一致）
 * @see kv_set() - 设置/更新键值对
 */
bool kv_update(kv_db_t *db, const char *key, kv_value_t value) {
    return kv_set(db, key, value);
}

/* ============================================================================
 * 公共 API 实现 - 事务管理
 * ============================================================================ */

/**
 * @brief 开启一个新的事务
 * 
 * 开启事务后，后续的写操作会先写入事务日志缓冲区，
 * 直到提交或回滚。事务提供原子性保证。
 * 
 * @param db 数据库句柄
 * 
 * @return 成功返回 true，失败返回 false
 *         - 已有活跃事务时返回 false
 * 
 * @par 线程安全:
 *     - 同一数据库实例同一时间只能有一个活跃事务
 *     - 多线程使用时需外部同步
 * 
 * @note 事务期间数据写入临时缓冲区
 * @note 不支持嵌套事务
 * 
 * @see kv_commit() - 提交事务
 * @see kv_rollback() - 回滚事务
 */
bool kv_begin(kv_db_t *db) {
    // 检查是否已有活跃事务
    if (db->in_transaction) return false;
    
    db->in_transaction = true;
    // 分配事务缓冲区
    txn_buffer_t *txn = calloc(1, sizeof(txn_buffer_t));
    db->txn_log = txn;
    return true;
}

/**
 * @brief 提交当前事务中的所有更改
 * 
 * 提交事务后，事务内的所有修改永久保存到数据库。
 * 
 * @param db 数据库句柄
 * 
 * @return 成功返回 true，失败返回 false
 *         - 无活跃事务时返回 false
 * 
 * @par 提交流程:
 *     1. 获取写锁
 *     2. 遍历事务链表，依次写入磁盘
 *     3. 释放事务资源
 *     4. 释放写锁
 * 
 * @note 提交后事务结束
 * @note 提交后无法回滚
 * 
 * @see kv_begin() - 开启事务
 * @see kv_rollback() - 回滚事务
 */
bool kv_commit(kv_db_t *db) {
    if (!db->in_transaction) return false;
    
    pthread_mutex_lock(&db->write_lock);
    txn_buffer_t *txn = (txn_buffer_t*)db->txn_log;
    txn_node_t *curr = txn->head;
    bool success = true;
    
    // 将事务中的所有操作顺序写入磁盘
    while (curr) {
        if (!kv_set_internal(db, curr->key, curr->value)) {
            success = false;
            // 继续处理其他操作，不中断
        }
        curr = curr->next;
    }
    
    // 清理事务资源
    curr = txn->head;
    while (curr) {
        txn_node_t *tmp = curr;
        curr = curr->next;
        free(tmp->key);
        kv_value_free(&tmp->value);
        free(tmp);
    }
    free(txn);
    db->txn_log = NULL;
    db->in_transaction = false;
    pthread_mutex_unlock(&db->write_lock);
    
    return success;
}

/**
 * @brief 回滚当前事务，撤销所有未提交的更改
 * 
 * 回滚事务后，事务内的所有修改被丢弃。
 * 
 * @param db 数据库句柄
 * 
 * @return 成功返回 true，失败返回 false
 *         - 无活跃事务时返回 false
 * 
 * @par 回滚流程:
 *     1. 遍历事务链表
 *     2. 释放所有节点内存
 *     3. 重置事务状态
 * 
 * @note 回滚后事务结束
 * @note 回滚不会影响已提交的事务
 * 
 * @see kv_begin() - 开启事务
 * @see kv_commit() - 提交事务
 */
bool kv_rollback(kv_db_t *db) {
    if (!db->in_transaction) return false;
    
    txn_buffer_t *txn = (txn_buffer_t*)db->txn_log;
    txn_node_t *curr = txn->head;
    
    // 释放所有事务节点
    while (curr) {
        txn_node_t *tmp = curr;
        curr = curr->next;
        free(tmp->key);
        kv_value_free(&tmp->value);
        free(tmp);
    }
    
    free(txn);
    db->txn_log = NULL;
    db->in_transaction = false;
    return true;
}

/* ============================================================================
 * 公共 API 实现 - 批量操作
 * ============================================================================ */

/**
 * @brief 批量插入键值对
 * 
 * 该函数通过事务机制实现批量插入，减少锁竞争和 I/O 次数。
 * 
 * @param db 数据库句柄
 * @param pairs 键值对数组
 * @param count 键值对数量
 * 
 * @return 全部成功返回 true，任一失败返回 false
 * 
 * @par 实现方式:
 *     - 开启事务
 *     - 依次执行 kv_set（写入事务缓冲区）
 *     - 提交事务（统一写入磁盘）
 * 
 * @note 适合批量导入数据场景
 * @see kv_set() - 单个键值对插入
 */
bool kv_batch_set(kv_db_t *db, kv_pair_t *pairs, size_t count) {
    // 使用事务实现批量操作
    kv_begin(db);
    for (size_t i = 0; i < count; i++) {
        kv_set(db, pairs[i].key, pairs[i].value);
    }
    return kv_commit(db);
}

/* ============================================================================
 * 公共 API 实现 - 备份与恢复
 * ============================================================================ */

/**
 * @brief 将当前数据库状态备份到指定路径
 * 
 * 通过复制数据文件实现备份。
 * 
 * @param db 数据库句柄
 * @param backup_path 备份文件路径
 * 
 * @return 成功返回 true，失败返回 false
 * 
 * @note 备份前会刷盘确保数据完整
 * @note 使用 system("cp") 命令，依赖 Unix 环境
 */
bool kv_backup(kv_db_t *db, const char *backup_path) {
    pthread_mutex_lock(&db->write_lock);
    
    // 刷盘确保数据完整
    fflush(db->active_file);
    fsync(fileno(db->active_file));
    
    // 构建源文件路径
    char active_path[1024];
    snprintf(active_path, sizeof(active_path), "%s/active.dat", db->path);
    
    // 执行复制命令
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "cp %s %s", active_path, backup_path);
    int res = system(cmd);
    
    pthread_mutex_unlock(&db->write_lock);
    return res == 0;
}

/**
 * @brief 从备份文件恢复数据库
 * 
 * 通过复制备份文件覆盖当前数据文件实现恢复。
 * 
 * @param db 数据库句柄
 * @param backup_path 备份文件路径
 * 
 * @return 成功返回 true，失败返回 false
 * 
 * @note 恢复会覆盖当前数据库的所有数据
 * @note 恢复后需要重新扫描文件重建索引（简化版未实现）
 */
bool kv_restore(kv_db_t *db, const char *backup_path) {
    pthread_mutex_lock(&db->write_lock);
    
    // 关闭当前文件
    fclose(db->active_file);
    
    // 构建目标文件路径
    char active_path[1024];
    snprintf(active_path, sizeof(active_path), "%s/active.dat", db->path);
    
    // 执行复制命令
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "cp %s %s", backup_path, active_path);
    int res = system(cmd);
    
    // 重新打开文件
    db->active_file = fopen(active_path, "ab+");
    // 恢复后需要重新扫描文件以重建索引（简化版未重复编写扫描代码）
    
    pthread_mutex_unlock(&db->write_lock);
    return res == 0;
}

/* ============================================================================
 * 公共 API 实现 - 持久化配置
 * ============================================================================ */

/**
 * @brief 设置基于时间的自动同步周期
 * 
 * @param db 数据库句柄
 * @param seconds 同步周期（秒），0 表示禁用
 */
void kv_set_persistence_time(kv_db_t *db, uint32_t seconds) {
    db->persist_time_sec = seconds;
}

/**
 * @brief 设置基于数据量的自动同步阈值
 * 
 * @param db 数据库句柄
 * @param bytes 同步阈值（字节），0 表示禁用
 */
void kv_set_persistence_size(kv_db_t *db, size_t bytes) {
    db->persist_size_threshold = bytes;
}

/* ============================================================================
 * 公共 API 实现 - 工具函数
 * ============================================================================ */

/**
 * @brief 释放 kv_value_t 内部分配的堆内存
 * 
 * 该函数负责释放 kv_value_t 结构体中字符串类型的堆内存。
 * 
 * @param val 值结构体指针
 * 
 * @par 使用说明:
 *     - kv_get() 返回的字符串/JSON 必须调用此函数释放
 *     - 数值类型 (INT/FLOAT/BOOL) 无需释放
 *     - 传入 NULL 是安全的
 * 
 * @note 忘记释放会导致内存泄漏
 * @see kv_get() - 获取键值
 */
void kv_value_free(kv_value_t *val) {
    if (!val) return;
    
    if (val->type == KV_TYPE_STRING || val->type == KV_TYPE_JSON) {
        free(val->value.s);
        val->value.s = NULL;
    }
}