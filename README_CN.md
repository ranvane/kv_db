# 高性能键值数据库 (KV-DB)

[English Version](README.md)

这是一个基于 C 语言实现的单机高性能键值数据库，提供简洁的 Python API 接口。它采用类似 LSM-tree 的追加写日志（Append-only log）存储结构，并结合内存索引（Bitcask 模型）以实现极高的读写性能。

## 核心特性
- **C 语言核心引擎**：高性能、低延迟，采用追加写模式确保磁盘顺序 I/O。
- **Python API**：提供类似 SQLite 的简洁接口，支持 Python 原生类型（int, float, bool, str, dict, list）。
- **ACID 事务**：支持 `begin`、`commit` 和 `rollback`，确保数据操作的原子性和一致性。
- **内存索引**：在内存中维护哈希表索引，查询复杂度为 `O(1)`。
- **跨平台支持**：支持 Linux、macOS 和 Windows。
- **数据压缩**：集成 zlib 压缩，有效节省磁盘空间。
- **自动 CI/CD**：集成 GitHub Actions，自动构建各平台的 Wheel 安装包。

## 快速安装

### 1. 准备工作
确保系统已安装 `zlib` 开发库：
- **Linux (Ubuntu/Debian)**: `sudo apt-get install zlib1g-dev`
- **macOS**: 通常自带，或通过 `brew install zlib`
- **Windows**: 推荐使用预编译的 `zlib` 库并配置 `INCLUDE` 和 `LIB` 环境变量。

### 2. 通过 Wheel 安装
您可以直接从 GitHub Releases 下载对应平台的 `.whl` 文件，或者本地构建：
```bash
# 安装构建工具
pip install --upgrade build

# 构建并安装
python3 -m build
pip install dist/*.whl
```

## 多平台打包说明

### Linux / macOS
项目在类 Unix 系统下使用 `Makefile` 进行编译。确保系统中安装了 `gcc` (或 `clang`) 和 `make`。
构建命令：
```bash
python3 -m build
```

### Windows (MSVC)
Windows 下推荐使用 Visual Studio 编译环境。
1. 打开 "Developer Command Prompt for VS"。
2. 确保 `zlib.lib` 位于库搜索路径中。
3. 运行构建：
```bash
python -m build
```
*注：`setup.py` 会自动调用 `cl.exe` 进行编译并将生成的 `libkvdb.dll` 打包。*

## 自动化构建 (GitHub Actions)
项目包含 `.github/workflows/build_wheels.yml` 脚本。当您将代码推送到 GitHub 或发布新版本（Tag）时，Actions 会自动：
1. 在 Linux, macOS, Windows 矩阵环境中启动构建。
2. 使用 `cibuildwheel` 工具为不同 Python 版本生成 Wheel 包。
3. 将生成的安装包作为 Artifacts 上传，您可以直接在 Actions 页面下载。

## Python API 使用示例

```python
from kv_db import KVDB

# 连接数据库（若目录不存在则自动创建）
db = KVDB("./my_db")

# 基础 CRUD 操作
db.set("name", "Trae")
db.set("data", {"version": "1.0", "tags": ["fast", "reliable"]})

print(db.get("name"))  # 输出: Trae
print(db.get("data"))  # 输出: {'version': '1.0', 'tags': ['fast', 'reliable']}

# 事务处理
db.begin()
db.set("key1", "val1")
db.commit()

# 关闭连接
db.close()
```

## 编译与测试 (开发者)

```bash
# 运行 C 语言核心测试
make test

# 运行 Python 接口验证 (安装后)
python3 tests/verify_wheel.py
```
## GitHub Actions编译与测试 (开发者)
只需将代码推送到 GitHub 并打上版本标签（如 git tag v1.0.1 && git push origin v1.0.1 ），GitHub Actions 将会自动：

1. 在 Windows 虚拟机上启动 vcpkg 安装依赖。
2. 使用 setuptools 自动调用 MSVC 编译器完成 src/kv_store.c 的编译。
3. 生成包含 libkvdb.so (即 DLL) 的 Windows Wheel 包并发布到 Release。