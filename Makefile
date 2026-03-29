# ============================================================================
# KV-DB Makefile
# 支持平台: Linux, macOS, Windows (MinGW)
# ============================================================================

CC ?= gcc
CFLAGS = -Wall -Wextra -fPIC -Iinclude -std=c11
LIBS = -lz

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
    LIBS += -lpthread
    CFLAGS += -D_POSIX_C_SOURCE=200809L
else ifneq (,$(findstring MINGW,$(UNAME_S)))
    LIB_EXT = dll
    # MinGW 下使用 Win32 原生 API，不需要 pthread
    LIBS += -lkernel32
    # Windows 下可执行文件后缀
    EXE_EXT = .exe
    # Windows 不支持 rpath
    RPATH_FLAG = 
else
    LIB_EXT = so
    LIBS += -lpthread
    CFLAGS += -D_POSIX_C_SOURCE=200809L
    RPATH_FLAG = -Wl,-rpath,.
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
	$(CC) -shared -o $@ $^ $(LIBS)

# 编译 C 源代码
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 运行 C 核心单元测试
test: all
	$(CC) $(CFLAGS) $(TEST_DIR)/test_kv.c -o $(TEST_DIR)/test_kv$(EXE_EXT) -L. -lkvdb $(RPATH_FLAG) $(LIBS)
	@echo ">>> 正在运行 C 核心单元测试..."
	./$(TEST_DIR)/test_kv$(EXE_EXT)

# 清理构建产物
clean:
	rm -rf $(OBJ_DIR) $(LIB) $(TEST_DIR)/test_kv
	rm -rf ./test_db ./python_test_db ./stress_test_db

.PHONY: all clean test
