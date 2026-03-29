# 高性能键值数据库 (KV-DB)

[English Version](README.md)

这是一个基于 C 语言实现的单机高性能键值数据库，提供简洁的 Python API 接口。它采用类似 LSM-tree 的追加写日志（Append-only log）存储结构，并结合内存索引（Bitcask 模型）以实现极高的读写性能。

**注意：本项目目前仅支持 POSIX 兼容系统（如 Linux 和 macOS）。**

## 核心特性
- **高性能引擎**：基于追加写日志（Append-only log）的存储模型，确保磁盘顺序 I/O。
- **内存索引**：在内存中维护哈希表索引（使用 `uthash`），实现 `O(1)` 查询性能。
- **POSIX 标准**：完全基于 POSIX 标准接口实现（pthread, stdatomic, pread 等）。
- **ACID 事务**：支持 `begin`、`commit` 和 `rollback`，确保数据操作的原子性和一致性。
- **数据压缩**：集成 zlib 压缩，有效节省磁盘空间。
- **Python 绑定**：提供类似内置字典的简洁接口，支持 Python 原生类型（int, float, bool, str, dict, list）。
- **崩溃恢复**：启动时自动扫描数据文件重建索引，确保数据持久性。

## 快速安装

### 1. 依赖准备
- **Linux**: `sudo apt-get install zlib1g-dev`
- **macOS**: `brew install zlib`

### 2. 通过 Wheel 安装
```bash
# 推荐使用 build 工具进行打包安装
pip install build
python3 -m build
pip install dist/*.whl
```

## 使用示例 (Python)

```python
from kv_db import KVDB

# 连接数据库（若目录不存在则自动创建）
db = KVDB("./mydb")

# 基础操作
db.set("name", "Trae")
db.set("info", {"role": "AI Assistant", "version": 1.0})

print(db.get("name"))  # 输出: Trae
print(db.get("info"))  # 输出: {'role': 'AI Assistant', 'version': 1.0}

# 事务支持
db.begin()
db.set("key1", "val1")
db.commit()

db.close()
```

## 自动化 CI/CD
项目集成 GitHub Actions，每当推送 Tag 时，会自动触发以下流程：
1. **多平台构建**：在 Linux 和 macOS 上并行编译。
2. **自动化测试**：运行 C 单元测试和 Python 压力测试。
3. **自动发布**：将编译好的 Wheel 包自动上传至 GitHub Releases。

## 贡献指南
欢迎提交 Issue 或 Pull Request！请确保您的代码符合 POSIX 标准并保持良好的跨平台（Linux/macOS）兼容性。
