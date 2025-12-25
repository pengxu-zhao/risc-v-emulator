cmake_minimum_required(VERSION 3.12)

project(riscv-emulator
    VERSION 1.0.0
    DESCRIPTION "A simple RISC-V emulator"
    LANGUAGES C CXX
)

# 设置标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)   # 建议至少 C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(
        -g3                  # 最大调试信息
        -ggdb3               # 生成 GDB 专用调试信息
        -O0                  # 无优化，便于调试
        -Wall -Wextra -pedantic
        -fno-omit-frame-pointer  # 保留帧指针
        -fno-inline          # 禁止内联，便于单步调试
    )
    # 设置链接器选项
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -g3 -ggdb3")
else()
    add_compile_options(-O2 -Wall -g)  # Release 也保留 -g
endif()

# 头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 子目录
add_subdirectory(src)

# 测试目录
option(BUILD_TESTS "Build test programs" ON)
if(BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests AND IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_subdirectory(tests ${CMAKE_BINARY_DIR}/test-build)
endif()

# 显示信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Output directory: ${CMAKE_BINARY_DIR}/bin")
