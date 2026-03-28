#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
KV 数据库压力测试模块

本模块用于对 KV 数据库进行全方位的压力测试，验证数据库在以下场景的性能和稳定性：
    1. 顺序写入性能 - 测试批量写入操作的效率
    2. 随机读取性能 - 测试随机访问键值的效率  
    3. 多线程并发访问 - 测试并发场景下的数据一致性和线程安全性

测试完成后会自动清理测试数据，确保不遗留临时文件。

@module: stress_test
@author: KVDB Team
@version: 1.0
"""

import os
import sys
import time
import random
import threading
import shutil

# 将项目根目录添加到 Python 路径中，以便正确导入 python.kv_db 模块
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from python.kv_db import KVDB


def stress_test():
    """
    执行 KV 数据库综合压力测试
    
    本函数依次执行三项核心测试：
    1. 顺序写入测试：连续写入 10000 个键值对，统计写入耗时和吞吐量
    2. 随机读取测试：随机读取已写入的键值对，验证数据正确性并统计读取性能
    3. 并发访问测试：启动 10 个线程同时进行读写操作，验证线程安全性
    
    测试流程：
        - 创建临时测试数据库目录
        - 执行各项性能测试并输出统计结果
        - 验证数据一致性（通过 assert 断言）
        - 关闭数据库连接并清理测试文件
    
    @raises: AssertionError - 当读取的数据与预期值不匹配时抛出
    @raises: Exception - 数据库操作异常时抛出
    
    @note: 测试数据库路径为 ./stress_test_db，测试完成后自动删除
    """
    # 配置测试数据库路径
    db_path = "./stress_test_db"
    
    # 清理已存在的测试数据库目录，确保测试环境干净
    if os.path.exists(db_path):
        shutil.rmtree(db_path)
    
    # 初始化 KV 数据库实例
    db = KVDB(db_path)
    
    # 设置测试键值对数量
    num_keys = 10000
    
    print(f"开始压力测试，共 {num_keys} 个键值对...")
    
    # ==================== 测试项 1: 顺序写入性能 ====================
    start = time.time()
    for i in range(num_keys):
        # 依次写入键值对，键名格式：key_0, key_1, key_2...
        db.set(f"key_{i}", f"value_{i}")
    end = time.time()
    
    # 计算并输出写入性能指标
    write_duration = end - start
    write_ops_per_sec = num_keys / write_duration
    print(f"  顺序写入耗时：{write_duration:.2f}秒 (吞吐量：{write_ops_per_sec:.2f} 操作/秒)")
    
    # ==================== 测试项 2: 随机读取性能 ====================
    start = time.time()
    for i in range(num_keys):
        # 随机生成索引，模拟真实场景下的随机访问模式
        idx = random.randint(0, num_keys - 1)
        val = db.get(f"key_{idx}")
        # 断言验证：确保读取的值与写入的值一致
        assert val == f"value_{idx}", f"数据不一致：key_{idx} 期望值 value_{idx}, 实际值 {val}"
    end = time.time()
    
    # 计算并输出读取性能指标
    read_duration = end - start
    read_ops_per_sec = num_keys / read_duration
    print(f"  随机读取耗时：{read_duration:.2f}秒 (吞吐量：{read_ops_per_sec:.2f} 操作/秒)")
    
    # ==================== 测试项 3: 多线程并发访问 ====================
    print("  测试多线程并发访问...")
    
    def worker(worker_id):
        """
        并发测试工作线程函数
        
        每个线程独立写入和读取自己的键值对，避免键名冲突。
        键名格式：thread_{worker_id}_key_{i}
        
        @param worker_id: 线程标识符，用于区分不同线程的数据
        """
        for i in range(1000):
            # 线程写入操作
            db.set(f"thread_{worker_id}_key_{i}", i)
            # 线程读取并验证操作
            val = db.get(f"thread_{worker_id}_key_{i}")
            # 断言验证数据一致性
            assert val == i, f"线程 {worker_id} 数据不一致：期望 {i}, 实际 {val}"

    # 创建线程列表
    threads = []
    start = time.time()
    
    # 启动 10 个并发线程
    for i in range(10):
        t = threading.Thread(target=worker, args=(i,), name=f"Worker-{i}")
        threads.append(t)
        t.start()
    
    # 等待所有线程执行完成
    for t in threads:
        t.join()
    end = time.time()
    
    # 输出并发测试性能指标
    concurrent_duration = end - start
    total_ops = 10 * 10000  # 10 个线程 × 10000 次操作
    print(f"  并发访问测试 (10 线程，共 {total_ops} 次操作): {concurrent_duration:.2f}秒")
    
    # 关闭数据库连接，释放资源
    db.close()
    
    # 清理测试数据库目录
    if os.path.exists(db_path):
        shutil.rmtree(db_path)
        # pass
    
    print("✓ 压力测试全部通过！")


if __name__ == "__main__":
    # 当直接运行此脚本时，执行压力测试
    stress_test()