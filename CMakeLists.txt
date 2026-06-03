cmake_minimum_required(VERSION 3.20)
project(eskiuc VERSION 0.0.1 LANGUAGES C CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find LLVM (flexible version: 17+)
find_package(LLVM REQUIRED CONFIG)

# Include LLVM headers
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Add include directories for our project
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Source files
set(SOURCES
    main.cpp
    lexer/lexer.cpp
    parser/parser.cpp
    ast/ast.cpp
    ast/ast_printer.cpp
    sema/type_checker.cpp
    codegen/codegen.cpp
)

# Create executable
add_executable(eskiuc ${SOURCES})

# Link LLVM
llvm_map_components_to_libnames(llvm_libs support core irreader nativecodegen)
target_link_libraries(eskiuc ${llvm_libs})

# Compiler flags
if(MSVC)
  target_compile_options(eskiuc PRIVATE /W4)
else()
  target_compile_options(eskiuc PRIVATE -Wall -Wextra -Wpedantic)
endif()
