# ============================================================================
# KV-DB CMakeLists.txt (POSIX Optimized)
# ============================================================================
cmake_minimum_required(VERSION 3.10)
project(kv_db C)

# 强制 C11 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 平台检查：仅支持 POSIX
if(WIN32)
    message(FATAL_ERROR "此项目现已完全切换为 POSIX 标准实现，不再支持 Windows 环境。")
endif()

# 查找依赖
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# 编译选项
add_compile_options(-Wall -Wextra -fPIC)
add_definitions(-D_POSIX_C_SOURCE=200809L)

# 核心共享库
add_library(kvdb SHARED src/kv_store.c)

target_include_directories(kvdb PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(kvdb PRIVATE 
    ZLIB::ZLIB
    Threads::Threads
)

# 设置库产物属性
set_target_properties(kvdb PROPERTIES
    OUTPUT_NAME "kvdb"
    VERSION ${PROJECT_VERSION}
)

# 单元测试
enable_testing()
add_executable(test_kv tests/test_kv.c)
target_link_libraries(test_kv kvdb)
add_test(NAME kv_core_test COMMAND test_kv)

# 安装规则
include(GNUInstallDirs)
install(TARGETS kvdb
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES include/kv_store.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# 卸载支持
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
endif()
