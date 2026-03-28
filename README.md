# High-Performance Key-Value Database (KV-DB)

[中文版文档 (Chinese Version)](README_CN.md)

A high-performance, single-machine key-value database system implemented in C with a Python API wrapper. It features LSM-tree like append-only storage, in-memory indexing (Bitcask model), ACID transactions, and cross-platform support.

## Core Features
- **C-based Storage Engine**: Fast append-only storage with Bitcask-style indexing.
- **Python API**: SQLite-like simplicity with support for Python types.
- **ACID Transactions**: Support for `begin`, `commit`, and `rollback`.
- **Cross-Platform**: Support for Linux, macOS, and Windows.
- **Automatic CI/CD**: GitHub Actions integrated for automatic Wheel building on multiple platforms.

## Quick Installation

### 1. Prerequisites
Ensure `zlib` development headers are installed:
- **Linux**: `sudo apt-get install zlib1g-dev`
- **macOS**: `brew install zlib`
- **Windows**: Configure `zlib.lib` in your environment.

### 2. Install via Wheel
Build and install directly:
```bash
pip install --upgrade build
python3 -m build
pip install dist/*.whl
```

## Cross-Platform Packaging

### Linux / macOS
The project uses `Makefile` for C compilation. Build with:
```bash
python3 -m build
```

### Windows (MSVC)
Use the "Developer Command Prompt for VS" and run:
```bash
python -m build
```
The `setup.py` script handles `cl.exe` calls and `libkvdb.dll` packaging automatically.

## CI/CD with GitHub Actions
The project includes a `.github/workflows/build_wheels.yml` workflow. Upon pushing to `main` or creating a tag, it:
1. Triggers builds on Ubuntu, macOS, and Windows runners.
2. Uses `cibuildwheel` to generate Wheels for various Python versions.
3. Uploads all `.whl` files as artifacts.

## API Usage (Python)

```python
from kv_db import KVDB

db = KVDB("./my_db")
db.set("name", "Trae")
print(db.get("name"))
db.close()
```

## Development
```bash
# Run C core tests
make test

# Verify Python installation
python3 tests/verify_wheel.py
```
