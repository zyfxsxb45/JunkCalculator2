# ==============================================================================
# Junk Calculator 2 - Ultimate CMake Build Script
# Version: 2.3.1.1
# ==============================================================================
cmake_minimum_required(VERSION 3.15)

# ★ Enable CMake 3.15+ MSVC clean runtime control policy
cmake_policy(SET CMP0091 NEW)

# ★ Elegantly force static linking (MT/MTd) across all configs.
# This permanently eliminates the "missing VCRUNTIME140.dll" error on target machines!
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(JunkCalculator2 VERSION 2.3.1.1 LANGUAGES CXX)

# Enforce C++ 20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# Core Sources & Headers Collection
# ==============================================================================
set(JC2_HEADERS
    "Token.h" "Lexer.h" "Expr.h" "Parser.h" "Value.h"
    "Complex.h" "Matrix.h" "BigInt.h" "Fraction.h" "Base.h"
    "Tolerance.h" "Image.h" "Probability.h" "Highlight.h"
    "HelpText.h" "Bytecode.h" "Compiler.h" "VM.h"
    "Module.h" "GcHeap.h" "BuiltinRegistry.h" "Symbolic.h"
    "Integration.h" "Factorization.h" "Groebner.h"
    "modules/json_module.h"
    "modules/image_module.h"
    "modules/prob_module.h"
    "modules/bytes_module.h" 
    "modules/socket_module.h"
    "modules/window_module.h"
    "modules/latex_module.h"
)

set(JC2_SOURCES
    "Lexer.cpp" "Parser.cpp" "Compiler.cpp"
    "VM.cpp" "BuiltinRegistry.cpp" "main.cpp"
    "Symbolic.cpp" "Integration.cpp" "Factorization.cpp" "Groebner.cpp"
)

add_executable(JunkCalculator2 ${JC2_HEADERS} ${JC2_SOURCES})
target_include_directories(JunkCalculator2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ★ Critical Linkage for Win32 OS:
# ws2_32 (Networking), User32 & Gdi32 (Native GUI), imm32 (IME Focus Control)
if(WIN32)
    target_link_libraries(JunkCalculator2 PRIVATE ws2_32 User32 Gdi32 imm32)
endif()

# ==============================================================================
# Extreme Compiler Optimization & Safety Levels
# ==============================================================================
if(MSVC)
    # /utf-8: Prevent encoding corruption | /EHsc: Standard C++ exception handling
    target_compile_options(JunkCalculator2 PRIVATE /W4 /WX /utf-8 /bigobj /EHsc)
    
    # Enable Whole Program Optimization (LTCG) in Release mode for maximum speed
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Oi /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
else()
    target_compile_options(JunkCalculator2 PRIVATE -Wall -Wextra -Werror -O3)
endif()

# ==============================================================================
# Post-Build Resource Injection (Auto-Copying directories)
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

# ★ Added auto-copy for the showcase examples!
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_custom_command(TARGET JunkCalculator2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/examples"
        "$<TARGET_FILE_DIR:JunkCalculator2>/examples"
        COMMENT "Copying examples/ to build output directory"
    )
endif()

# ==============================================================================
# CPack Installation & Release Packaging Rules
# ==============================================================================
install(TARGETS JunkCalculator2 DESTINATION bin)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data" DESTINATION bin)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib" DESTINATION bin)
endif()
# ★ Make sure examples are included in the released ZIP!
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/examples" DESTINATION bin)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION .)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt" DESTINATION .)
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "JunkCalculator2")
set(CPACK_PACKAGE_VERSION "2.3.1.1")
set(CPACK_PACKAGE_VENDOR "Yu Liangyang")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-Windows-x64")
include(CPack)

