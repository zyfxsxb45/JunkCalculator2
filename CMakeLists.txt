# ==============================================================================
# Junk Calculator 2.0 - Ultimate CMake Build Script
# Developed by Yu Liangyang
# ==============================================================================
cmake_minimum_required(VERSION 3.15)
project(JunkCalculator2 VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# 源文件收集
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
    "Module.h"
    "modules/json_module.h"
    "modules/prob_module.h"
    "modules/image_module.h"
)

set(JC2_SOURCES
    "Lexer.cpp"
    "Parser.cpp"
    "Evaluator.cpp"
    "main.cpp"
)

add_executable(JunkCalculator2 ${JC2_HEADERS} ${JC2_SOURCES} "modules/prob_module.h")
target_include_directories(JunkCalculator2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ==============================================================================
# 编译器优化与静态链接
# ==============================================================================
if(MSVC)
    target_compile_options(JunkCalculator2 PRIVATE /W4 /WX /utf-8)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Oi /GL /MT")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
else()
    target_compile_options(JunkCalculator2 PRIVATE -Wall -Wextra -Werror -O3)
endif()

# ==============================================================================
# 构建后自动复制 data/ 和 lib/ 到输出目录
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
# CPack 安装打包规则
# ==============================================================================
install(TARGETS JunkCalculator2 DESTINATION bin)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/ DESTINATION bin/data)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib/ DESTINATION bin/lib)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/README.md DESTINATION .)
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "JunkCalculator2")
set(CPACK_PACKAGE_VERSION "2.1.0")
set(CPACK_PACKAGE_VENDOR "Yu Liangyang")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-Windows-x64")
include(CPack)