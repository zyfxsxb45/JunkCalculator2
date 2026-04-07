# ==============================================================================
# Junk Calculator 2.1.0 - Ultimate CMake Build Script
# Developed by Yu Liangyang, Tsinghua University
# ==============================================================================
cmake_minimum_required(VERSION 3.15)
project(JunkCalculator2 VERSION 2.1.0 LANGUAGES CXX)

# 要求 C++20 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# 核心源文件与头文件收集
# ==============================================================================
set(JC2_HEADERS
    "Token.h"
    "Lexer.h"
    "Expr.h"
    "Parser.h"
    "Value.h"
    "Evaluator.h"
    "Complex.h"
    "Matrix.h"
    "BigInt.h"
    "Fraction.h"
    "Base.h"
    "Tolerance.h"
    "Image.h"
    "Probability.h"
    "Highlight.h"
    "HelpText.h"
    "Bytecode.h"
    "Compiler.h"
    "VM.h"
    "Module.h"
    "modules/json_module.h"
    "modules/image_module.h"
    "modules/prob_module.h"
)

set(JC2_SOURCES
    "Lexer.cpp"
    "Parser.cpp"
    "Evaluator.cpp"
    "Compiler.cpp"
    "VM.cpp"
    "main.cpp"
)

add_executable(JunkCalculator2 ${JC2_HEADERS} ${JC2_SOURCES})
target_include_directories(JunkCalculator2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ==============================================================================
# 编译器极限优化与静态链接绑定 (防止缺失 VCRUNTIME.dll)
# ==============================================================================
if(MSVC)
    # /utf-8: 强制国际化字符编码，防止中文乱码
    # /W4 /WX: 严苛警告并视为错误
    target_compile_options(JunkCalculator2 PRIVATE /W4 /WX /utf-8 /bigobj)
    
    # 开启 Release 模式下最高级别的全程序优化 (Link Time Code Generation)
    # /MT: 强制把 C++ 运行库静态打进 exe 肚子里，实现单文件纯绿色版！
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Oi /GL /MT")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
else()
    target_compile_options(JunkCalculator2 PRIVATE -Wall -Wextra -Werror -O3)
endif()

# ==============================================================================
# 自动化体验：构建后自动复制 data/ 和 lib/ 目录到 exe 旁边 (用于调试和测试)
# ==============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_custom_command(TARGET JunkCalculator2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:JunkCalculator2>/data"
        COMMENT "Copying data/ to build output directory"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    add_custom_command(TARGET JunkCalculator2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/lib"
        "$<TARGET_FILE_DIR:JunkCalculator2>/lib"
        COMMENT "Copying lib/ to build output directory"
    )
endif()

# ==============================================================================
# CPack 安装与发布打包规则 (生成最终的 Release ZIP 压缩包)
# ==============================================================================

# 1. 拷贝核心程序
install(TARGETS JunkCalculator2 DESTINATION bin)

# 2. 强制创建并拷贝资源文件夹
# 如果源码目录下没有这两个文件夹，CMake 也会在打包时在 bin 里建出来
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data" DESTINATION bin)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib" DESTINATION bin)

# 3. 拷贝文档与许可协议 (消除打包软件的无协议警告，兼容带后缀的情况)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION .)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION .)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt" DESTINATION .)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md" DESTINATION .)
endif()

# 启用 CPack 生成 ZIP 包
set(CPACK_GENERATOR "ZIP") # 最通用的便携格式
set(CPACK_PACKAGE_NAME "JunkCalculator2")
set(CPACK_PACKAGE_VERSION "2.1.0")
set(CPACK_PACKAGE_VENDOR "Yu Liangyang")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-Windows-x64")
include(CPack)