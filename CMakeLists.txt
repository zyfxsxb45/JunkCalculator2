# ==============================================================================
# Junk Calculator 2.2.1 - Ultimate CMake Build Script
# ==============================================================================
cmake_minimum_required(VERSION 3.15)

# ★ 开启 CMake 3.15+ 的 MSVC 纯净运行库控制策略
cmake_policy(SET CMP0091 NEW)
# ★ 优雅地强制所有模式静态链接，彻底告别 VCRUNTIME.dll 丢失与 D9025 警告！
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(JunkCalculator2 VERSION 2.2.1 LANGUAGES CXX)

# 要求 C++20 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# 核心源文件与头文件收集
# ==============================================================================
set(JC2_HEADERS
    "Token.h" "Lexer.h" "Expr.h" "Parser.h" "Value.h"
    "Complex.h" "Matrix.h" "BigInt.h" "Fraction.h" "Base.h"
    "Tolerance.h" "Image.h" "Probability.h" "Highlight.h"
    "HelpText.h" "Bytecode.h" "Compiler.h" "VM.h"
    "Module.h" "GcHeap.h" "BuiltinRegistry.h"
    "modules/json_module.h"
    "modules/image_module.h"
    "modules/prob_module.h"
    "modules/bytes_module.h" 
    "modules/socket_module.h"
)

set(JC2_SOURCES
    "Lexer.cpp" "Parser.cpp" "Compiler.cpp"
    "VM.cpp" "BuiltinRegistry.cpp" "main.cpp"
)

add_executable(JunkCalculator2 ${JC2_HEADERS} ${JC2_SOURCES})
target_include_directories(JunkCalculator2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ★ 核心修复：如果是 Windows 系统，必须链接 WinSock2 网络库！
if(WIN32)
    target_link_libraries(JunkCalculator2 PRIVATE ws2_32)
endif()

# ==============================================================================
# 编译器极限优化与安全级别
# ==============================================================================
if(MSVC)
    # /utf-8: 防止中文乱码 | /EHsc: C++ 异常处理模型
    target_compile_options(JunkCalculator2 PRIVATE /W4 /WX /utf-8 /bigobj /EHsc)
    # 开启 Release 模式下最高级别的全程序优化 (LTCG)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Oi /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
else()
    target_compile_options(JunkCalculator2 PRIVATE -Wall -Wextra -Werror -O3)
endif()

# ==============================================================================
# 构建后自动复制环境资源
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
# CPack 安装与发布打包规则
# ==============================================================================
install(TARGETS JunkCalculator2 DESTINATION bin)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data" DESTINATION bin)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib" DESTINATION bin)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION .)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt" DESTINATION .)
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "JunkCalculator2")
set(CPACK_PACKAGE_VERSION "2.2.1")
set(CPACK_PACKAGE_VENDOR "Yu Liangyang")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-Windows-x64")
include(CPack)