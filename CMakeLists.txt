# CMakeLists.txt (备选构建系统)
cmake_minimum_required(VERSION 3.10)
project(kv_db C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 查找 zlib
find_package(ZLIB REQUIRED)

# 源文件
set(SOURCES src/kv_store.c)

# 创建共享库
add_library(kvdb SHARED ${SOURCES})

target_include_directories(kvdb PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
    ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(kvdb PRIVATE ${ZLIB_LIBRARIES})

# 平台特定配置
if(WIN32)
    target_compile_definitions(kvdb PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# 测试
enable_testing()
add_executable(test_kv tests/test_kv.c)
target_link_libraries(test_kv kvdb)
add_test(NAME kv_test COMMAND test_kv)