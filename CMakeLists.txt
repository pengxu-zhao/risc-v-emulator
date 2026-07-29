cmake_minimum_required(VERSION 3.12)

project(riscv-emulator
    VERSION 1.0.0
    DESCRIPTION "RISC-V emulator"
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
        -Wall #-Wextra -pedantic
        -fno-omit-frame-pointer  # 保留帧指针
        -Wno-unused-parameter              # 抑制常见噪音
        -Wno-unused-variable
        -Wno-sign-compare
    )
    # 设置链接器选项
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -g3 -ggdb3")
else()
    add_compile_options(-O2 -Wall -g)  # Release 也保留 -g
endif()

# 头文件目录

include_directories(${CMAKE_SOURCE_DIR}/softfloat/source/include)
include_directories(${CMAKE_SOURCE_DIR}/softfloat/source/RISCV)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)



file(GLOB SOFTFLOAT_CORE_SRC_FILES
    ${CMAKE_SOURCE_DIR}/softfloat/source/*.c
)
# 转换核心文件为绝对路径
set(SOFTFLOAT_CORE_FULL "")
foreach(src_file ${SOFTFLOAT_CORE_SRC_FILES})
    list(APPEND SOFTFLOAT_CORE_FULL ${src_file})
endforeach()

# ============================
# 2. 自动收集 RISCV 特化目录下的所有 .c 文件（绝对路径）
# ============================
file(GLOB SOFTFLOAT_RISCV_FILES
    ${CMAKE_SOURCE_DIR}/softfloat/source/RISCV/*.c
)
list(FILTER SOFTFLOAT_RISCV_FILES EXCLUDE REGEX ".*F128.*|.*extF80.*")

# ============================
# 3. 合并所有源文件
# ============================
set(SOFTFLOAT_ALL_SOURCES
    ${SOFTFLOAT_CORE_FULL}
    ${SOFTFLOAT_RISCV_FILES}
)

message(STATUS "SOFTFLOAT_ALL_SOURCES = ${SOFTFLOAT_ALL_SOURCES}")
# 建立静态库
add_library(softfloat STATIC ${SOFTFLOAT_ALL_SOURCES})

# 头文件路径
target_include_directories(softfloat PUBLIC
    ${CMAKE_SOURCE_DIR}/softfloat/source/include
    ${CMAKE_SOURCE_DIR}/softfloat/source/RISCV
    ${CMAKE_SOURCE_DIR}/softfloat/source
)

target_compile_options(softfloat PRIVATE -fkeep-inline-functions)


# 关键宏定义：启用 64 位优化，指定小端，避免不完整类型问题
target_compile_definitions(softfloat PRIVATE
    SOFTFLOAT_FAST_INT64
    LITTLEENDIAN=1
    INLINE=
)
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


