# ============================================================================
# KV-DB Makefile (POSIX Optimized)
# 支持平台: Linux, macOS
# ============================================================================

CC ?= gcc
CFLAGS = -Wall -Wextra -fPIC -Iinclude -std=c11 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -shared -lz -lpthread

# 调试与发布模式切换
# 使用方式: make DEBUG=1
ifeq ($(DEBUG), 1)
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O3 -DNDEBUG
endif

# 自动识别操作系统并设置动态库后缀
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S), Darwin)
    LIB_EXT = dylib
else
    LIB_EXT = so
endif

SRC_DIR = src
TEST_DIR = tests
OBJ_DIR = obj

SRC = $(SRC_DIR)/kv_store.c
OBJ = $(OBJ_DIR)/kv_store.o
LIB = libkvdb.$(LIB_EXT)

# 默认目标
all: $(OBJ_DIR) $(LIB)

# 创建对象文件目录
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# 编译动态库
$(LIB): $(OBJ)
	$(CC) -shared -o $@ $^ -lz -lpthread

# 编译 C 源代码
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 运行 C 核心单元测试
test: all
	$(CC) $(CFLAGS) $(TEST_DIR)/test_kv.c -o $(TEST_DIR)/test_kv -L. -lkvdb -Wl,-rpath,.
	@echo ">>> 正在运行 C 核心单元测试..."
	./$(TEST_DIR)/test_kv

# 清理构建产物
clean:
	rm -rf $(OBJ_DIR) $(LIB) $(TEST_DIR)/test_kv
	rm -rf ./test_db ./python_test_db ./stress_test_db

.PHONY: all clean test
