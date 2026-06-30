cmake_minimum_required(VERSION 3.20)
project(eskiuc VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -DESKIU_STATIC=ON links LLVM statically (used for release binaries)
option(ESKIU_STATIC "Link LLVM libraries statically" OFF)

find_package(LLVM REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCES
    main.cpp
    main_support.cpp
    lexer/lexer.cpp
    lexer/preprocessor.cpp
    parser/parser.cpp
    parser/parse_decl.cpp
    parser/parse_stmt.cpp
    parser/parse_expr.cpp
    ast/ast.cpp
    ast/ast_printer.cpp
    sema/type.cpp
    sema/type_checker.cpp
    sema/typecheck_decl.cpp
    sema/typecheck_stmt.cpp
    sema/typecheck_expr.cpp
    sema/typecheck_type.cpp
    sema/async_transform.cpp
    codegen/codegen_module.cpp
    codegen/codegen_type.cpp
    codegen/codegen_scope.cpp
    codegen/codegen_decl.cpp
    codegen/codegen_stmt.cpp
    codegen/codegen_expr.cpp
    codegen/codegen_call.cpp
    codegen/codegen_closure.cpp
    codegen/codegen_adt.cpp
)

add_executable(eskiuc ${SOURCES})

# X86 backend available unless static build on Apple Silicon
if(NOT (ESKIU_STATIC AND APPLE))
    target_compile_definitions(eskiuc PRIVATE ESKIU_HAS_X86)
endif()

if(ESKIU_STATIC)
    # Static LLVM — release binaries.
    # On Apple Silicon, X86 static libs are not available in Homebrew LLVM.
    if(APPLE)
        set(STATIC_BACKENDS "aarch64codegen aarch64asmparser aarch64desc aarch64info")
    else()
        set(STATIC_BACKENDS "aarch64codegen aarch64asmparser aarch64desc aarch64info x86codegen x86asmparser x86desc x86info")
    endif()
    execute_process(
        COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --link-static --libs
                support core irreader nativecodegen ${STATIC_BACKENDS}
        OUTPUT_VARIABLE LLVM_STATIC_LIBS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --system-libs
        OUTPUT_VARIABLE LLVM_SYSTEM_LIBS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    separate_arguments(LLVM_STATIC_LIBS UNIX_COMMAND "${LLVM_STATIC_LIBS}")
    separate_arguments(LLVM_SYSTEM_LIBS UNIX_COMMAND "${LLVM_SYSTEM_LIBS}")
    target_link_libraries(eskiuc ${LLVM_STATIC_LIBS} ${LLVM_SYSTEM_LIBS})
else()
    # Dynamic LLVM — default for development (X86 available as dylib on all platforms)
    llvm_map_components_to_libnames(llvm_libs
        support core irreader
        nativecodegen
        aarch64codegen aarch64asmparser aarch64desc aarch64info
        x86codegen x86asmparser x86desc x86info)
    target_link_libraries(eskiuc ${llvm_libs})
endif()

# The sanitizer instrumentation (ASan) is emitted by this exact LLVM, so its
# runtime must match — bake in the LLVM toolchain's clang for sanitized links.
target_compile_definitions(eskiuc PRIVATE
    ESKIU_LLVM_BINDIR="${LLVM_TOOLS_BINARY_DIR}")

if(MSVC)
    target_compile_options(eskiuc PRIVATE /W4)
else()
    target_compile_options(eskiuc PRIVATE -Wall -Wextra -Wpedantic)
endif()
