# 定义源文件列表
set(SOURCES
    main.c        # 主入口
    memory.c      # 内存子系统
    cpu.c         # CPU 核心
    instructions.c # 指令解码/执行
    decode.c
    elf_load.c
    trap.c
    mmu.c
    uart.c
    clint.c
    bus.c
    trap_vector.c
    dts.c
    plic.c
    virtio_blk.c

    # 其他源文件可以继续添加
)

# 定义可执行文件
add_executable(rv-emulator ${SOURCES})

# 指定依赖的头文件目录
target_include_directories(rv-emulator PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# 链接 C++ 标准库（对于 freestanding 交叉编译一般 g++ 会自动加）
# 但加上也没坏
target_link_libraries(rv-emulator PRIVATE stdc++)

# 可选：指定 C++ 标准（这里和根目录保持一致）
set_target_properties(rv-emulator PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# 可选：设置输出目录，覆盖根目录设置
set_target_properties(rv-emulator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
