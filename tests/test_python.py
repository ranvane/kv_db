#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
KV 数据库 Python API 测试模块

本模块用于验证 KV 数据库 Python 绑定接口的功能正确性，测试覆盖以下核心功能：
    1. 基础数据类型支持 - 验证 int、float、bool、str、dict 等类型的存储和读取
    2. 数据更新操作 - 验证键值对的更新功能
    3. 数据删除操作 - 验证键值对的删除功能
    4. 事务支持 - 验证事务的 begin/commit 机制和数据隔离性

测试采用断言验证确保数据一致性，测试完成后自动清理临时文件。

@module: test_python
@author: KVDB Team
@version: 1.0
@see: python.kv_db.KVDB - Python 绑定的数据库类
"""

import os
import sys
import shutil

# 将项目根目录添加到 Python 路径中，以便正确导入 python.kv_db 模块
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from python.kv_db import KVDB


def test_python_api():
    """
    执行 Python API 功能完整性测试
    
    本函数依次执行四项核心测试：
    1. 基础数据类型测试 - 验证 5 种常用数据类型的存储和读取
    2. 更新操作测试 - 验证已存在键的值更新功能
    3. 删除操作测试 - 验证键值对的删除及删除后查询返回 None
    4. 事务操作测试 - 验证事务的隔离性和提交后的可见性
    
    测试流程：
        - 创建临时测试数据库目录
        - 初始化 KV 数据库实例
        - 依次执行各项功能测试并使用 assert 断言验证
        - 关闭数据库连接并清理测试文件
    
    @raises: AssertionError - 当实际结果与预期值不匹配时抛出
    @raises: Exception - 数据库操作异常时抛出
    
    @note: 测试数据库路径为 ./python_test_db，测试完成后自动删除
    @note: 所有测试项按顺序执行，任一失败将中断后续测试
    """
    # ==================== 测试环境准备 ====================
    # 配置测试数据库路径
    db_path = "./python_test_db"
    
    # 清理已存在的测试数据库目录，确保测试环境干净
    if os.path.exists(db_path):
        shutil.rmtree(db_path)

    # 初始化 KV 数据库实例
    db = KVDB(db_path)

    # ==================== 测试项 1: 基础数据类型 ====================
    """
    验证数据库对 Python 常用数据类型的支持能力
    
    测试类型包括：
    - int: 整数类型
    - float: 浮点数类型
    - bool: 布尔类型
    - str: 字符串类型
    - dict: 字典类型（含嵌套列表）
    
    验证方式：写入后立即读取，断言读取值与写入值完全一致
    """
    # 写入各种基础类型的键值对
    db.set("int_key", 123)                      # 整数类型
    db.set("float_key", 45.67)                  # 浮点数类型
    db.set("bool_key", True)                    # 布尔类型
    db.set("str_key", "hello python")           # 字符串类型
    db.set("json_key", {"a": 1, "b": [2, 3]})   # 字典类型（含嵌套列表）

    # 断言验证：确保读取的值与写入的值类型和值都一致
    assert db.get("int_key") == 123, "整数类型读取失败"
    assert db.get("float_key") == 45.67, "浮点数类型读取失败"
    assert db.get("bool_key") is True, "布尔类型读取失败"
    assert db.get("str_key") == "hello python", "字符串类型读取失败"
    assert db.get("json_key") == {"a": 1, "b": [2, 3]}, "字典类型读取失败"

    print("✓ Python API 基础数据类型测试：通过")

    # ==================== 测试项 2: 更新操作 ====================
    """
    验证键值对的更新功能
    
    测试场景：对已存在的键执行 set 操作，验证新值覆盖旧值
    预期结果：读取时返回更新后的新值
    """
    # 更新已存在的键的值
    db.update("int_key", 456)
    # 断言验证：确保更新后的值正确
    assert db.get("int_key") == 456, "更新操作失败：期望 456, 实际 " + str(db.get("int_key"))
    print("✓ Python API 更新操作测试：通过")

    # ==================== 测试项 3: 删除操作 ====================
    """
    验证键值对的删除功能
    
    测试场景：删除已存在的键，验证删除后查询返回 None
    预期结果：get() 返回 None，表示键不存在
    """
    # 删除已存在的键
    db.delete("str_key")
    # 断言验证：确保删除后查询返回 None
    assert db.get("str_key") is None, "删除操作失败：期望 None, 实际 " + str(db.get("str_key"))
    print("✓ Python API 删除操作测试：通过")

    # ==================== 测试项 4: 事务操作 ====================
    """
    验证事务的隔离性和提交机制
    
    测试场景：
    1. 开启事务 (begin)
    2. 在事务中写入数据
    3. 提交前验证数据不可见（隔离性）
    4. 提交事务 (commit)
    5. 提交后验证数据可见
    
    预期结果：
    - 提交前：事务内的写入对其他操作不可见
    - 提交后：事务内的写入对外可见
    """
    # 开启事务
    db.begin()
    # 在事务中写入数据
    db.set("txn_key", "secret")
    # 断言验证：事务未提交前，数据应该不可见（返回 None）
    assert db.get("txn_key") is None, "事务隔离性失败：未提交时数据应不可见"
    
    # 提交事务
    db.commit()
    # 断言验证：事务提交后，数据应该可见
    assert db.get("txn_key") == "secret", "事务提交失败：提交后数据应可见"
    print("✓ Python API 事务操作测试：通过")

    # ==================== 测试清理 ====================
    # 关闭数据库连接，释放资源
    db.close()
    
    # 清理测试数据库目录
    if os.path.exists(db_path):
        shutil.rmtree(db_path)
    
    print("✅ 所有 Python API 测试通过！")


if __name__ == "__main__":
    """
    程序入口点
    
    当直接运行此脚本时（而非作为模块导入），执行测试函数。
    使用方式：
        $ python test_python.py
    """
    test_python_api()