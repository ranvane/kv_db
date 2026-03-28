#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
KV 数据库 Python 绑定接口模块

本模块提供 KV 数据库的 Python 语言封装，通过 ctypes 调用底层 C 语言实现的动态库。
支持以下核心功能：
    1. 多数据类型存储 - 支持 int、float、bool、str、dict、list 等 Python 原生类型
    2. CRUD 操作 - 提供 set、get、delete、update 等基本操作接口
    3. 事务支持 - 提供 begin、commit、rollback 事务控制接口
    4. 自动资源管理 - 通过析构函数确保数据库连接正确关闭

本模块设计目标：
    - API 简洁易用，类似 SQLite 的使用体验
    - 自动处理 Python 类型与 C 类型的转换
    - 内存安全，自动管理 C 端分配的内存

@module: kv_db
@author: KVDB Team
@version: 1.0
@date: 2024
@see: python.kv_db.KVDB - 主数据库类
"""

import ctypes
import os
import json
from enum import IntEnum


# ============================================================================
# 类型定义
# ============================================================================

class KVType(IntEnum):
    """
    KV 数据库支持的值类型枚举
    
    该枚举定义了数据库支持的所有数据类型，与 C 端的 kv_type_t 枚举保持一致。
    每种类型对应不同的存储格式和序列化方式。
    
    @var INT: 整数类型 (int64)
    @var FLOAT: 浮点数类型 (double)
    @var BOOL: 布尔类型 (bool)
    @var STRING: 字符串类型 (UTF-8 编码)
    @var JSON: JSON 类型 (序列化为 UTF-8 字符串存储)
    
    @note: 类型枚举值必须与 C 端定义保持一致，否则会导致类型识别错误
    """
    INT = 0      # 64 位有符号整数
    FLOAT = 1    # 64 位浮点数 (double)
    BOOL = 2     # 布尔值 (true/false)
    STRING = 3   # UTF-8 编码字符串
    JSON = 4     # JSON 对象或数组 (内部序列化为字符串)


class KVValueUnion(ctypes.Union):
    """
    值联合体 - 对接 C 语言的 union 结构
    
    该联合体用于存储不同类型的值数据，同一时间只使用其中一个字段。
    使用联合体可以节省内存空间，避免为每种类型单独分配存储。
    
    @var i: 整数值字段，对应 KVType.INT
    @var f: 浮点数值字段，对应 KVType.FLOAT
    @var b: 布尔值字段，对应 KVType.BOOL
    @var s: 字符串指针字段，对应 KVType.STRING 和 KVType.JSON
    
    @note: 使用哪个字段取决于 KVValue.type 字段的值
    @warning: 不要同时访问多个字段，应根据 type 字段访问对应字段
    """
    _fields_ = [
        ("i", ctypes.c_int64),      # 整数值 (64 位)
        ("f", ctypes.c_double),     # 浮点数值 (64 位)
        ("b", ctypes.c_bool),       # 布尔值
        ("s", ctypes.c_char_p),     # 字符串指针 (UTF-8 编码)
    ]


class KVValue(ctypes.Structure):
    """
    键值对值结构体 - 对接 C 语言的 kv_value_t
    
    该结构体封装了值的类型、数据和长度信息，是 Python 与 C 端数据交换的核心结构。
    
    @var type: 值类型标识，使用 KVType 枚举值
    @var value: 值联合体，存储实际数据
    @var length: 数据长度（字节），主要用于字符串和 JSON 类型
    
    @see: KVValueUnion - 值联合体
    @see: KVType - 值类型枚举
    
    @par 内存布局:
    @code
    | type (4 字节) | value (8 字节) | length (8 字节) |
    @endcode
    """
    _fields_ = [
        ("type", ctypes.c_int),           # 值类型标识
        ("value", KVValueUnion),          # 值联合体
        ("length", ctypes.c_size_t),      # 数据长度（字节）
    ]


class KVPair(ctypes.Structure):
    """
    键值对结构体 - 对接 C 语言的 kv_pair_t
    
    该结构体用于批量操作时传递键值对数据，目前主要用于内部实现。
    
    @var key: 键字符串（UTF-8 编码）
    @var value: 值结构体
    
    @note: 目前主要用于批量写入操作的扩展预留
    """
    _fields_ = [
        ("key", ctypes.c_char_p),    # 键字符串指针
        ("value", KVValue),          # 值结构体
    ]


# ============================================================================
# 数据库主类
# ============================================================================

class KVDB:
    """
    高性能键值数据库 Python 接口封装类
    
    该类提供类似 SQLite 的简洁 API，支持基本数据类型及 JSON 对象的存储。
    通过 ctypes 调用底层 C 语言实现的动态库 (libkvdb.so)，实现高性能数据存储。
    
    @par 支持的数据类型:
        - int: 整数类型
        - float: 浮点数类型
        - bool: 布尔类型
        - str: 字符串类型
        - dict/list: JSON 类型（自动序列化）
    
    @par 核心功能:
        - CRUD 操作：set、get、delete、update
        - 事务控制：begin、commit、rollback
        - 资源管理：自动关闭连接，支持上下文管理器
    
    @par 线程安全:
        - 底层 C 库支持多线程并发访问
        - Python GIL 保证 Python 端的线程安全
    
    @example:
        >>> db = KVDB("./mydb")
        >>> db.set("name", "Alice")
        >>> db.set("age", 25)
        >>> db.set("info", {"city": "Beijing", "skills": ["Python", "C"]})
        >>> print(db.get("name"))  # 输出：Alice
        >>> db.close()
    
    @author: KVDB Team
    @version: 1.0
    """

    def __init__(self, db_path):
        """
        初始化并连接到 KV 数据库
        
        构造函数负责加载动态库、设置 C 函数签名、打开数据库连接。
        如果数据库目录不存在，会自动创建。
        
        @param db_path (str): 数据库文件存储目录路径
                            - 可以是绝对路径或相对路径
                            - 路径应指向一个目录，而非文件
                            - 需要确保当前用户有读写权限
        
        @raises Exception: 当无法打开数据库时抛出（路径不存在、权限不足等）
        @raises OSError: 当动态库加载失败时抛出
        
        @note: 数据库文件存储在 db_path 目录下，文件名为 data.log
        @note: 首次打开时会创建必要的目录和文件结构
        
        @see: close() - 关闭数据库连接
        @see: __del__() - 析构函数
        
        @example:
            >>> # 创建数据库实例
            >>> db = KVDB("./data/mydb")
            >>> # 使用绝对路径
            >>> db = KVDB("/home/user/kvdb_data")
        """
        # 构建动态库的绝对路径
        # 先在当前包目录下查找 libkvdb.so
        lib_name = "libkvdb.so"
        current_dir = os.path.dirname(os.path.abspath(__file__))
        lib_path = os.path.join(current_dir, lib_name)
        
        # 如果当前包目录没找到（开发环境），尝试向上级目录查找
        if not os.path.exists(lib_path):
            lib_path = os.path.join(current_dir, "..", lib_name)
        
        # 如果还是没找到，抛出更友好的错误
        if not os.path.exists(lib_path):
            raise OSError(f"找不到核心共享库 {lib_name}。请确保已编译项目或正确安装包。")
        
        # 加载 C 动态库
        self.lib = ctypes.CDLL(lib_path)

        # ==================== 设置 C 函数签名 ====================
        # 设置每个 C 函数的参数类型 (argtypes) 和返回类型 (restype)
        # 这是 ctypes 正确调用 C 函数的必要条件
        
        # kv_open: 打开数据库
        self.lib.kv_open.restype = ctypes.c_void_p      # 返回数据库句柄指针
        self.lib.kv_open.argtypes = [ctypes.c_char_p]   # 参数：路径字符串
        
        # kv_close: 关闭数据库
        self.lib.kv_close.argtypes = [ctypes.c_void_p]  # 参数：数据库句柄指针
        
        # kv_set: 设置键值对
        self.lib.kv_set.restype = ctypes.c_bool         # 返回：成功/失败
        self.lib.kv_set.argtypes = [
            ctypes.c_void_p,    # 数据库句柄
            ctypes.c_char_p,    # 键字符串
            KVValue             # 值结构体
        ]
        
        # kv_get: 获取键值
        self.lib.kv_get.restype = ctypes.c_bool         # 返回：是否找到
        self.lib.kv_get.argtypes = [
            ctypes.c_void_p,                    # 数据库句柄
            ctypes.c_char_p,                    # 键字符串
            ctypes.POINTER(KVValue)             # 输出：值结构体指针
        ]
        
        # kv_delete: 删除键值对
        self.lib.kv_delete.restype = ctypes.c_bool
        self.lib.kv_delete.argtypes = [
            ctypes.c_void_p,    # 数据库句柄
            ctypes.c_char_p     # 键字符串
        ]
        
        # kv_begin: 开启事务
        self.lib.kv_begin.restype = ctypes.c_bool
        self.lib.kv_begin.argtypes = [ctypes.c_void_p]
        
        # kv_commit: 提交事务
        self.lib.kv_commit.restype = ctypes.c_bool
        self.lib.kv_commit.argtypes = [ctypes.c_void_p]
        
        # kv_rollback: 回滚事务
        self.lib.kv_rollback.restype = ctypes.c_bool
        self.lib.kv_rollback.argtypes = [ctypes.c_void_p]
        
        # kv_value_free: 释放值结构体分配的内存
        self.lib.kv_value_free.argtypes = [ctypes.POINTER(KVValue)]

        # ==================== 打开数据库连接 ====================
        # 调用 C 函数打开数据库，获取数据库句柄
        self.db = self.lib.kv_open(db_path.encode('utf-8'))
        
        # 检查数据库是否成功打开
        if not self.db:
            raise Exception(f"无法打开数据库，请检查路径权限：{db_path}")

    def close(self):
        """
        关闭数据库连接并释放所有资源
        
        该方法会：
        1. 刷新所有未持久化的数据到磁盘
        2. 关闭数据文件句柄
        3. 释放内存索引结构
        4. 释放数据库句柄
        
        @note: 调用后该 KVDB 实例不可再使用
        @note: 建议显式调用此方法，而非依赖析构函数
        @warning: 重复调用 close() 是安全的，第二次调用无效果
        
        @see: __del__() - 析构函数会自动调用 close()
        @see: __enter__() / __exit__() - 支持上下文管理器
        
        @example:
            >>> db = KVDB("./mydb")
            >>> db.set("key", "value")
            >>> db.close()  # 显式关闭
        """
        if self.db:
            self.lib.kv_close(self.db)
            self.db = None

    def __del__(self):
        """
        析构函数 - 确保资源被正确释放
        
        当对象被垃圾回收时自动调用，确保数据库连接不会泄露。
        作为资源管理的最后一道保障，建议仍显式调用 close()。
        
        @note: Python 垃圾回收时机不确定，不应依赖此方法释放关键资源
        @see: close() - 推荐显式调用此方法
        """
        self.close()

    def __enter__(self):
        """
        上下文管理器入口 - 支持 with 语句
        
        @return: self - 返回当前数据库实例
        
        @example:
            >>> with KVDB("./mydb") as db:
            ...     db.set("key", "value")
            ...     # 退出 with 块时自动关闭
        """
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """
        上下文管理器出口 - 自动关闭数据库
        
        @param exc_type: 异常类型（如有）
        @param exc_val: 异常值（如有）
        @param exc_tb: 异常追踪（如有）
        @return: False - 不抑制异常
        """
        self.close()
        return False

    def set(self, key, value):
        """
        设置键值对 - 写入或更新数据
        
        该方法支持多种 Python 数据类型，会自动进行类型识别和转换。
        如果键已存在，则更新其值；如果键不存在，则创建新键值对。
        
        @param key (str): 键名，必须是字符串类型
                         - 建议使用字母、数字、下划线组合
                         - 避免使用特殊字符和空格
        @param value: 值，支持以下类型：
                     - int: 整数（自动转换为 int64）
                     - float: 浮点数（自动转换为 double）
                     - bool: 布尔值
                     - str: 字符串（UTF-8 编码）
                     - dict: 字典（序列化为 JSON）
                     - list: 列表（序列化为 JSON）
        
        @return (bool): 操作成功返回 True，失败返回 False
        
        @raises TypeError: 当值类型不支持时抛出
        @raises Exception: 当数据库操作失败时抛出
        
        @note: JSON 类型使用 json.dumps() 序列化，支持嵌套结构
        @note: 字符串和 JSON 类型会自动进行 UTF-8 编码
        @warning: 大对象（>1MB）可能影响性能，建议分块存储
        
        @see: get() - 获取键值
        @see: delete() - 删除键值对
        @see: update() - 更新键值（实际调用 set）
        
        @example:
            >>> db.set("count", 100)                    # 整数
            >>> db.set("price", 19.99)                  # 浮点数
            >>> db.set("active", True)                  # 布尔值
            >>> db.set("name", "Alice")                 # 字符串
            >>> db.set("user", {"id": 1, "name": "Bob"}) # JSON 对象
            >>> db.set("tags", ["python", "database"])   # JSON 数组
        """
        # 创建值结构体实例
        kv_val = KVValue()
        
        # ==================== 类型识别与转换 ====================
        # 注意：bool 检查必须在 int 之前，因为 bool 是 int 的子类
        if isinstance(value, bool):
            # 布尔类型
            kv_val.type = KVType.BOOL
            kv_val.value.b = value
            
        elif isinstance(value, int):
            # 整数类型
            kv_val.type = KVType.INT
            kv_val.value.i = value
            
        elif isinstance(value, float):
            # 浮点数类型
            kv_val.type = KVType.FLOAT
            kv_val.value.f = value
            
        elif isinstance(value, str):
            # 字符串类型 - 需要编码为 UTF-8
            kv_val.type = KVType.STRING
            kv_val.value.s = value.encode('utf-8')
            kv_val.length = len(kv_val.value.s)
            
        elif isinstance(value, (dict, list)):
            # JSON 类型 - 序列化为 JSON 字符串后存储
            json_str = json.dumps(value, ensure_ascii=False).encode('utf-8')
            kv_val.type = KVType.JSON
            kv_val.value.s = json_str
            kv_val.length = len(json_str)
            
        else:
            # 不支持的类型
            raise TypeError(
                f"不支持的值类型：{type(value)}。"
                f"支持的类型：int, float, bool, str, dict, list"
            )

        # ==================== 调用 C 函数写入 ====================
        # 将键编码为 UTF-8，调用 C 端的 kv_set 函数
        return self.lib.kv_set(self.db, key.encode('utf-8'), kv_val)

    def get(self, key):
        """
        获取指定键的值 - 读取数据
        
        该方法根据键名从数据库中检索对应的值，并自动进行类型转换，
        返回 Python 原生数据类型。
        
        @param key (str): 键名，必须是字符串类型
        
        @return: 对应的 Python 值，类型取决于存储时的类型：
                - INT -> int
                - FLOAT -> float
                - BOOL -> bool
                - STRING -> str
                - JSON -> dict 或 list
                如果键不存在，返回 None
        
        @raises Exception: 当数据库操作失败时抛出
        
        @note: 返回的 JSON 数据会自动反序列化为 Python 对象
        @note: 字符串自动从 UTF-8 解码
        @warning: C 端分配的字符串内存会自动释放，无需手动管理
        
        @see: set() - 设置键值对
        @see: delete() - 删除键值对
        
        @example:
            >>> db.set("count", 100)
            >>> count = db.get("count")      # 返回：100 (int)
            >>> db.set("user", {"id": 1})
            >>> user = db.get("user")        # 返回：{"id": 1} (dict)
            >>> missing = db.get("noexist")  # 返回：None
        """
        # 创建值结构体用于接收返回数据
        kv_val = KVValue()
        
        # ==================== 调用 C 函数读取 ====================
        # 调用 kv_get 获取值，通过指针传递 kv_val 接收结果
        if self.lib.kv_get(self.db, key.encode('utf-8'), ctypes.byref(kv_val)):
            res = None
            
            # ==================== 类型转换 ====================
            # 根据 type 字段将 C 类型转换为 Python 类型
            if kv_val.type == KVType.INT:
                # 整数类型
                res = kv_val.value.i
                
            elif kv_val.type == KVType.FLOAT:
                # 浮点数类型
                res = kv_val.value.f
                
            elif kv_val.type == KVType.BOOL:
                # 布尔类型
                res = kv_val.value.b
                
            elif kv_val.type == KVType.STRING:
                # 字符串类型 - 从 UTF-8 解码
                res = kv_val.value.s.decode('utf-8')
                
            elif kv_val.type == KVType.JSON:
                # JSON 类型 - 反序列化为 Python 对象
                res = json.loads(kv_val.value.s.decode('utf-8'))

            # ==================== 内存释放 ====================
            # 调用 C 函数释放 kv_val 中分配的字符串内存
            # 这一步很重要，避免内存泄漏
            self.lib.kv_value_free(ctypes.byref(kv_val))
            
            return res
        
        # 键不存在或读取失败
        return None

    def delete(self, key):
        """
        删除指定键值对
        
        该方法从数据库中永久删除指定的键及其关联的值。
        删除后无法恢复，请谨慎使用。
        
        @param key (str): 要删除的键名
        
        @return (bool): 操作成功返回 True
                       如果键不存在，也返回 True（幂等性）
                       操作失败返回 False
        
        @note: 删除不存在的键不会抛出异常
        @note: 删除操作会同步到磁盘（取决于持久化配置）
        @warning: 删除后数据无法恢复，请确保不需要该数据
        
        @see: set() - 设置键值对
        @see: get() - 获取键值
        
        @example:
            >>> db.set("temp", "value")
            >>> db.delete("temp")      # 删除成功
            >>> db.get("temp")         # 返回：None
            >>> db.delete("noexist")   # 也返回 True，无副作用
        """
        # 调用 C 端的 kv_delete 函数
        return self.lib.kv_delete(self.db, key.encode('utf-8'))

    def update(self, key, value):
        """
        更新指定键的值
        
        该方法实际上调用 set() 实现，如果键存在则更新值，
        如果键不存在则创建新键值对。
        
        @param key (str): 要更新的键名
        @param value: 新的值（类型同 set 方法）
        
        @return (bool): 操作成功返回 True，失败返回 False
        
        @note: 语义上与 set() 相同，提供更新操作的明确语义
        @see: set() - 设置/更新键值对
        
        @example:
            >>> db.set("count", 100)
            >>> db.update("count", 200)   # 更新为 200
        """
        # 实际调用 set 方法实现更新
        return self.set(key, value)

    def begin(self):
        """
        开启事务
        
        开启一个新的事务，事务内的操作在提交前对其他连接不可见。
        事务提供原子性保证：要么全部成功，要么全部回滚。
        
        @return (bool): 开启成功返回 True，失败返回 False
        
        @note: 同一时间只能有一个活跃事务
        @note: 事务期间数据写入临时缓冲区，提交后才持久化
        @warning: 长时间持有事务可能影响性能
        
        @see: commit() - 提交事务
        @see: rollback() - 回滚事务
        
        @example:
            >>> db.begin()
            >>> db.set("a", 1)
            >>> db.set("b", 2)
            >>> db.commit()  # 全部提交
        """
        return self.lib.kv_begin(self.db)

    def commit(self):
        """
        提交事务
        
        将事务中的所有操作永久保存到数据库。
        提交后，事务内的所有修改对其他连接可见。
        
        @return (bool): 提交成功返回 True，失败返回 False
        
        @note: 提交后事务结束，需要重新 begin() 开启新事务
        @note: 提交操作会触发数据持久化到磁盘
        @warning: 提交后无法回滚
        
        @see: begin() - 开启事务
        @see: rollback() - 回滚事务
        
        @example:
            >>> db.begin()
            >>> db.set("key", "value")
            >>> db.commit()  # 确认提交
        """
        return self.lib.kv_commit(self.db)

    def rollback(self):
        """
        回滚事务
        
        放弃事务中的所有操作，恢复到事务开始前的状态。
        当检测到错误或需要取消操作时使用。
        
        @return (bool): 回滚成功返回 True，失败返回 False
        
        @note: 回滚后事务结束，需要重新 begin() 开启新事务
        @note: 回滚不会影响已提交的事务
        @see: begin() - 开启事务
        @see: commit() - 提交事务
        
        @example:
            >>> db.begin()
            >>> db.set("key", "value")
            >>> # 发现错误，取消操作
            >>> db.rollback()  # 撤销所有未提交的操作
        """
        return self.lib.kv_rollback(self.db)


# ============================================================================
# 模块级工具函数（可选扩展）
# ============================================================================

def open_db(db_path):
    """
    便捷函数 - 打开数据库
    
    作为 KVDB 构造函数的别名，提供更简洁的 API。
    
    @param db_path (str): 数据库路径
    @return: KVDB 实例
    
    @example:
        >>> from python.kv_db import open_db
        >>> db = open_db("./mydb")
    """
    return KVDB(db_path)