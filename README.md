# High-Performance Key-Value Database (KV-DB)

[中文版文档 (Chinese Version)](README_CN.md)

A high-performance, single-machine key-value database system implemented in C with a Python API wrapper. It features LSM-tree like append-only storage, in-memory indexing (Bitcask model), ACID transactions, and full POSIX support.

**Note: This project currently only supports POSIX-compatible systems (e.g., Linux and macOS).**

## Core Features
- **High-Performance Engine**: Append-only log storage ensures sequential disk I/O.
- **In-Memory Indexing**: Fast O(1) lookups using a hash table (`uthash`).
- **POSIX Standard**: Fully implemented based on POSIX standard interfaces (pthread, stdatomic, pread, etc.).
- **ACID Transactions**: Atomic operations with `begin`, `commit`, and `rollback`.
- **Data Compression**: Integrated zlib compression for efficient storage.
- **Python Bindings**: Simple dictionary-like interface supporting native Python types.
- **Crash Recovery**: Rebuilds index from data files on startup to ensure durability.

## Quick Installation

### 1. Prerequisites
- **Linux**: `sudo apt-get install zlib1g-dev`
- **macOS**: `brew install zlib`

### 2. Install via Wheel
```bash
# Recommended: Build and install using the standard 'build' module
pip install build
python3 -m build
pip install dist/*.whl
```

## API Usage (Python)

```python
from kv_db import KVDB

# Connect to database (creates directory if it doesn't exist)
db = KVDB("./mydb")

# Basic CRUD
db.set("name", "Trae")
db.set("data", {"version": "1.0", "features": ["fast", "reliable"]})

print(db.get("name")) # Output: Trae
print(db.get("data")) # Output: {'version': '1.0', 'features': ['fast', 'reliable']}

# Transactions
db.begin()
db.set("key1", "val1")
db.commit()

db.close()
```

## CI/CD with GitHub Actions
The project uses GitHub Actions for automated workflows:
1. **Multi-Platform Build**: Parallel compilation on Linux and macOS.
2. **Automated Testing**: Runs C unit tests and Python stress tests.
3. **Automated Release**: Uploads built Wheels to GitHub Releases on tag pushes.

## Contributing
Issues and Pull Requests are welcome! Please ensure your code adheres to POSIX standards and maintains cross-platform (Linux/macOS) compatibility.
